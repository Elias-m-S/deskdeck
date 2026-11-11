"""Minimal S-expression parser/writer for KiCad files."""
import re


class Sym(str):
    """Unquoted atom"""


_tok = re.compile(r'\s*(\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+)', re.S)


def parse(text):
    pos = 0
    stack = [[]]
    while True:
        m = _tok.match(text, pos)
        if not m:
            break
        t = m.group(1)
        pos = m.end()
        if t == '(':
            stack.append([])
        elif t == ')':
            e = stack.pop()
            stack[-1].append(e)
        elif t.startswith('"'):
            stack[-1].append(t[1:-1].replace('\\"', '"').replace('\\\\', '\\'))
        else:
            stack[-1].append(Sym(t))
    return stack[0][0] if len(stack[0]) == 1 else stack[0]


def dump(e, indent=0):
    if isinstance(e, list):
        if not e:
            return '()'
        simple = all(not isinstance(x, list) for x in e)
        if simple:
            return '(' + ' '.join(dump(x) for x in e) + ')'
        head = []
        rest = []
        for x in e:
            if not rest and not isinstance(x, list):
                head.append(dump(x))
            else:
                rest.append(x)
        pad = '  ' * (indent + 1)
        s = '(' + ' '.join(head)
        for x in rest:
            s += '\n' + pad + dump(x, indent + 1)
        return s + '\n' + '  ' * indent + ')'
    if isinstance(e, Sym):
        return str(e)
    if isinstance(e, int):
        return str(e)
    if isinstance(e, float):
        r = f'{e:.4f}'.rstrip('0').rstrip('.')
        return r if r not in ('-0', '') else '0'
    s = str(e).replace('\\', '\\\\').replace('"', '\\"')
    return f'"{s}"'


def find(e, key):
    return [x for x in e if isinstance(x, list) and x and x[0] == key]


def first(e, key):
    r = find(e, key)
    return r[0] if r else None
