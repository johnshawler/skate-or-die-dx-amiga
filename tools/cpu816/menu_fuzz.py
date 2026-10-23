#!/usr/bin/env python3
"""Differential test: demo/src/menu.h (native build) against the original overlay F39 running on the 65816 core.
Both get the same control byte and keyboard value every pass; after each pass the menu variables, player tables, random-number state
and the displayed picture are compared.   usage: menu_fuzz.py [trials] [passes] [seed]"""
import ctypes, os, random, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, f'{HERE}/..')
import cpu816 as C, overworld_sim as O, flow
import sprites as SP

RES = f'{HERE}/../../work/res'
rd = lambda n: open(f'{RES}/F{n}.bin', 'rb').read()
lib = ctypes.CDLL(f'{HERE}/libmenu.so')
lib.t_fb.restype = ctypes.POINTER(ctypes.c_ubyte); lib.t_names.restype = ctypes.POINTER(ctypes.c_ubyte)
lib.t_pal.restype = ctypes.POINTER(ctypes.c_ushort); lib.t_scores.restype = ctypes.POINTER(ctypes.c_ubyte); lib.t_board.restype = ctypes.POINTER(ctypes.c_ubyte); lib.t_colour.restype = ctypes.POINTER(ctypes.c_ubyte)
FIELDS = 'state c2 c4 c6 c8 ca cc ce d2 d4 cx cy f4bcd f4bcf f4bd1 f4bd3 sign_frame pass f60 f62 f64 f66 f68 f6e f70 exit_code practice key gx gy mask tcol trow slot_x slot_y slot_f count f38 f3a'.split()
ADDR = dict(state=0x4BD5, c2=0xC2, c4=0xC4, c6=0xC6, c8=0xC8, ca=0xCA, cc=0xCC, ce=0xCE, d2=0xD2, d4=0xD4, cx=0x4BC7, cy=0x4BC9, f4bcd=0x4BCD,
            f4bcf=0x4BCF, f4bd1=0x4BD1, f4bd3=0x4BD3, sign_frame=0x3036, pass_=0x3038, f60=0x60, f62=0x62, f64=0x64, f66=0x66, f68=0x68, f6e=0x6E,
            f70=0x70, exit_code=0x6C, key=0x3064, gx=0x3044, gy=0x3046, mask=0x2E, tcol=0x303E, trow=0x3040, slot_x=0x4B8F, slot_y=0x4B9F, slot_f=0x4BAF, f38=0x38, f3a=0x3A)
ADDR['pass'] = ADDR.pop('pass_')
LONG = dict(practice=0x077E17, count=0x0768CC)
assert len(FIELDS) == lib.t_nfields()

pics = {0: rd(29), 1: rd(30), 2: rd(31), 3: rd(33), 4: rd(32)}
font = rd(2)[:0x60 * 32]
lib.t_init(); lib.t_set_font(font)
for k, v in pics.items(): lib.t_set_pic(k, v)   # 32768 bytes: pixels, SCB, palettes
cursor_tab = SP.load_table(f'{RES}/F36.bin'); cur_img = rd(37); cur_msk = rd(38)
_t = (ctypes.c_int * (len(cursor_tab) * 3))(*[v for fr in cursor_tab for v in (fr['w'], fr['words'], fr['off'])])
lib.t_disp.restype = ctypes.POINTER(ctypes.c_ubyte)
lib.t_set_sprites(_t, len(cursor_tab), cur_img, len(cur_img), cur_msk)

def emu_vars():
    v = {}
    for f in FIELDS:
        v[f] = C.rd16(LONG[f]) if f in LONG else C.rd16(0x030000 + ADDR[f])
    return v
def emu_tables():
    names = bytes(C.mem[0x076810:0x076810 + 128]); board = bytes(C.mem[0x0768FE:0x0768FE + 8]); colour = bytes(C.mem[0x0768EE:0x0768EE + 16])
    rng = C.rd8(0x033078) | (C.rd8(0x033079) << 8)
    return names, board, colour, rng, bytes(C.mem[0x077F00:0x077F00 + 192])

