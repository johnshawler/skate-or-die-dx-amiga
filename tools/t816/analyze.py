"""Static analysis for the 65816 -> C translator: reachable code, M/X/D/DB tracking, function discovery.

The code is hand-written assembly, so the analysis is a flow-following one with a small abstract state per instruction:
  m, x   register widths (1 = 8-bit)           -- changed by REP/SEP/PLP
  d      decimal flag (0/1)                    -- SED/CLD/PLP
  db     data bank register if known           -- PLB with a known pushed value, MVN/MVP
  dk     direct page register if known         -- PLD/TCD with a known value
  stack  abstract bytes pushed in this function (None = unknown), for PHP/PLP, PHB/PLB, PHD/PLD pairs
  a      accumulator if it is a known constant (for LDA #imm / PHA / PLB patterns)
Calls are assumed to be balanced; the state after a JSR is the callee's exit state (one exit state per function is required).
Anything the analysis cannot decide is reported, never guessed."""
import sys
from collections import namedtuple
from isa import decode, Insn

class St:
    __slots__ = ('m', 'x', 'd', 'db', 'dk', 'stack', 'a', 'alo')
    def __init__(self, m=0, x=0, d=0, db=None, dk=None, stack=(), a=None, alo=None):
        self.m, self.x, self.d, self.db, self.dk, self.stack, self.a, self.alo = m, x, d, db, dk, stack, a, alo
    def key(self): return (self.m, self.x, self.d, self.db, self.dk)
    def copy(self): return St(self.m, self.x, self.d, self.db, self.dk, self.stack, self.a, self.alo)
    def __repr__(self): return 'St(m%d x%d d%d db=%s dk=%s)' % (self.m, self.x, self.d, self.db, self.dk)

PRESERVE_A = {'STA', 'STX', 'STY', 'STZ', 'LDX', 'LDY', 'CMP', 'CPX', 'CPY', 'BIT', 'INX', 'INY', 'DEX', 'DEY', 'TAX', 'TAY', 'TXY', 'TYX',
              'TSX', 'TXS', 'PHA', 'PHX', 'PHY', 'PHB', 'PHD', 'PHK', 'PHP', 'PEA', 'PEI', 'PER', 'NOP', 'REP', 'SEP', 'PLB', 'PLD', 'PLP',
              'PLX', 'PLY', 'CLC', 'SEC', 'CLI', 'SEI', 'CLD', 'SED', 'CLV', 'TCD', 'TCS', 'WDM'}
BRANCHES = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'}

