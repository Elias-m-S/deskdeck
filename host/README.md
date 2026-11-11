# Host daemon

A small Python service that connects the DeskDeck to PipeWire or PulseAudio on Linux. It receives `@ADJ`/`@MUTE`/`@WATCH`/`@HELLO` from the device over the USB serial port, sets per-app volumes with `pactl`, and sends `@APPS`/`@LVL`/`@PING` back so the display always shows the real state.

```bash
sudo apt install python3-serial pulseaudio-utils

python3 deskdeck_daemon.py --list     # show running audio streams (names to match on)
python3 deskdeck_daemon.py --logs     # run in the foreground, also print the device logs
```

## Install as a service

```bash
./install.sh              # links the daemon to ~/.local/bin, installs + enables the systemd user unit
journalctl --user -u deskdeck -f
./install.sh uninstall
```

The daemon and `idf.py monitor` share the same port. While the service runs, the device logs are visible with `--logs`; `flash.sh` pauses the service automatically while flashing, for a monitor session stop it with `systemctl --user stop deskdeck`.

## Configuration

`~/.config/deskdeck/config.json` is created on first start:

```json
{
  "targets": {
    "CS2":     {"match": ["cs2"], "start_percent": 100},
    "Discord": {"match": ["discord"], "start_percent": 100},
    "Gesamt":  {"type": "sink"},
    "Brave":   {"match": ["brave"]}
  },
  "show_active_apps": false,
  "default_percent": 50
}
```

- The order of `targets` is the order in the device menu. At most 16 targets, names are cut to 23 characters.
- `match` is matched case-insensitively as a substring against `application.name` and `application.process.binary` of each audio stream (`--list` shows both).
- `"type": "sink"` (or `"source"`) binds a target to the default output (or input) instead of an app. The firmware treats the target named in `MASTER_TARGET_NAME` ("Gesamt") specially: without the daemon it falls back to the USB media keys.
- `start_percent` sets every new stream of that app to this level until you turn its knob; afterwards new streams get the last value you set (until the daemon restarts).
- `show_active_apps` additionally offers every currently playing app in the menu.
- `default_percent` is the level an app gets when its knob is turned for the first time while no stream is running.

## Protocol

| Line | Direction | Meaning |
|---|---|---|
| `@HELLO` | device -> host | after boot or on reconnect, host resends everything |
| `@WATCH a\|b` | device -> host | targets currently on the knobs |
| `@ADJ +2 Discord` | device -> host | change volume by percentage points |
| `@MUTE Discord` | device -> host | toggle mute |
| `@PING` | host -> device | keep-alive every 2 s; after 5 s without, the device shows `kein PC-Dienst` |
| `@APPS a\|b\|c` | host -> device | selectable targets |
| `@LVL 42 0 1 Discord` | host -> device | percent, muted, active (stream running), name |

Lines not starting with `@` are the device's log output and are ignored (or echoed with `--logs`).