def model_vars():
    a = (ctypes.c_ushort * len(FIELDS))(); lib.t_dump(a); return dict(zip(FIELDS, list(a)))
def model_tables():
    return (bytes(lib.t_names()[:128]), bytes(lib.t_board()[:8]), bytes(lib.t_colour()[:16]), lib.t_get_rng(), bytes(lib.t_scores()[:192]))

def model_frame(v, slots=None):
    return bytes(lib.t_disp()[:32000])

def emu_frame(): return bytes(C.mem[0xE12000:0xE12000 + 32000])

def trial(script, max_passes, verbose=False, compare_frames=True, pokes=None, trace=None, scores=None):
    flow.setup(6)
    r = C.run(8_000_000); assert r == 1 and C.hit_pc() == O.LOOP_TOP
    if scores: C.load(0x077F00, scores)
    # sync: the model starts from the emulator's variables at the first main-loop pass
    v = emu_vars(); arr = (ctypes.c_ushort * len(FIELDS))(*[v[f] for f in FIELDS]); lib.t_load(arr)
    n, b, c, rng, hs = emu_tables()
    ctypes.memmove(lib.t_scores(), hs, 192)
    pal = bytes(C.mem[0xE19E00:0xE19E20]); lib.t_set_pal((ctypes.c_ushort * 16)(*[pal[2*i] | (pal[2*i+1] << 8) for i in range(16)]))
    ctypes.memmove(lib.t_names(), n, 128); ctypes.memmove(lib.t_board(), b, 8); ctypes.memmove(lib.t_colour(), c, 16)
    lib.t_set_rng(rng & 255, rng >> 8)
    last_frames = None
    k = 0; log = []; hist = []      # slot values after each pass: the sprite engine shows the slots as they were two passes earlier
    while k < max_passes:
        # compare state after the previous pass
        ev, mv = emu_vars(), model_vars()
        if trace: trace(k, ev, mv)
        bad = [f for f in FIELDS if ev[f] != mv[f] and not (f in ('trow', 'gx', 'gy', 'tcol') and False)]
        et, mt = emu_tables(), model_tables()
        if et[0] != mt[0]: bad.append('names')
        if et[1] != mt[1]: bad.append('board')
        if et[2] != mt[2]: bad.append('colour')
        if et[3] != mt[3]: bad.append('rng')
        if et[4] != mt[4]: bad.append('scores')
        if bytes(C.mem[0xE19E00:0xE19E20]) != bytes(lib.t_pal()[i] >> (8 * j) & 255 for i in range(16) for j in (0, 1)) and ev['f60'] == 0 and ev['f62'] == 0 and ev['f64'] == 0 and ev['f66'] == 0 and ev['f68'] == 0 and ev['f6e'] == 0 and ev['state'] != 0: bad.append('palette')
        if compare_frames and not bad and k > 0 and ev['state'] != 0 and lib.t_cur_pic() >= 0 and ev['f60'] == 0 and ev['f62'] == 0 and ev['f64'] == 0 and ev['f66'] == 0 and ev['f68'] == 0 and ev['f6e'] == 0:
            ef, mf = emu_frame(), model_frame(mv)
            if ef != mf: bad.append('frame'); last_frames = (ef, mf, hist[-3:])
        if bad:
            return dict(ok=False, pass_=k, bad=bad, frames=last_frames, emu={f: ev[f] for f in bad if f in ev}, model={f: mv[f] for f in bad if f in mv}, log=log)
        if ev['state'] == 0 and k > 3: return dict(ok=True, passes=k, left=True, log=log)
        ctl, key = script(k, ev)
        if pokes:
            for (a, val) in pokes(k, ev) or []:
                C.wr16(0x030000 + a, val)
                f = [f for f in FIELDS if ADDR.get(f) == a]
                if f:
                    mv = model_vars(); mv[f[0]] = val; arr = (ctypes.c_ushort * len(FIELDS))(*[mv[x] for x in FIELDS]); lib.t_load(arr)
        kb = (0x80 | ord(key)) if key else 0
        C.wr16(0x033058, ctl); C.wr16(0x03305C, 0x00FF); C.wr8(0xE0C010, kb); C.wr8(0xE0C000, kb)
        left = lib.t_pass(ctl, kb)
        mm = model_vars(); hist.append((mm['slot_x'], mm['slot_y'], mm['slot_f']))
        log.append((k, ctl, key, ev['state']))
        if left:
            return dict(ok=True, passes=k, left=True, log=log)
        rr = C.run(8_000_000)
        if rr == 2: raise SystemExit('fault at %06x' % C.fault_pc())
        if rr == 0 or C.hit_pc() != O.LOOP_TOP:
            return dict(ok=True, passes=k, left=True, log=log)
        k += 1
    return dict(ok=True, passes=k, log=log)

