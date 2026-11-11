#!/usr/bin/env python3
"""
DeskDeck host daemon: connects the DeskDeck to PipeWire/PulseAudio.

- receives  @ADJ / @MUTE / @WATCH / @HELLO  from the device (CDC, /dev/ttyACM*)
- sets the per-app volume through `pactl`
- sends     @APPS / @LVL / @PING            back so the display stays in sync

Configuration: ~/.config/deskdeck/config.json (created on first start)

    python3 deskdeck_daemon.py            # normal operation
    python3 deskdeck_daemon.py --list     # show running audio streams (to set up the config)
    python3 deskdeck_daemon.py --logs     # additionally print the device's log output

Dependencies: sudo apt install python3-serial pulseaudio-utils
"""
import argparse
import glob
import json
import logging
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: sudo apt install python3-serial")

log = logging.getLogger("deskdeck")

CONFIG_PATH = Path.home() / ".config" / "deskdeck" / "config.json"
DEFAULT_CONFIG = {
    # Order = order in the device menu.
    # "sink"/"source" = default output/input, "match" = case-insensitive substring of
    # application.name or application.process.binary of an audio stream.
    # "start_percent" = volume every new stream of that app gets as long as you have not
    # touched the encoder since the daemon started (afterwards the last set value applies).
    "targets": {
        "CS2": {"match": ["cs2"], "start_percent": 100},
        "Discord": {"match": ["discord"], "start_percent": 100},
        "Gesamt": {"type": "sink"},
        "Brave": {"match": ["brave"]},
    },
    # Additionally offer every currently playing app in the menu
    "show_active_apps": False,
    # Volume an app gets when its encoder is turned for the first time while no stream is running
    "default_percent": 50,
}

PING_INTERVAL = 2.0
POLL_INTERVAL = 1.0
# The device stores at most this many targets / characters per name (see host_link.h)
MAX_TARGETS = 16
TARGET_NAME_LEN = 23


# --------------------------------------------------------------------------- pactl

