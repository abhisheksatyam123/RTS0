#!/usr/bin/env python3
"""Fail if lockstep simulation code uses the C library's transcendental functions (G30).

glibc, Apple's libm, Android's bionic and the Windows CRT round these differently (and glibc and
the UCRT pick FMA or non-FMA code by CPU), so every call must go through src/core/math/det_math.h.
Flagged: a call under any qualifier but det:: (sin(x), std::sin(x), godot::Math::sin(x)), a
qualified or address-taken name without a call (static_cast<float (*)(float)>(std::sin), &sin,
(void *)&sinf), __builtin_sin and the rest, and std::polar and std::arg. Not flagged: det:: and
rtso::det::, members (.exp, ->log), and a bare name that is neither called nor address-taken (a
variable named log, also as std::vector<int> &log, Foo *&log or a lambda's [this, &log]).
A line that only feeds presentation (never the simulation state or its hash) may carry the
comment `det-exempt` to keep its <cmath> call.
Usage: check_det_math.py [repo root]  (exit 1 and file:line for each call found)
"""
import bisect, pathlib, re, sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '.')
SCAN = ['src/simulation', 'src/core', 'src/network/turn_manager.cpp', 'src/network/turn_manager.h']
ALLOW = {'src/core/math/det_math.h'}
LIBM = ('sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1|'
        'log|log2|log10|log1p|pow|hypot|cbrt|erf|erfc|lgamma|tgamma|sincos')
# group 1: GCC's builtin form; group 2: <complex>'s polar and arg (they call sin, cos and atan2), only
# flagged under std:: since the simulation has its own polar helpers.
NAME = re.compile(rf'(?<![\w$])(?:(__builtin_)(?:{LIBM})[fl]?|(?:{LIBM})[fl]?|(polar|arg))(?![\w$])')

def code_only(src: str) -> str:
    """Blank comments and string/char literals (raw strings too), keeping newlines."""
    out, i, n, state = [], 0, len(src), None
    while i < n:
        c = src[i]
        if state is None:
            if src.startswith('//', i): state = 'line'; out.append('  '); i += 2; continue
            if src.startswith('/*', i): state = 'block'; out.append('  '); i += 2; continue
            if c in '"\'':
                j = i  # the identifier or number just before the quote
                while j and (src[j - 1].isalnum() or src[j - 1] in '_.\''): j -= 1
                if c == "'" and re.match(r'\.?\d', src[j:i]):  # a digit separator: 1'000, 0xff'ff, .5'0
                    out.append(c); i += 1; continue
                d = src.find('(', i + 1, i + 18) if c == '"' and src[j:i] in ('R', 'u8R', 'uR', 'UR', 'LR') else -1
                if d != -1:  # R"delim( ... )delim": no escapes, and it may hold quotes and newlines
                    e = src.find(')' + src[i + 1:d] + '"', d + 1)
                    e = n if e == -1 else e + d - i + 1
                    out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:e])); i = e; continue
                state = c; out.append(' '); i += 1; continue
            out.append(c); i += 1; continue
        if state == 'line':
            if c == '\n': state = None
            out.append(c if c == '\n' else ' '); i += 1; continue
        if state == 'block':
            if src.startswith('*/', i): state = None; out.append('  '); i += 2; continue
            out.append(c if c == '\n' else ' '); i += 1; continue
        if c == '\\': out.append('  '); i += 2; continue
        if c == state: state = None
        out.append(c if c == '\n' else ' '); i += 1
    return ''.join(out)

def back(code: str, k: int) -> int:
    """k moved back over whitespace."""
    while k and code[k - 1] in ' \t\r\n': k -= 1
    return k

def qualifier(code: str, i: int):
    """The qualifier before code[i] (['godot', 'Math'] for godot::Math::sin, [''] for ::sin) and where it starts."""
    quals = []
    while True:
        k = back(code, i)
        if k < 2 or code[k - 2:k] != '::': return quals, i
        k = m = back(code, k - 2)
        while m and (code[m - 1].isalnum() or code[m - 1] == '_'): m -= 1
        quals.insert(0, code[m:k]); i = m
        if m == k: return quals, i  # ::sin, or Foo<T>::sin

def word_before(code: str, k: int) -> str:
    """The identifier (or number) ending at code[k - 1], '' if none."""
    w = k
    while w and (code[w - 1].isalnum() or code[w - 1] == '_'): w -= 1
    return code[w:k]

def operand_before(code: str, k: int) -> bool:
    """code[:k] ends with an operand (an identifier, ')' or ']'), so a & or ( there is binary or a call."""
    w = word_before(code, k)
    return w not in ('', 'return', 'throw', 'case') or code[k - 1:k] in (')', ']')