KEYS = list('abcdefghijklmnopqrstuvwxyz0123456789 -') + ['\x8d', '\x88', '\x7f', 'A', 'Q']   # \x8d Return, \x88/\x7f Delete; uppercase is ignored by the game
def keycode(ch):          # value of $E0C000 is 0x80 | ascii: the script gives the ascii
    return ch

def make_random(rng):
    hold = [0xFF, 0]
    def script(k, ev):
        st = ev['state']
        if k % 7 == 0 or rng.random() < 0.15:
            hold[0] = rng.choice([0xFF] * 4 + [0xFD, 0xFE, 0xFB, 0xF7, 0xF9, 0xFA, 0xF5, 0xF6])
        ctl = hold[0]
        fire = rng.random() < (0.25 if st != 6 else 0.02)
        if fire: ctl &= ~0x10
        key = None
        if st == 6 and rng.random() < 0.45:
            key = rng.choice(KEYS)
            if key == '\x7f': key = '\x7f'
        return ctl, key
    def pokes(k, ev):
        out = []
        if ev['state'] == 2 and rng.random() < 0.3:
            hs = [tuple(f for f in range(5))]
            tab = rd(39)[0x4628 - 0x1000:0x4628 - 0x1000 + 70]
            i = rng.randrange(14); x0, x1, y0, y1, _ = tab[i * 5:i * 5 + 5]
            x1 = min(x1, 0x95); y1 = min(y1, 0xB1)             # the controls never take the sign beyond x = $94, y = $B0
            if x1 > x0 and y1 > y0:
                out += [(0x4BC7, rng.randrange(x0, x1)), (0x4BC9, (rng.randrange(y0, y1) & ~1))]
        if rng.random() < 0.01:
            out.append((0x6C, rng.randint(1, 5)))             # an exit of the map reported while in the shop code
        if rng.random() < 0.005:
            out.append((0x70, 1))
        return out
    return script, pokes


