#!/usr/bin/env python3
"""Run the original overlay on the oracle (tools/cpu816) and the translated C side by side and compare them.
usage: lockstep.py <cfg name> <mode> <passes> [--trace]
Compares, after the given number of main-loop passes: write log hash and count, all memory banks, registers; with --trace also the
instruction trace (first divergence is reported)."""
import sys, os, ctypes, json, subprocess, struct
import numpy as np
here = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/../../../tools/t816'); G = os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + '/..')
sys.path.insert(0, f'{here}/../cpu816'); sys.path.insert(0, here)
import cpu816 as C, event_sim as E, analyze

def build(cfgname, debug=True):
    cfg = analyze.load_cfg_fix(json.load(open(f'{G}/cfg/{cfgname}.json')))
    outc = f'/tmp/t816_{cfgname}.c'; lib = f'/tmp/lib{cfgname}{"_dbg" if debug else ""}.so'
    subprocess.check_call([sys.executable, f'{G}/tools/gen_ecs.py', f'{G}/cfg/{cfgname}.json', outc], stderr=subprocess.DEVNULL)
    flags = ['-O1', '-shared', '-fPIC', f'-I{G}/runtime', f'-I{here}/runtime', f'-I{G}/native', f'-I{here}/native', f'-I{G}/src'] + (['-DT816_B2DIRTY'] if cfgname in C2P_SCROLL_CFGS else []) + (['-DT816_DEBUG'] if debug else []) + (['-DT816_FAST'] if os.environ.get('T816_FAST') else [])
    srcs = [outc, f'{G}/runtime/m816rt.c'] + ([f'{G}/tools/c2p_host.c'] if cfgname in C2P_SCROLL_CFGS else [])
    subprocess.check_call(['gcc'] + flags + ['-o', lib] + srcs)
    return cfg, lib

BANKS = [0, 1, 2, 3, 4, 5, 7, 0xE0, 0xE1]
C2P_SCROLL_CFGS = ('ev21', 'ev15')       # events whose picture conversion works from the scroll (c2p.h, C2P_SCROLL): the planar picture is checked at every pass

WIN = (0x3400, 0x9D00)     # the scrolled window of banks 1 and E1: not maintained in the events of C2P_SCROLL_CFGS (the platform converts bank 2 directly), so never compared
def masked(b, data, masked_on):
    if masked_on and b in (1, 0xE1):
        data = bytearray(data); data[WIN[0]:WIN[1]] = bytes(WIN[1] - WIN[0]); data = bytes(data)
    return data

def c2p_reference(e1):
    """the planar picture (4 planes of 40 bytes per row, rows of 160 bytes) of the 200 rows at E1:$2000"""
    a = np.frombuffer(e1, dtype=np.uint8).reshape(200, 80, 2)           # 160 bytes a row, two pixels a byte
    a = a.reshape(200, 160)
    px = np.empty((200, 320), dtype=np.uint8); px[:, 0::2] = a >> 4; px[:, 1::2] = a & 15
    out = np.zeros((200, 4, 40), dtype=np.uint8)
    for pl in range(4):
        bits = ((px >> pl) & 1).reshape(200, 40, 8)
        out[:, pl, :] = np.packbits(bits, axis=2).reshape(200, 40)
    return out.reshape(200, 160)

class IOS(ctypes.Structure):
    _fields_ = [('key', ctypes.c_uint8), ('key_waiting', ctypes.c_uint8), ('anykey', ctypes.c_uint8), ('mods', ctypes.c_uint8), ('enabled', ctypes.c_uint8),
                ('btn', ctypes.c_uint8 * 3), ('pdl_len', ctypes.c_uint16 * 4), ('pdl_left', ctypes.c_uint16 * 4), ('ramrd', ctypes.c_uint8), ('ramwrt', ctypes.c_uint8)]