CAPTURE = set('_ \t\r\n,&=*.')  # a capture list's other characters: [this, =, *this, &a, b...]
def capture(code: str, amp: int, e: int) -> bool:
    """code[amp] is the & of a lambda's by-reference capture: [&log](, [this, &exp] mutable {."""
    i = amp
    while i and (code[i - 1].isalnum() or code[i - 1] in CAPTURE): i -= 1
    if code[i - 1:i] != '[' or operand_before(code, back(code, i - 1)): return False  # a[&sin] subscripts
    while e < len(code) and (code[e].isalnum() or code[e] in CAPTURE): e += 1
    if code[e:e + 1] != ']': return False
    e += 1
    while e < len(code) and code[e] in ' \t\r\n': e += 1
    return re.match(r'[({<]|->|(mutable|constexpr|consteval|noexcept|static)\b', code[e:e + 11]) is not None

TYPE_WORD = re.compile(r'void|bool|char|short|int|long|float|double|signed|unsigned|const|volatile|auto|\w+_t')
def cast(code: str, c: int) -> bool:
    """code[c] closes a C-style cast: (void *)&sinf, (double (*)(double))&sin, not (a) & exp or f(x) & exp."""
    depth, o = 0, c
    while o >= 0:
        depth += {')': 1, '(': -1}.get(code[o], 0)
        if depth == 0: break
        o -= 1
    if o < 0 or operand_before(code, back(code, o)): return False  # f(x) & exp, sizeof(int) & exp
    t = code[o + 1:c].strip()
    if t.endswith(('*', '&', '>')) or re.search(r'\([\w\s:]*\*\s*(const|volatile)?\s*\)|\*\s*[>,]', t): return True
    return re.fullmatch(r'[\w\s:<>,]*', t) is not None and any(TYPE_WORD.fullmatch(w) for w in re.findall(r'\w+', t))

def address_of(code: str, b: int, e: int) -> bool:
    """code[b - 1] is a unary &: not a binary & or && (mask & exp, (a) & exp, ok && log), a reference
    declarator (vector<int> &log, Foo *&exp) or a lambda's capture ([this, &log]). e ends the name."""
    if code[b - 1:b] != '&' or code[b - 2:b - 1] == '&': return False
    a = back(code, b - 1)
    p = code[a - 1:a]
    if p == ')': return cast(code, a - 1)
    if p == '>': return False  # the end of a type: const std::vector<int> &log
    if p == '*':  # Foo *&log or Foo<T> **&log declare; (*&sin)(x) and return *&sin dereference
        k = a - 1
        while k and code[k - 1] in '* \t\r\n': k -= 1
        return not (code[k - 1:k] == '>' or operand_before(code, k))
    if p in ('[', ',') and capture(code, b - 1, e): return False
    return not operand_before(code, a)

def uses(code: str):
    """(offset, text) of each libm use in comment- and literal-free code."""
    for m in NAME.finditer(code):
        quals, start = qualifier(code, m.start())
        b = back(code, start)
        if code[b - 1:b] == '.' or code[b - 2:b] == '->': continue  # a member: obj.exp, p->log(
        if quals and quals[-1] == 'det': continue  # det::sin, rtso::det::sin
        e = m.end()
        while e < len(code) and code[e] in ' \t\r\n': e += 1
        called, taken = code[e:e + 1] == '(', address_of(code, b, m.end())
        if m.group(2) and (not quals or quals[-1] != 'std'): continue  # the simulation's own polar()
        if not (m.group(1) or quals or called or taken): continue  # a variable or type named like one
        yield m.start(), ('&' if taken else '') + ''.join(q + '::' for q in quals) + m.group(0) + ('()' if called else '')

bad = 0
files = []
for s in SCAN:
    p = ROOT / s
    files += [p] if p.is_file() else sorted(q for q in p.rglob('*') if q.suffix in ('.h', '.hpp', '.cpp', '.cc'))
for f in files:
    rel = f.relative_to(ROOT).as_posix()
    if rel in ALLOW or '/0ad/' in rel: continue
    text = f.read_text(encoding='utf-8', errors='replace')
    raw = text.split('\n')
    lines = [m.start() for m in re.finditer('\n', text)]
    for at, what in uses(code_only(text)):
        ln = bisect.bisect_left(lines, at) + 1
        if 'det-exempt' in raw[ln - 1]: continue  # presentation-only maths, never hashed
        bad += 1
        print(f'{rel}:{ln}: {what} is the C library; use rtso::det:: (src/core/math/det_math.h)')
print(f'{bad} direct libm call(s) in simulation code' if bad else 'no direct libm calls in simulation code')
sys.exit(1 if bad else 0)