def make_directed(rng):
    """goal-driven input: walk to the sign-up sign, type names, pick boards, edit and delete entries, look at the other screens"""
    tab = rd(39)[0x4628 - 0x1000:0x4628 - 0x1000 + 70]
    target = [rng.choice([0, 0, 0, 2, 3, 0])]
    st = dict(dwell=0, plan=None, typed=0, queue=None)
    def hot_pos(i):
        x0, x1, y0, y1, _ = tab[i * 5:i * 5 + 5]
        return (x0 + x1 - 1) // 2, ((y0 + min(y1, 0xB1)) // 2) & ~1
    idx = {0: 2, 2: 5, 3: 4}
    def script(k, ev):
        s = ev['state']; ctl = 0xFF; key = None
        fire = False
        if s == 2:
            if ev['c2'] == target[0] and ev['c6'] == 0 and rng.random() < 0.7: fire = True
        elif s in (3, 7):
            r = rng.random()
            if r < 0.35: ctl = rng.choice([0xFD, 0xFE])
            if rng.random() < 0.06: fire = True
            if ev['c6'] == 0 and rng.random() < 0.02: fire = True
        elif s == 6:
            if ev['ce'] == 0 and ev['f66'] == 0 and not st.get('queue') and st.get('last6') != ev['pass'] // 400 and rng.random() < 0.2 or (st.get('queue') is None):
                st['queue'] = []
            if not st.get('queue'):
                kind = rng.choice(['normal', 'normal', 'erase', 'erase', 'empty', 'spaces', 'long', 'random'])
                n = rng.randint(1, 9)
                letters = list('abcdefghijklmnopqrstuvwxyz0123456789 -')
                if kind == 'normal': q = [rng.choice(letters) for _ in range(n)] + ['\x8d']
                elif kind == 'erase': q = [rng.choice(letters) for _ in range(n)] + [rng.choice(['\x7f', '\x88']) for _ in range(n + rng.randint(0, 2))] + ['\x8d']
                elif kind == 'empty': q = ['\x8d']
                elif kind == 'spaces': q = [' ', ' ', 'a', ' ', 'b', '\x8d']
                elif kind == 'long': q = [rng.choice(letters[:26]) for _ in range(rng.randint(14, 18))] + ['\x7f'] + ['q'] + ['\x8d']
                else: q = [rng.choice(letters + ['A', 'Q', '\x7f', '\x88']) for _ in range(rng.randint(2, 25))] + ['\x8d']
                st['queue'] = q
            if rng.random() < 0.6 and st['queue']: key = st['queue'].pop(0)
        elif s in (4, 5):
            if rng.random() < 0.6: ctl = rng.choice([0xFB, 0xF7, 0xFD, 0xFE])
            if rng.random() < 0.02: fire = True
        elif s in (8, 9):
            if rng.random() < 0.1: fire = True
        if fire: ctl &= ~0x10
        return ctl, key
    def pokes(k, ev):
        out = []
        s = ev['state']
        if s == 2 and ev['c2'] != target[0]:
            x, y = hot_pos(idx[target[0]]); out += [(0x4BC7, x), (0x4BC9, y)]
        if s == 2 and ev['c2'] == target[0] and rng.random() < 0.1:
            target[0] = rng.choice([0, 0, 2, 3, 0])
        if s in (3, 6, 7) and rng.random() < 0.03:
            out.append((0x3038, (ev['pass'] | 0x7F) - rng.randint(0, 2)))
        if s == 6 and ev['ce'] == 0 and rng.random() < 0.05: out.append((0x3038, ev['pass']))
        return out
    return script, pokes

if __name__ == '__main__':
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    passes = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    directed = len(sys.argv) > 4 and sys.argv[4] == 'directed'
    total = 0; fails = 0; seen = set()
    for t in range(trials):
        rng = random.Random(seed * 1000 + t)
        script, pokes = (make_directed if directed else make_random)(rng)
        sc = bytearray()
        for e in range(12):
            nm = bytes(rng.choice(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-') for _ in range(rng.randint(0, 9)))
            ent = nm.ljust(10, b'\0') + bytes([rng.randrange(256) if rng.random() < 0.6 else 0 for _ in range(3)]) + b'\0\0' + bytes([rng.randint(1, 15)])
            assert len(ent) == 16
            sc += ent
        res = trial(script, passes, pokes=pokes, scores=bytes(sc))
        total += res.get('passes', res.get('pass_', 0))
        for e in res['log']: seen.add(e[3])
        if not res['ok']:
            fails += 1
            print('trial', t, 'MISMATCH at pass', res['pass_'], res['bad'], 'emu', res['emu'], 'model', res['model']); 
            print('   last inputs', res['log'][-5:])
            if fails >= 3: break
    print('trials', trials, 'passes', total, 'failures', fails, 'states seen', sorted(seen))