class Analysis:
    def __init__(self, data, base, bank, cfg):
        self.data, self.base, self.bank, self.cfg = data, base, bank, cfg
        self.insn = {}          # pc -> Insn (with .m .x .d .db .dk = state on entry)
        self.succ = {}          # pc -> list of ('fall'|'br'|'jmp'|'call', target)
        self.entries = {}       # function entry -> entry state
        self.summary = {}       # (entry, key) -> exit St or None while in progress
        self.warn = []
        self.dataspans = []     # (start, end) inline data skipped
        self.continuations = set()
        self.exits = {}         # pc of RTS/RTL -> St
        self.calls = {}         # pc of JSR/JSL -> (target24, callee_key)
        self.outkey = {}
        self.dispatch = {}      # pc of computed JMP -> list of handler entries
        self.patched_jmps = dict(cfg.get('patched_jmp', {}))   # pc of JMP abs -> address of the word holding its (self-modified) operand
        self.inline = cfg.get('inline', {})                    # callee -> (n_bytes_before_string, 'z') inline-data layout after the JSR

    def w(self, msg): self.warn.append(msg)

    # ---- abstract transfer ----
    def push(self, st, cells): st.stack = None if st.stack is None else st.stack + tuple(cells)
    def pop(self, st, n):
        if st.stack is None: return [None] * n
        out = []
        s = list(st.stack)
        for _ in range(n):
            out.append(s.pop() if s else None)
        st.stack = tuple(s)
        return out

    def step(self, i, st):
        """update st (a copy) for instruction i; returns nothing"""
        mn = i.mn; v = i.val
        if mn == 'REP':
            if v & 0x20: st.m = 0
            if v & 0x10: st.x = 0
            if v & 0x08: st.d = 0
        elif mn == 'SEP':
            if v & 0x20: st.m = 1
            if v & 0x10: st.x = 1
            if v & 0x08: st.d = 1
        elif mn == 'CLD': st.d = 0
        elif mn == 'SED': st.d = 1
        elif mn == 'LDA' and i.mode == 'immM':
            if st.m == 0: st.a = v; st.alo = v & 255
            else: st.a = ((st.a & 0xFF00) | v) if st.a is not None else None; st.alo = v
        elif mn == 'PHA':
            if st.m: self.push(st, [st.alo])
            else: self.push(st, [None if st.a is None else st.a >> 8, st.alo])
        elif mn in ('PHX', 'PHY'):
            self.push(st, [None] * (1 if st.x else 2))
        elif mn == 'PHB': self.push(st, [st.db])
        elif mn == 'PHK': self.push(st, [self.bank])
        elif mn == 'PHD': self.push(st, [None if st.dk is None else st.dk >> 8, None if st.dk is None else st.dk & 255])
        elif mn == 'PHP': self.push(st, [('P', st.m, st.x, st.d)])
        elif mn == 'PEA': self.push(st, [v >> 8, v & 255])
        elif mn in ('PEI', 'PER'): self.push(st, [None, None])
        elif mn == 'PLA': self.pop(st, 1 if st.m else 2)
        elif mn in ('PLX', 'PLY'): self.pop(st, 1 if st.x else 2)
        elif mn == 'PLB':
            c = self.pop(st, 1)[0]
            st.db = c if isinstance(c, int) else None
        elif mn == 'PLD':
            lo, hi = self.pop(st, 1)[0], self.pop(st, 1)[0]
            st.dk = (lo | hi << 8) if isinstance(lo, int) and isinstance(hi, int) else None
        elif mn == 'PLP':
            c = self.pop(st, 1)[0]
            if isinstance(c, tuple): st.m, st.x, st.d = c[1], c[2], c[3]
            elif isinstance(c, int): st.m, st.x, st.d = (c >> 5) & 1, (c >> 4) & 1, (c >> 3) & 1
            else: self.w('PLP at %04X with unknown stacked flags' % i.pc); st.m = st.x = None
        elif mn == 'TCD': st.dk = st.a
        elif mn == 'TDC': st.a = st.dk; st.alo = None if st.dk is None else st.dk & 255
        elif mn in ('TCS', 'TXS', 'TSC', 'TSX'):
            st.stack = None
        elif mn in ('MVN', 'MVP'):
            st.db = i.val >> 8       # operand bytes are dst,src (the table decodes them little-endian: first byte = dst)
            st.db = i.val & 255
        if mn not in PRESERVE_A and not (mn == 'LDA' and i.mode == 'immM'):
            st.a = None; st.alo = None

    # ---- exploration ----
    def explore_function(self, entry, st0):
        """explore code reachable from entry (not following calls); returns the unified exit state (or None if there is no RTS)"""
        k = (entry, st0.key())
        if k in self.summary: return self.summary[k]
        self.summary[k] = st0.copy()          # recursion guard: assume balanced
        exits = []
        self.entries.setdefault(entry, st0)
        work = [(entry, st0.copy())]
        while work:
            pc, st = work.pop()
            while True:
                if st.m is None or st.x is None: self.w('unknown m/x at %04X' % pc); break
                if pc in self.cfg.get('dk_dynamic', ()) and st.dk is not None:       # the direct page register differs between the paths into here
                    st = st.copy(); st.dk = None
                old = self.insn.get(pc)
                if old is not None:
                    if (old.m, old.x, old.d, old.db, old.dk) != st.key():
                        if old.mn in ('REP', 'SEP'):                # the outgoing state of REP/SEP may not depend on the incoming one
                            t = st.copy(); self.step(old, t)
                            if t.key() == self.outkey.get(pc): break
                        self.w('state conflict at %04X: have m%s x%s d%s db%s dk%s, arriving %s' % (pc, old.m, old.x, old.d, old.db, old.dk, st))
                    break
                i = decode(self.data, self.base, pc, st.m, st.x)
                if i is None: self.w('cannot decode at %04X (from function %04X)' % (pc, entry)); break
                i.m, i.x, i.d, i.db, i.dk = st.m, st.x, st.d, st.db, st.dk
                self.insn[pc] = i
                nst = st.copy()
                mn = i.mn
                succ = []
                self.step(i, nst)
                self.outkey[pc] = nst.key()
                if mn in BRANCHES or mn == 'BRA':
                    t = (pc + 2 + (i.val - 256 if i.val > 127 else i.val)) & 0xFFFF
                    succ.append(('br', t)); work.append((t, nst.copy()))
                    if mn == 'BRA': self.succ[pc] = succ; break
                elif mn == 'BRL':
                    t = (pc + 3 + (i.val - 65536 if i.val > 32767 else i.val)) & 0xFFFF
                    succ.append(('jmp', t)); self.succ[pc] = succ; work.append((t, nst.copy())); break
                elif mn == 'JMP' and i.mode == 'abs':
                    if pc in self.patched_jmps:
                        self.succ[pc] = [('dispatch', self.patched_jmps[pc])]
                        tabs = self.cfg.get('dispatch_table', {}).get(pc)
                        tg = list(self.cfg.get('dispatch_targets', {}).get(pc, []))
                        if tabs:
                            ta, cnt = tabs
                            tg += [self.data[ta - self.base + 2 * k] | self.data[ta - self.base + 2 * k + 1] << 8 for k in range(cnt)]
                        self.dispatch.setdefault(pc, [])
                        for t in tg:
                            if t not in self.dispatch[pc]: self.dispatch[pc].append(t)
                            ex = self.callfn(t, nst, pc, tail=True)
                        break
                    succ.append(('jmp', i.val)); self.succ[pc] = succ; work.append((i.val, nst.copy())); break
                elif mn == 'JML' and i.mode == 'long':
                    if (i.val >> 16) == self.bank:
                        succ.append(('jmp', i.val & 0xFFFF)); self.succ[pc] = succ; work.append((i.val & 0xFFFF, nst.copy())); break
                    self.succ[pc] = [('xjmp', i.val)]; self.w('JML out of the overlay at %04X -> %06X' % (pc, i.val)); break
                elif mn in ('JMP', 'JML'):
                    self.succ[pc] = [('indirect', None)]; self.w('indirect jump at %04X' % pc); break
                elif mn == 'RTS' or mn == 'RTL':
                    self.exits[pc] = st.copy(); exits.append(st.copy()); self.succ[pc] = [('ret', None)]; break
                elif mn == 'RTI' or mn == 'STP' or mn == 'WAI':
                    self.succ[pc] = [('halt', None)]; break
                elif mn == 'JSR' and i.mode == 'abs' and pc in self.cfg.get('patched_jsr', {}):
                    ta, cnt = self.cfg['dispatch_table'][pc]
                    tg = [self.data[ta - self.base + 2 * k] | self.data[ta - self.base + 2 * k + 1] << 8 for k in range(cnt)]
                    self.dispatch[pc] = tg
                    self.succ[pc] = [('pcall', None)]
                    for t in tg:
                        ex = self.callfn(t, nst, pc)
                        if ex is not None: nst.m, nst.x, nst.d, nst.db, nst.dk = ex.m, ex.x, ex.d, ex.db, ex.dk
                    nst.a = None; nst.alo = None
                    self.succ[pc].append(('fall', i.nxt))
                    pc = i.nxt; st = nst
                    continue
                elif mn in ('JSR', 'JSL'):
                    if mn == 'JSR' and i.mode == 'abs': t24 = (self.bank << 16) | i.val
                    elif mn == 'JSL': t24 = i.val
                    else: t24 = None
                    cont = i.nxt
                    if t24 is None:
                        self.w('indirect call at %04X' % pc); succ.append(('call', None)); self.calls[pc] = (None, None)
                    else:
                        tgt = t24 & 0xFFFF
                        inl = self.inline.get(t24)
                        if (t24 >> 16) == self.bank and tgt not in self.cfg.get('replace', {}) and tgt in range(self.base, self.base + len(self.data)):
                            ex = self.callfn(tgt, nst, pc)
                            if ex is not None:
                                nst.m, nst.x, nst.d, nst.db, nst.dk = ex.m, ex.x, ex.d, ex.db, ex.dk
                        else:
                            self.calls[pc] = (t24, None)
                        if inl is not None:
                            nbytes, kind = inl
                            p = cont + nbytes
                            while self.data[p - self.base] != 0: p += 1
                            p += 1
                            self.dataspans.append((cont, p)); self.continuations.add(p)
                            succ.append(('call', t24)); succ.append(('inline_cont', p))
                            self.succ[pc] = succ
                            cont = p
                        else:
                            succ.append(('call', t24))
                    if pc not in self.succ: self.succ[pc] = succ + [('fall', cont)]
                    elif ('fall', cont) not in self.succ[pc]: self.succ[pc].append(('fall', cont))
                    nst.a = None; nst.alo = None
                    pc = cont; st = nst
                    continue
                if pc not in self.succ: self.succ[pc] = succ + [('fall', i.nxt)]
                pc = i.nxt; st = nst
        # unify exits
        res = None
        for e in exits:
            if res is None: res = e.copy()
            elif res.key() != e.key():
                self.w('function %04X has several exit states: %s vs %s' % (entry, res, e))
        self.summary[k] = res
        return res

    def callfn(self, tgt, st, from_pc, tail=False):
        if tgt not in range(self.base, self.base + len(self.data)): return None
        est = St(st.m, st.x, st.d, st.db, st.dk, (), None)
        self.calls[from_pc] = ((self.bank << 16) | tgt, est.key())
        return self.explore_function(tgt, est)