def pactl(*args, check=True):
    r = subprocess.run(["pactl", *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"pactl {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout


def list_streams():
    """All playback streams (sink inputs) as a list of dicts."""
    data = json.loads(pactl("-f", "json", "list", "sink-inputs") or "[]")
    streams = []
    for s in data:
        props = s.get("properties", {})
        vols = [int(str(ch.get("value_percent", "0")).rstrip("%"))
                for ch in s.get("volume", {}).values()]
        streams.append({
            "index": s["index"],
            "name": props.get("application.name", ""),
            "binary": props.get("application.process.binary", ""),
            "percent": round(sum(vols) / len(vols)) if vols else 0,
            "muted": bool(s.get("mute", False)),
        })
    return streams


def device_level(kind):
    """kind = 'sink' | 'source' -> (percent, muted)"""
    dev = "@DEFAULT_SINK@" if kind == "sink" else "@DEFAULT_SOURCE@"
    vol = pactl(f"get-{kind}-volume", dev)
    m = re.search(r"(\d+)%", vol)
    muted = "yes" in pactl(f"get-{kind}-mute", dev)
    return (int(m.group(1)) if m else 0), muted


# --------------------------------------------------------------------------- targets

class Targets:
    def __init__(self, cfg):
        self.cfg = cfg
        self.stored = {}       # target -> wanted volume, even while the app is not running
        self.applied = {}      # stream index -> target we already adjusted that stream for

    def spec(self, name):
        for key, spec in self.cfg["targets"].items():
            if key.lower() == name.lower():
                return spec
        return {"match": [name]}  # dynamic entry from show_active_apps

    def names(self, streams):
        names = list(self.cfg["targets"].keys())
        if self.cfg.get("show_active_apps"):
            for s in streams:
                if s["name"] and not any(self.matches(n, s) for n in names):
                    names.append(s["name"])
        return list(dict.fromkeys(n[:TARGET_NAME_LEN] for n in names))[:MAX_TARGETS]

    def matches(self, name, stream):
        spec = self.spec(name)
        if "type" in spec:
            return False
        hay = f"{stream['name']} {stream['binary']}".lower()
        return any(m.lower() in hay for m in spec.get("match", []))

    def level(self, name, streams):
        """-> (percent, muted, active)"""
        spec = self.spec(name)
        if "type" in spec:
            pct, muted = device_level(spec["type"])
            return pct, muted, True
        hits = [s for s in streams if self.matches(name, s)]
        if hits:
            return hits[0]["percent"], hits[0]["muted"], True
        pct, muted = self.wanted().get(name, (self.cfg.get("default_percent", 50), False))
        return pct, muted, False

    def set(self, name, percent, muted, streams):
        percent = max(0, min(100, percent))
        spec = self.spec(name)
        if "type" in spec:
            dev = "@DEFAULT_SINK@" if spec["type"] == "sink" else "@DEFAULT_SOURCE@"
            pactl(f"set-{spec['type']}-volume", dev, f"{percent}%")
            pactl(f"set-{spec['type']}-mute", dev, "1" if muted else "0")
            return
        self.stored[name] = (percent, muted)
        for s in streams:
            if self.matches(name, s):
                pactl("set-sink-input-volume", str(s["index"]), f"{percent}%")
                pactl("set-sink-input-mute", str(s["index"]), "1" if muted else "0")
                self.applied[s["index"]] = name

    def wanted(self):
        """target -> (percent, muted) for new streams: last set on the encoder, else start_percent."""
        want = {name: (spec["start_percent"], False)
                for name, spec in self.cfg["targets"].items() if "start_percent" in spec}
        want.update(self.stored)
        return want

    def apply_to_new_streams(self, streams):
        """New streams (e.g. a Discord call starts) get the wanted volume."""
        changed = False
        alive = {s["index"] for s in streams}
        self.applied = {i: n for i, n in self.applied.items() if i in alive}
        for name, (pct, muted) in self.wanted().items():
            for s in streams:
                if s["index"] not in self.applied and self.matches(name, s):
                    log.info("new stream %s (%s) -> %s %d%%", s["index"], s["name"], name, pct)
                    pactl("set-sink-input-volume", str(s["index"]), f"{pct}%")
                    pactl("set-sink-input-mute", str(s["index"]), "1" if muted else "0")
                    self.applied[s["index"]] = name
                    changed = True
        return changed


# --------------------------------------------------------------------------- device

def find_port():
    hits = sorted(glob.glob("/dev/serial/by-id/*DeskDeck*"))
    return hits[0] if hits else None


class Session:
    def __init__(self, ser, targets, echo_logs):
        self.ser = ser
        self.t = targets
        self.echo_logs = echo_logs
        self.buf = b""
        self.sent_apps = None
        self.sent_lvl = {}
        self.watch = []
        self.last_ping = 0.0
        self.last_poll = 0.0
        self.streams = []

    def send(self, line):
        self.ser.write((line + "\n").encode())

    def resync(self):
        self.sent_apps = None
        self.sent_lvl = {}

    def publish(self, force_names=()):
        names = self.t.names(self.streams)
        if names != self.sent_apps:
            self.send("@APPS " + "|".join(names))
            self.sent_apps = names
        for name in dict.fromkeys(list(self.watch) + names + list(force_names)):
            lvl = self.t.level(name, self.streams)
            if self.sent_lvl.get(name) != lvl or name in force_names:
                pct, muted, active = lvl
                self.send(f"@LVL {pct} {int(muted)} {int(active)} {name}")
                self.sent_lvl[name] = lvl

    def handle(self, line):
        if not line.startswith("@"):
            if self.echo_logs and line.strip():
                print("DEV |", line)
            return
        cmd, _, arg = line.partition(" ")
        if cmd == "@HELLO":
            log.info("device says hello")
            self.resync()
        elif cmd == "@WATCH":
            self.watch = [w for w in arg.split("|") if w]
            log.info("encoders: %s", ", ".join(self.watch))
        elif cmd == "@ADJ":
            delta_s, _, name = arg.partition(" ")
            self.streams = list_streams()
            pct, muted, _ = self.t.level(name, self.streams)
            self.t.set(name, pct + int(delta_s), muted, self.streams)
            self.streams = list_streams()
            self.publish(force_names=[name])
        elif cmd == "@MUTE":
            name = arg
            self.streams = list_streams()
            pct, muted, _ = self.t.level(name, self.streams)
            self.t.set(name, pct, not muted, self.streams)
            self.streams = list_streams()
            self.publish(force_names=[name])

    def run(self):
        self.send("@PING")
        while True:
            chunk = self.ser.read(256)  # blocks at most until the timeout
            if chunk:
                self.buf += chunk
                while b"\n" in self.buf:
                    raw, self.buf = self.buf.split(b"\n", 1)
                    try:
                        self.handle(raw.decode(errors="replace").rstrip("\r"))
                    except RuntimeError as e:
                        log.warning("%s", e)

            now = time.monotonic()
            if now - self.last_poll >= POLL_INTERVAL:
                self.last_poll = now
                try:
                    self.streams = list_streams()
                    if self.t.apply_to_new_streams(self.streams):
                        self.streams = list_streams()
                    self.publish()
                except RuntimeError as e:
                    log.warning("%s", e)
            if now - self.last_ping >= PING_INTERVAL:
                self.last_ping = now
                self.send("@PING")


# --------------------------------------------------------------------------- main

def load_config():
    if not CONFIG_PATH.exists():
        CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
        CONFIG_PATH.write_text(json.dumps(DEFAULT_CONFIG, indent=2, ensure_ascii=False) + "\n")
        log.info("configuration created: %s", CONFIG_PATH)
    cfg = dict(DEFAULT_CONFIG)
    cfg.update(json.loads(CONFIG_PATH.read_text()))
    return cfg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port to use (default: auto-detect)")
    ap.add_argument("--list", action="store_true", help="show running audio streams and exit")
    ap.add_argument("--logs", action="store_true", help="also print the device's log output")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s", datefmt="%H:%M:%S")

    if args.list:
        for s in list_streams():
            print(f"#{s['index']:<5} {s['percent']:>3}%{' muted' if s['muted'] else '      '}  "
                  f"name={s['name']!r}  binary={s['binary']!r}")
        return

    targets = Targets(load_config())
    while True:
        port = args.port or find_port()
        if not port:
            log.info("DeskDeck not found, waiting ...")
            time.sleep(2)
            continue
        try:
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                log.info("connected to %s", port)
                Session(ser, targets, args.logs).run()
        except (serial.SerialException, OSError) as e:
            log.info("connection lost (%s), retrying ...", e)
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