PDL = {'lo': 0, 'mid': 60, 'hi': 300}
def apply_input(io, inp):
    """inp = (joy_x, joy_y, fire, key) with joy -1/0/1"""
    jx, jy, fire, key = inp
    io.pdl_len[0] = PDL['lo'] if jx < 0 else PDL['hi'] if jx > 0 else PDL['mid']
    io.pdl_len[1] = PDL['lo'] if jy < 0 else PDL['hi'] if jy > 0 else PDL['mid']
    io.btn[0] = 0x80 if fire else 0
    if key: io.key = key; io.key_waiting = 1; io.anykey = 1
    else: io.anykey = 0

def make_script(kind, seed):
    import random
    rng = random.Random(seed)
    st = dict(jx=0, jy=0, fire=0, hold_j=0, hold_f=0)
    keys = [ord(c) for c in 'abcdefghijklmnopqrstuvwxyz0123456789'] + [0x20, 0x0D, 0x08, 0x7F] + ([] if os.environ.get('T816_NOESC') else [0x1B])
    def script(k):
        if kind == 'idle': return (0, 0, 0, 0)
        if kind == 'fire': return (0, 0, 1, 0)
        if kind.startswith('pump'):        # full effort: the stick alternates left/right every pass (pump1) or every 2 / 3 passes, the button held or pulsed
            n = int(kind[4]) if len(kind) > 4 and kind[4].isdigit() else 1
            return (1 if (k // n) % 2 else -1, 0, 1 if kind.endswith('b') else (1 if k % 4 < 2 else 0), 0)
        if kind == 'waggle':          # fast left/right alternation (running), fire pulses of random length, now and then up/down
            if st['hold_j'] <= 0:
                st['jx'] = -st['jx'] if st['jx'] else 1; st['hold_j'] = rng.choice([1, 1, 2, 3, 6])
                st['jy'] = rng.choice([0, 0, 0, 0, -1, 1])
            st['hold_j'] -= 1
            if st['hold_f'] <= 0:
                st['fire'] ^= 1; st['hold_f'] = rng.choice([1, 2, 4, 8, 16, 40]) if st['fire'] else rng.choice([1, 3, 10, 30, 80])
            st['hold_f'] -= 1
            return (st['jx'], st['jy'], st['fire'], 0)
        if st['hold_j'] <= 0:
            st['jx'] = rng.choice([-1, 0, 0, 1]); st['jy'] = rng.choice([-1, 0, 0, 1]); st['hold_j'] = int(rng.expovariate(1 / 12.0)) + 1
        st['hold_j'] -= 1
        if st['hold_f'] <= 0:
            st['fire'] ^= 1; st['hold_f'] = int(rng.expovariate(1 / (6.0 if st['fire'] else 20.0))) + 1
        st['hold_f'] -= 1
        key = rng.choice(keys) if rng.random() < 0.02 else 0
        return (st['jx'], st['jy'], st['fire'], key)
    return script

def run(cfgname, mode, passes, trace=False, script=None):
    cfg, libp = build(cfgname)
    rt = ctypes.CDLL(libp)
    rt.rt_bank_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 65536); rt.rt_cpu.restype = ctypes.c_void_p; rt.rt_io_ptr.restype = ctypes.c_void_p; rt.rt_dmin_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 200); rt.rt_dmax_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * 200)
    for fn in ('rt_get_trace_n', 'rt_get_wr_n', 'rt_get_wr_hash'): getattr(rt, fn).restype = ctypes.c_uint64
    rt.rt_set_trace.argtypes = [ctypes.c_void_p, ctypes.c_uint64]; rt.rt_set_wlog.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
    entry = cfg['entry']; hook_pc = 0x030000 | int(cfg['pass_hook'], 0); hooks = [hook_pc] + [0x030000 | int(h, 0) for h in cfg.get('extra_pass_hooks', [])]
    script = script or make_script('idle', 0)
    _tab = [script(k) for k in range(passes + 3)]
    script = lambda k: _tab[k]
    C.io.enabled = 1
    for k in range(4): C.io.pdl_left[k] = 0
    # --- oracle ---
    E.load_mode(mode)
    snapshot = {b: bytes(C.mem[b << 16:(b << 16) + 65536]) for b in BANKS}
    cap = int(os.environ.get("T816_CAP", 4_000_000))
    otr = np.zeros((cap, 4), dtype=np.uint32) if trace else None
    step_k = 0; cam_cap = 0; cam_lead = 0
    if cfgname == 'ev21':            # the jam's position step (native/dh21_consts.h N_STEP_K) is a deliberate change: the oracle gets the same one at $25B9
        import re
        consts = open(f'{G}/native/dh21_consts.h').read()
        m = re.search(r'#define N_STEP_K (\d+)', consts); step_k = int(m.group(1)) if m else 0
        m = re.search(r'#define N_CAM_CAP (\d+)', consts); cam_cap = int(m.group(1)) if m else 0
        m = re.search(r'#define N_CAM_LEAD (\d+)', consts); cam_lead = int(m.group(1)) if m else 0
    [C.hook(h) for h in hooks]; C.hook(0x077E08)
    if step_k: C.hook(0x0325B9)
    if cam_cap: C.hook(0x0326F6)
    regs0 = (C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s, C.cpu.d, C.cpu.db, C.cpu.p)
    wl_cap = int(os.environ.get('T816_WL', 0))
    owl = np.zeros((wl_cap, 2), dtype=np.uint32) if wl_cap else None
    C.wlog_start(owl.ctypes.data if wl_cap else None, wl_cap)
    if trace: C.trace_start(otr.ctypes.data, cap)
    msk = cfgname in C2P_SCROLL_CFGS; o_pics = []
    hits = 0; ohash = []; orig_io = None; FAST = bool(os.environ.get('T816_FAST')); ostate = []
    import zlib
    apply_input(C.io, script(0))
    while True:
        r = C.run(50_000_000)
        if r == 2: raise SystemExit('oracle fault at %06x' % C.fault_pc())
        if r == 0: print('oracle: instruction limit'); break
        if cam_cap and C.hit_pc() == 0x0326F6:    # the camera's step with the larger cap, then the RTS at $2719
            target = C.rd16(0x03646C); lead = max(C.rd16(0x036464), C.rd16(0x036466))
            if cam_lead and lead >= cam_lead and ((lead - cam_lead) & 0xFFFF) > target: target = (lead - cam_lead) & 0xFFFF
            diff = (target - C.rd16(0x036538)) & 0xFFFF
            step = 0 if diff & 0x8000 else min(diff, cam_cap)
            cam = (C.rd16(0x036538) + step) & 0xFFFF
            C.wr16(0x036446, step); C.wr16(0x036538, cam); C.cpu.a = cam; C.cpu.pc = 0x2719
            C.cpu.p = (C.cpu.p & ~(0x80 | 0x40 | 0x02 | 0x01)) | (0x80 if cam & 0x8000 else 0) | (0x02 if cam == 0 else 0)
            continue
        if step_k and C.hit_pc() == 0x0325B9:     # INC $6464,X; LDA #0; STA $60FA,X; RTS with the position moving step_k rows
            x = C.cpu.x; C.wr16(0x036464 + x, (C.rd16(0x036464 + x) + step_k) & 0xFFFF); C.wr16(0x0360FA + x, 0)
            sp = C.cpu.s; ret = C.mem[(sp + 1) & 0xFFFF] | (C.mem[(sp + 2) & 0xFFFF] << 8)
            C.cpu.s = (sp + 2) & 0xFFFF; C.cpu.pc = (ret + 1) & 0xFFFF; C.cpu.a = 0; C.cpu.p = (C.cpu.p & ~0x80) | 0x02
            continue
        if C.hit_pc() not in hooks: print('oracle: event left the overlay at', hex(C.hit_pc()), 'after', hits, 'passes'); break
        hits += 1
        ohash.append((C.wlog_hash(), C.wlog_n()))
        if FAST: ostate.append((zlib.crc32(b''.join(masked(b, bytes(C.mem[b << 16:(b << 16) + 65536]), msk) for b in BANKS)), C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s, C.cpu.p & 0xC3))
        if msk: o_pics.append(bytes(C.mem[0xE12000:0xE12000 + 32000]))
        if hits > passes: break
        apply_input(C.io, script(hits))
        hp = C.hit_pc(); C.hook(hp, False); C.run(1); C.hook(hp, True)      # step off the hook
    # note: the oracle stops *before* executing the hook instruction for the (passes+1)-th time; the translated code stops at the same point
    o_hash, o_n = C.wlog_hash(), C.wlog_n()
    o_trn = C.trace_n()
    # --- translated ---
    rt.rt_init()
    for b in BANKS: ctypes.memmove(rt.rt_bank_ptr(b), snapshot[b], 65536)
    class Cpu(ctypes.Structure): _fields_ = [('a', ctypes.c_uint16), ('x', ctypes.c_uint16), ('y', ctypes.c_uint16), ('s', ctypes.c_uint16), ('d', ctypes.c_uint16),
                                              ('db', ctypes.c_uint8), ('n', ctypes.c_uint8), ('v', ctypes.c_uint8), ('z', ctypes.c_uint8), ('c', ctypes.c_uint8), ('i', ctypes.c_uint8)]
    cp = Cpu.from_address(rt.rt_cpu())
    a, x, y, s, d, db, p = regs0
    cp.a, cp.x, cp.y, cp.s, cp.d, cp.db = a, x, y, s, d, db
    cp.n, cp.v, cp.z, cp.c, cp.i = (p >> 7) & 1, (p >> 6) & 1, (p >> 1) & 1, p & 1, (p >> 2) & 1
    ttr = np.zeros((cap, 4), dtype=np.uint32) if trace else None
    rt.rt_set_trace(ttr.ctypes.data if trace else None, cap if trace else 0)
    twl = np.zeros((wl_cap, 2), dtype=np.uint32) if wl_cap else None
    rt.rt_set_wlog(twl.ctypes.data if wl_cap else None, wl_cap)
    cnt = [0]; thash = []; tstate = []
    tio = IOS.from_address(rt.rt_io_ptr()); tio.enabled = 1; tio.key = 0; tio.key_waiting = 0; tio.anykey = 0; tio.mods = 0
    for k in range(3): tio.btn[k] = 0
    for k in range(4): tio.pdl_left[k] = 0
    apply_input(tio, script(0))
    CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint32)
    snap = [bytes(rt.rt_bank_ptr(0xE1).contents[0x2000:0x9D00])]
    probe = []
    dirty_bad = [0]; scroll_cfg = cfgname in C2P_SCROLL_CFGS; c2p_bad = [0, None]
    if scroll_cfg:
        rt.c2p_host_pass.restype = ctypes.POINTER(ctypes.c_uint8 * 32000); rt.c2p_host_init()
    def cb(pc):
        cnt[0] += 1
        cur = bytes(rt.rt_bank_ptr(0xE1).contents[0x2000:0x9D00]); dmin = rt.rt_dmin_ptr().contents; dmax = rt.rt_dmax_ptr().contents
        if scroll_cfg:       # the platform's planar picture (spans, scroll shifts) against a conversion of the whole picture
            got = np.frombuffer(bytes(rt.c2p_host_pass().contents), dtype=np.uint8).reshape(200, 160)
            bad = np.nonzero((got != c2p_reference(o_pics[cnt[0] - 1] if cnt[0] - 1 < len(o_pics) else cur)).any(axis=1))[0]
            if len(bad):
                c2p_bad[0] += 1
                if c2p_bad[1] is None: c2p_bad[1] = (cnt[0], bad[:8].tolist())
            dirty_bad[0] += 0; snap[0] = cur
            thash.append((rt.rt_get_wr_hash(), rt.rt_get_wr_n()))
            if os.environ.get('T816_JAMPROBE'):
                b3 = rt.rt_bank_ptr(3).contents; probe.append(tuple(b3[a] | (b3[a + 1] << 8) for a in (0x6464, 0x6466, 0x6538, 0x646C)))
            if FAST: tstate.append((zlib.crc32(b''.join(masked(b, bytes(rt.rt_bank_ptr(b).contents), True) for b in BANKS)), cp.a, cp.x, cp.y, cp.s, (cp.n << 7) | (cp.v << 6) | (cp.z << 1) | cp.c))
            if cnt[0] > passes: return 1
            apply_input(tio, script(cnt[0]))
            return 0
        for row in range(200):
            a, b = cur[row*160:row*160+160], snap[0][row*160:row*160+160]
            if a != b:
                for j in range(160):
                    if a[j] != b[j] and not (dmin[row] <= j // 4 <= dmax[row]): dirty_bad[0] += 1; break
            dmin[row] = 255; dmax[row] = 0
        snap[0] = cur
        thash.append((rt.rt_get_wr_hash(), rt.rt_get_wr_n()))
        if FAST: tstate.append((zlib.crc32(b''.join(bytes(rt.rt_bank_ptr(b).contents) for b in BANKS)), cp.a, cp.x, cp.y, cp.s, (cp.n << 7) | (cp.v << 6) | (cp.z << 1) | cp.c))
        if cnt[0] > passes: return 1
        apply_input(tio, script(cnt[0]))
        return 0
    cbo = CB(cb); rt.rt_set_pass_cb(cbo)
    rc = rt.rt_run(entry)
    if rc == 3: print('translated code: stopped by the harness after %d passes' % cnt[0])
    if rc == 1: print('translated code: event left the overlay to %06x after %d passes' % (ctypes.c_uint32.in_dll(rt, 'rt_err_got').value, cnt[0]))
    if rc == 2: print('translated code stopped on an unexpected transfer: pc=%06x got=%06x' % (ctypes.c_uint32.in_dll(rt, 'rt_err_pc').value, ctypes.c_uint32.in_dll(rt, 'rt_err_got').value))
    t_hash, t_n = rt.rt_get_wr_hash(), rt.rt_get_wr_n()
    t_trn = rt.rt_get_trace_n()
    print('passes %d: oracle writes %d hash %016x | translated writes %d hash %016x' % (passes, o_n, o_hash, t_n, t_hash))
    ok = (o_hash == t_hash and o_n == t_n)
    if FAST:
        ok = True
        print('fast mode: native routines in use; comparing the state (all banks, A X Y S, flags) at every pass instead of the write log')
        for k in range(min(len(ostate), len(tstate))):
            if ostate[k] != tstate[k]:
                print('first pass whose state differs: %d (oracle %s translated %s)' % (k, ostate[k], tstate[k])); ok = False; break
        if len(ostate) != len(tstate) - 0 and abs(len(ostate) - len(tstate)) > 1: print('different number of passes', len(ostate), len(tstate)); ok = False
    else:
      for k in range(min(len(ohash), len(thash))):
        if ohash[k] != thash[k]:
            print('first pass whose write log differs: %d (oracle n=%d, translated n=%d)' % (k, ohash[k][1], thash[k][1])); ok = False; break
    bad = []
    for b in BANKS:
        om = masked(b, bytes(C.mem[b << 16:(b << 16) + 65536]), msk); tm = masked(b, bytes(rt.rt_bank_ptr(b).contents), msk)
        if om != tm:
            diff = [i for i in range(65536) if om[i] != tm[i]]
            bad.append((b, len(diff), diff[:6])); ok = False
            print('  bank %02x differences (oracle/translated):' % b, [(hex(i), om[i], tm[i]) for i in diff[:6]])
    print('memory banks differing:', [(hex(b), n, [hex(x) for x in d]) for b, n, d in bad])
    print('regs oracle  A=%04x X=%04x Y=%04x S=%04x' % (C.cpu.a, C.cpu.x, C.cpu.y, C.cpu.s), ' translated A=%04x X=%04x Y=%04x S=%04x' % (cp.a, cp.x, cp.y, cp.s))
    if trace:
        o = otr[:min(o_trn, cap)]; t = ttr[:min(t_trn, cap)]
        o = o[(o[:, 0] >> 16) != 7]            # the oracle executes RTL stubs in bank 7 that the translation does not
        n = min(len(o), len(t))
        ne = np.nonzero((o[:n] != t[:n]).any(axis=1))[0]
        print('trace records: oracle %d (without bank 7), translated %d; first divergence at record %s' % (len(o), len(t), ne[0] if len(ne) else 'none'))
        if len(ne):
            k = ne[0]
            for j in range(max(0, k - 3), min(n, k + 2)):
                print('  #%d oracle  pc=%06x a=%04x x=%04x y=%04x s=%04x d=%04x db=%02x p=%02x' % (j, o[j][0], o[j][1] & 0xFFFF, o[j][1] >> 16, o[j][2] & 0xFFFF, o[j][2] >> 16, o[j][3] & 0xFFFF, (o[j][3] >> 16) & 255, o[j][3] >> 24))
                print('  #%d transl. pc=%06x a=%04x x=%04x y=%04x s=%04x d=%04x db=%02x p=%02x' % (j, t[j][0], t[j][1] & 0xFFFF, t[j][1] >> 16, t[j][2] & 0xFFFF, t[j][2] >> 16, t[j][3] & 0xFFFF, (t[j][3] >> 16) & 255, t[j][3] >> 24))
            ok = False
    if wl_cap:
        n = min(o_n, t_n, wl_cap)
        ne = np.nonzero((owl[:n] != twl[:n]).any(axis=1))[0]
        if len(ne):
            k = ne[0]
            print('first differing write #%d: oracle pc=%06x addr=%06x val=%02x | translated pc=%06x addr=%06x val=%02x' % (k, owl[k][0], owl[k][1] >> 8, owl[k][1] & 255, twl[k][0], twl[k][1] >> 8, twl[k][1] & 255))
            for j in range(max(0, k - 3), k): print('   before: pc=%06x addr=%06x val=%02x' % (owl[j][0], owl[j][1] >> 8, owl[j][1] & 255))
    if scroll_cfg: print('passes whose planar picture differs from a conversion of the whole picture: %d%s' % (c2p_bad[0], '' if c2p_bad[1] is None else ' (first: pass %d, rows %s)' % c2p_bad[1])); ok = ok and c2p_bad[0] == 0
    elif cnt[0]: print('pixels changed outside the marked span: %d' % dirty_bad[0]); ok = ok and dirty_bad[0] == 0
    if probe:
        import numpy as _np
        pr = _np.array(probe, dtype=_np.int64); d = _np.diff(pr, axis=0)
        okd = (d[:, 0] >= 0) & (d[:, 0] < 40); okd2 = (d[:, 1] >= 0) & (d[:, 1] < 40); okc = (d[:, 2] >= 0) & (d[:, 2] < 40)
        print('jam probe: max skater0 step %d, skater1 step %d, camera step %d per pass; max (leader - camera) %d, max (camera - trailer) %d; camera target - camera: max %d' % (d[okd, 0].max(), d[okd2, 1].max(), d[okc, 2].max(), (_np.maximum(pr[:, 0], pr[:, 1]) - pr[:, 2]).max(), (pr[:, 2] - _np.minimum(pr[:, 0], pr[:, 1])).max(), (pr[:, 3] - pr[:, 2]).max()))
    print('RESULT:', 'identical' if ok else 'DIFFERENT')
    return ok

if __name__ == '__main__':
    name = sys.argv[1]; mode = int(sys.argv[2]); passes = int(sys.argv[3])
    kind = os.environ.get('T816_KIND') or ('random' if any(a.startswith('--random') for a in sys.argv) else 'idle')
    seed = int([a for a in sys.argv if a.startswith('--random=')][0].split('=')[1]) if any(a.startswith('--random=') for a in sys.argv) else 0
    run(name, mode, passes, trace='--trace' in sys.argv, script=make_script(kind, seed))