def _num(q):
    if isinstance(q, str):
        try: return int(q, 0)
        except ValueError: return q
    return q

def load_cfg_fix(cfg):
    if 'dk_dynamic' in cfg: cfg['dk_dynamic'] = [_num(x) for x in cfg['dk_dynamic']]
    for k in ('inline', 'patched_jmp', 'patched_jsr', 'replace', 'dispatch_table', 'dispatch_targets'):
        if k in cfg:
            cfg[k] = {_num(a): (tuple(_num(q) for q in v) if isinstance(v, list) else _num(v)) for a, v in cfg[k].items()}
    return cfg

def analyze(data, base, bank, cfg):
    a = Analysis(data, base, bank, cfg)
    ent = cfg['entry']
    a.explore_function(ent, St(cfg.get('m', 0), cfg.get('x', 0), 0, cfg.get('db', 0), cfg.get('dk', 0), (), None))
    for e in cfg.get('extra_entries', []):
        a.explore_function(e, St(0, 0, 0, 3, 0, (), None))
    return a

if __name__ == '__main__':
    import json
    cfgpath = sys.argv[1]
    cfg = json.load(open(cfgpath))
    cfg = {k: v for k, v in cfg.items()}
    load_cfg_fix(cfg)
    data = open(cfg['file'], 'rb').read()
    a = analyze(data, cfg.get('base', 0x1000), cfg.get('bank', 3), cfg)
    print('instructions:', len(a.insn), ' functions:', len(a.entries), ' warnings:', len(a.warn))
    for w in a.warn[:60]: print(' ', w)
