"""C emitter for the 65816 translator.  See analyze.py for the analysis it relies on and runtime/m816.h for the runtime."""
import sys, re
from collections import defaultdict

BANKARR = {0: 'B0', 1: 'B1', 2: 'B2', 3: 'B3', 4: 'B4', 5: 'B5', 7: 'B7', 0xE0: 'BE0', 0xE1: 'BE1'}


_IDX1 = re.compile(r'^\(0x([0-9A-F]{6}) \+ ([XY])\)$')
_IDX2 = re.compile(r'^\(\(\(u32\)(\d+) << 16 \| 0x([0-9A-F]{4})\) \+ ([XY])\)$')
def _idxref(ref):
    """(bank, offset, register) if `ref` is a fixed 24-bit base plus X or Y"""
    m = _IDX1.match(ref)
    if m: v = int(m.group(1), 16); return v >> 16, v & 0xFFFF, m.group(2)
    m = _IDX2.match(ref)
    if m: return int(m.group(1)), int(m.group(2), 16), m.group(3)
    return None

class Emitter:
    def __init__(self, a, cfg, name):
        self.a, self.cfg, self.name = a, cfg, name
        self.bank = a.bank
        self.lines = []
        self.errors = []

    # ---------- partition into functions ----------
    def partition(self):
        a = self.a
        entries = set(a.entries.keys())
        succ = a.succ
        def reach(e, ents):
            seen = set(); work = [e]
            while work:
                p = work.pop()
                if p in seen or p not in a.insn: continue
                if p != e and p in ents: continue
                seen.add(p)
                for kind, t in succ.get(p, []):
                    if kind in ('fall', 'br', 'jmp', 'inline_cont') and t is not None: work.append(t)
            return seen
        while True:
            owners = defaultdict(list)
            sets = {}
            for e in sorted(entries):
                s = reach(e, entries); sets[e] = s
                for p in s: owners[p].append(e)
            multi = [p for p, o in owners.items() if len(o) > 1]
            if not multi: break
            targets = set()
            for p, ss in succ.items():
                for kind, t in ss:
                    if kind in ('br', 'jmp') and t in multi: targets.add(t)
            promote = [t for t in targets if t not in entries]
            if not promote: promote = [min(multi)]
            entries.update(promote)
        self.entries = entries; self.owner = {p: o[0] for p, o in owners.items()}; self.fsets = sets

    # ---------- operand helpers ----------
    def static_mem(self, bank, off):
        return BANKARR.get(bank)

    def rd(self, i, w16, kind, ref):
        """C expression reading memory. ref: ('s', bank, off) or ('d', expr24)"""
        if kind == 's':
            bank, off = ref
            arr = BANKARR.get(bank)
            if self.cfg.get('aux') and bank == 0 and 0x200 <= off < 0xC000 and not (w16 and off == 0xBFFF):
                return ('RDB0_16(0x%04X)' % off) if w16 else ('RDB0_8(0x%04X)' % off)
            if bank in (0xE0, 0xE1) and 0xC000 <= off < 0xD000:
                a24 = (bank << 16) | off
                if w16: return '({ u8 _il = IORD8(0x%06X, %s, 0x%04X); u8 _ih = IORD8(0x%06X, %s, 0x%04X); (u16)(_il | (_ih << 8)); })' % (a24, arr, off, a24 + 1, arr, off + 1)
                return 'IORD8(0x%06X, %s, 0x%04X)' % (a24, arr, off)
            if arr and not (w16 and off == 0xFFFF):
                return ('RDS16(%s, 0x%04X)' % (arr, off)) if w16 else ('%s[0x%04X]' % (arr, off))
            ex = '0x%06X' % ((bank << 16) | off)
            return ('RD16(%s)' % ex) if w16 else ('RD8(%s)' % ex)
        ix = _idxref(ref)
        if ix and not self.cfg.get('aux') or (ix and ix[0] != 0):
            bank, off, reg = ix; arr = BANKARR.get(bank)
            if arr and bank not in (0xE0, 0xE1):
                if w16: return '({ u32 _o = 0x%04X + (u32)%s; _o < 0xFFFF ? RDS16(%s, _o) : RD16(%s); })' % (off, reg, arr, ref)
                return '({ u32 _o = 0x%04X + (u32)%s; _o <= 0xFFFF ? %s[_o] : RD8(%s); })' % (off, reg, arr, ref)
        return ('RD16(%s)' % ref) if w16 else ('RD8(%s)' % ref)

    def wr(self, i, w16, kind, ref, val):
        if kind == 's':
            bank, off = ref
            arr = BANKARR.get(bank)
            if self.cfg.get('aux') and bank == 0 and 0x200 <= off < 0xC000 and not (w16 and off == 0xBFFF):
                return ('WRB0_16(0x%04X, %s);' % (off, val)) if w16 else ('WRB0_8(0x%04X, (u8)(%s));' % (off, val))
            if bank == 1 and not (w16 and off == 0xFFFF):
                if w16: return 'do { u16 _w = (%s); WRB1(0x%04X, (u8)_w); WRB1(0x%04X, (u8)(_w >> 8)); } while (0);' % (val, off, off + 1)
                return 'WRB1(0x%04X, (u8)(%s));' % (off, val)
            if bank in (0xE0, 0xE1) and 0xC000 <= off < 0xD000:
                a24 = (bank << 16) | off
                if w16: return 'do { u16 _w = (%s); IOWR8(0x%06X, %s, 0x%04X, (u8)_w); IOWR8(0x%06X, %s, 0x%04X, (u8)(_w >> 8)); } while (0);' % (val, a24, arr, off, a24 + 1, arr, off + 1)
                return 'IOWR8(0x%06X, %s, 0x%04X, (u8)(%s));' % (a24, arr, off, val)
            if bank == 0xE1 and not (w16 and off == 0xFFFF):
                return ('WRE1_16(0x%04X, %s);' % (off, val)) if w16 else ('WRE1_8(0x%04X, (u8)(%s));' % (off, val))
            if arr and not (w16 and off == 0xFFFF):
                return ('WRS16(%s, 0x%02X, 0x%04X, %s);' % (arr, bank, off, val)) if w16 else ('WRS8(%s, 0x%02X, 0x%04X, (u8)(%s));' % (arr, bank, off, val))
            ex = '0x%06X' % ((bank << 16) | off)
            return ('WR16(%s, %s);' % (ex, val)) if w16 else ('WR8(%s, (u8)(%s));' % (ex, val))
        ix = _idxref(ref)
        if ix and (not self.cfg.get('aux') or ix[0] != 0):
            bank, off, reg = ix; arr = BANKARR.get(bank)
            if arr and bank in (0, 2, 3, 4, 5, 7):           # banks without shadowing or soft switches: store directly when the index does not carry into the next bank
                if w16: return 'do { u32 _o = 0x%04X + (u32)%s; u16 _v = (%s); if (_o < 0xFFFF) WRS16(%s, 0x%02X, _o, _v); else WR16(%s, _v); } while (0);' % (off, reg, val, arr, bank, ref)
                return 'do { u32 _o = 0x%04X + (u32)%s; u8 _v = (u8)(%s); if (_o <= 0xFFFF) WRS8(%s, 0x%02X, _o, _v); else WR8(%s, _v); } while (0);' % (off, reg, val, arr, bank, ref)
        return ('WR16(%s, %s);' % (ref, val)) if w16 else ('WR8(%s, (u8)(%s));' % (ref, val))

    def dp(self, i, extra=''):
        """bank-0 offset expression (u16) of a direct page operand"""
        dk = i.dk
        base = ('%d' % dk) if dk is not None else 'cpu.d'
        return '(u16)(%s + %d%s)' % (base, i.val & 0xFF, extra)

    def dbank(self, i):
        return ('(u32)%d' % i.db) if i.db is not None else '(u32)cpu.db'

    def idx(self, reg, i):
        return reg   # X/Y hold at most 8 significant bits when the register is 8 bits wide (masked on every write)

    def ea(self, i):
        """effective address of a memory operand: ('s', (bank, off)) for a fixed location, ('d', expr) otherwise"""
        m = i.mode; v = i.val
        if m == 'dp':
            if i.dk is not None: return 's', (0, (i.dk + v) & 0xFFFF)
            return 'd', '(u32)%s' % self.dp(i)
        if m == 'dpx': return 'd', '(u32)%s' % self.dp(i, ' + X')
        if m == 'dpy': return 'd', '(u32)%s' % self.dp(i, ' + Y')
        if m == 'abs':
            if i.db is not None: return 's', (i.db, v)
            return 'd', '(((u32)cpu.db << 16) | 0x%04X)' % v
        if m == 'absx': return 'd', '((%s | 0x%04X) + X)' % (self.dbank(i) + ' << 16', v)
        if m == 'absy': return 'd', '((%s | 0x%04X) + Y)' % (self.dbank(i) + ' << 16', v)
        if m == 'long': return 's', (v >> 16, v & 0xFFFF)
        if m == 'longx': return 'd', '(0x%06X + X)' % v
        if m == 'ind': return 'd', '((%s << 16) | RD16(%s))' % (self.dbank(i), self.dp(i))
        if m == 'indx': return 'd', '((%s << 16) | RD16(%s))' % (self.dbank(i), self.dp(i, ' + X'))
        if m == 'indy': return 'd', '(((%s << 16) | RD16(%s)) + Y)' % (self.dbank(i), self.dp(i))
        if m == 'indl': return 'd', 'RD24(%s)' % self.dp(i)
        if m == 'indly': return 'd', '(RD24(%s) + Y)' % self.dp(i)
        if m == 'sr': return 'd', '(u32)(u16)(S + %d)' % v
        if m == 'sry': return 'd', '(((%s << 16) | RD16((u16)(S + %d))) + Y)' % (self.dbank(i), v)
        raise ValueError('mode %s' % m)

    # ---------- instruction emission ----------
    def insn(self, i, out, fn_entries):
        mn = i.mn; m16 = not i.m; x16 = not i.x
        W = 16 if m16 else 8; XW = 16 if x16 else 8
        L = lambda s: out.append('    ' + s)
        pc = i.pc
        def label_target(t):
            return 'L_%04X' % t
        def jump(t, cond=None):
            """emit a jump/branch to address t (same function: goto; another function: tail call)"""
            if t in fn_entries and t != self.cur_entry:
                body = 'SAVE(); return f_%04X();' % t
                L(('if (%s) { %s }' % (cond, body)) if cond else body)
            else:
                L(('if (%s) goto %s;' % (cond, label_target(t))) if cond else 'goto %s;' % label_target(t))
        def setnz(expr, w):
            L('SETNZ%d(%s);' % (w, expr))
        def operand_value(wide):
            if i.mode in ('immM', 'immX'): return '0x%X' % i.val
            kind, ref = self.ea(i)
            return self.rd(i, wide, kind, ref)
        def store(wide, val):
            kind, ref = self.ea(i)
            L(self.wr(i, wide, kind, ref, val))
        if mn == 'LDA':
            v = operand_value(m16)
            if m16: L('A = %s; SETNZ16(A);' % v)
            else: L('{ u8 _v = %s; A = (u16)((A & 0xFF00) | _v); SETNZ8(_v); }' % v)
        elif mn in ('LDX', 'LDY'):
            r = 'X' if mn == 'LDX' else 'Y'
            v = operand_value(x16)
            if x16: L('%s = %s; SETNZ16(%s);' % (r, v, r))
            else: L('%s = %s; SETNZ8(%s);' % (r, v, r))
        elif mn == 'STA': store(m16, 'A' if m16 else '(A & 255)')
        elif mn == 'STX': store(x16, 'X')
        elif mn == 'STY': store(x16, 'Y')
        elif mn == 'STZ': store(m16, '0')
        elif mn in ('ORA', 'AND', 'EOR'):
            op = {'ORA': '|', 'AND': '&', 'EOR': '^'}[mn]
            v = operand_value(m16)
            if m16: L('A = A %s %s; SETNZ16(A);' % (op, v))
            else: L('{ u8 _v = %s; A = (u16)((A & 0xFF00) | ((A %s _v) & 255)); SETNZ8(A); }' % (v, op))
        elif mn in ('ADC', 'SBC'):
            v = operand_value(m16)
            pre = 'D' if i.d else ''
            L('%s%s%d(%s);' % (pre, mn, 16 if m16 else 8, v))
        elif mn == 'CMP': L('CMP%d(A, %s);' % (W, operand_value(m16)))
        elif mn == 'CPX': L('CMP%d(X, %s);' % (XW, operand_value(x16)))
        elif mn == 'CPY': L('CMP%d(Y, %s);' % (XW, operand_value(x16)))
        elif mn == 'BIT':
            v = operand_value(m16)
            if i.mode == 'immM':
                L('Z = ((%s) & %s) == 0;' % ('A' if m16 else '(A & 255)', v))
            else:
                L('{ u32 _v = %s; N = (_v >> %d) & 1; V = (_v >> %d) & 1; Z = ((%s) & _v) == 0; }' % (v, 15 if m16 else 7, 14 if m16 else 6, 'A' if m16 else '(A & 255)'))
        elif mn in ('ASL', 'LSR', 'ROL', 'ROR'):
            if i.mode == 'acc':
                if m16: L('A = (u16)%s16(A);' % mn)
                else: L('A = (u16)((A & 0xFF00) | %s8(A));' % mn)
            else:
                kind, ref = self.ea(i)
                if kind == 'd':
                    L('{ u32 _a = %s; %s _x = %s%d(%s); %s }' % (ref, 'u32', mn, W, self.rd(i, m16, 'd', '_a'), self.wr(i, m16, 'd', '_a', '_x')))
                else:
                    L('{ u32 _x = %s%d(%s); %s }' % (mn, W, self.rd(i, m16, kind, ref), self.wr(i, m16, kind, ref, '_x')))
        elif mn in ('INC', 'DEC'):
            d = '+ 1' if mn == 'INC' else '- 1'
            if i.mode == 'acc':
                if m16: L('A = (u16)(A %s); SETNZ16(A);' % d)
                else: L('A = (u16)((A & 0xFF00) | ((A %s) & 255)); SETNZ8(A);' % d)
            else:
                kind, ref = self.ea(i)
                if kind == 'd':
                    L('{ u32 _a = %s; u32 _x = %s %s; %s SETNZ%d(_x); }' % (ref, self.rd(i, m16, 'd', '_a'), d, self.wr(i, m16, 'd', '_a', '_x'), W))
                else:
                    L('{ u32 _x = %s %s; %s SETNZ%d(_x); }' % (self.rd(i, m16, kind, ref), d, self.wr(i, m16, kind, ref, '_x'), W))
        elif mn in ('TSB', 'TRB'):
            kind, ref = self.ea(i)
            av = 'A' if m16 else '(A & 255)'
            nv = '(_v | A)' if mn == 'TSB' else '(_v & ~(u32)A)'
            if kind == 'd':
                L('{ u32 _a = %s; u32 _v = %s; Z = (%s & _v) == 0; %s }' % (ref, self.rd(i, m16, 'd', '_a'), av, self.wr(i, m16, 'd', '_a', nv)))
            else:
                L('{ u32 _v = %s; Z = (%s & _v) == 0; %s }' % (self.rd(i, m16, kind, ref), av, self.wr(i, m16, kind, ref, nv)))
        elif mn in ('INX', 'INY', 'DEX', 'DEY'):
            r = mn[2]; d = '+ 1' if mn[0] == 'I' else '- 1'
            if x16: L('%s = (u16)(%s %s); SETNZ16(%s);' % (r, r, d, r))
            else: L('%s = (u16)((%s %s) & 255); SETNZ8(%s);' % (r, r, d, r))
        elif mn in ('BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ'):
            cond = {'BPL': '!N', 'BMI': 'N', 'BVC': '!V', 'BVS': 'V', 'BCC': '!C', 'BCS': 'C', 'BNE': '!Z', 'BEQ': 'Z'}[mn]
            t = (pc + 2 + (i.val - 256 if i.val > 127 else i.val)) & 0xFFFF
            jump(t, cond)
        elif mn == 'BRA':
            jump((pc + 2 + (i.val - 256 if i.val > 127 else i.val)) & 0xFFFF)
        elif mn == 'BRL':
            jump((pc + 3 + (i.val - 65536 if i.val > 32767 else i.val)) & 0xFFFF)
        elif mn == 'JMP' and i.mode == 'abs':
            if pc in self.a.patched_jmps:
                L('{ u16 _t = RDS16(B%X, 0x%04X); SAVE(); return dispatch_call(_t); }' % (self.bank, self.a.patched_jmps[pc]))
            else:
                jump(i.val)
        elif mn == 'JML' and i.mode == 'long':
            if (i.val >> 16) == self.bank: jump(i.val & 0xFFFF)
            else: L('SAVE(); rt_leave(0x%06X); return 0;' % i.val)
        elif mn == 'JSR' and i.mode == 'abs' and pc in self.cfg.get('patched_jsr', {}):
            L('{ u16 _t = RDS16(B%X, 0x%04X); PUSH16(0x%04X); SAVE(); r = dispatch_call(_t); LOAD(); if (rt_exit) return 0; }' % (self.bank, self.cfg['patched_jsr'][pc], (i.nxt - 1) & 0xFFFF))
            L('if (r != 0x%X) rt_bad_return(0x%04X, r, 0x%X);' % (i.nxt, pc, i.nxt))
        elif mn in ('JSR', 'JSL'):
            if mn == 'JSR' and i.mode == 'abs': t24 = (self.bank << 16) | i.val
            elif mn == 'JSL': t24 = i.val
            else: t24 = None
            if t24 is None:
                L('/* %s */ SAVE(); rt_bad_return(0x%04X, 0, 0); LOAD();' % (mn, pc))
            else:
                rep = self.cfg.get('replace', {}).get(t24 & 0xFFFF) if (t24 >> 16) == self.bank else None
                tgt = t24 & 0xFFFF
                inl = self.a.inline.get(t24)
                if rep:
                    L('SAVE(); %s(); LOAD();' % rep)
                elif (t24 >> 16) == self.bank and tgt in self.a.insn:
                    cont = i.nxt
                    if inl is not None:
                        p = cont + inl[0]
                        while self.a.data[p - self.a.base] != 0: p += 1
                        cont = p + 1
                    ret = '0x%04X' % ((cont - 1) & 0xFFFF) if mn == 'JSR' else '0x%06X' % (((self.bank << 16) | cont) - 1)
                    if mn == 'JSR': L('PUSH16(0x%04X);' % ((i.nxt - 1) & 0xFFFF))
                    else: L('PUSH8(0x%02X); PUSH16(0x%04X);' % (self.bank, (i.nxt - 1) & 0xFFFF))
                    L('SAVE(); r = f_%04X(); LOAD(); if (rt_exit) return 0;' % tgt)
                    L('if (r != 0x%X) rt_bad_return(0x%04X, r, 0x%X);' % (cont if mn == 'JSR' else ((self.bank << 16) | cont), pc, cont if mn == 'JSR' else ((self.bank << 16) | cont)))
                    if inl is not None: L('goto L_%04X;' % cont)
                else:
                    if mn == 'JSL': L('STUB_JSL(0x%04X);' % (((i.nxt - 1) & 0xFFFF)))
                    L('SAVE(); rt_jsl(0x%06X); LOAD();' % t24)
        elif mn == 'RTS':
            L('{ u16 _r = PULL16(); SAVE(); return (u32)_r + 1; }')
        elif mn == 'RTL':
            L('{ u16 _r = PULL16(); u32 _b = PULL8(); SAVE(); return ((_b << 16) | _r) + 1; }')
        elif mn == 'PHA':
            if m16: L('PUSH16(A);')
            else: L('PUSH8(A & 255);')
        elif mn in ('PHX', 'PHY'):
            r = mn[2]
            if x16: L('PUSH16(%s);' % r)
            else: L('PUSH8(%s & 255);' % r)
        elif mn == 'PLA':
            if m16: L('A = PULL16(); SETNZ16(A);')
            else: L('{ u8 _v = PULL8(); A = (u16)((A & 0xFF00) | _v); SETNZ8(_v); }')
        elif mn in ('PLX', 'PLY'):
            r = mn[2]
            if x16: L('%s = PULL16(); SETNZ16(%s);' % (r, r))
            else: L('%s = PULL8(); SETNZ8(%s);' % (r, r))
        elif mn == 'PHP':
            L('PUSH8((u8)((N << 7) | (V << 6) | (%d << 5) | (%d << 4) | (%d << 3) | (I << 2) | (Z << 1) | C));' % (i.m, i.x, i.d))
        elif mn == 'PLP':
            L('{ u8 _p = PULL8(); N = _p >> 7; V = (_p >> 6) & 1; I = (_p >> 2) & 1; Z = (_p >> 1) & 1; C = _p & 1; }')
            nx = self.a.outkey[pc][1] if pc in self.a.outkey else i.x
            if nx: L('X &= 255; Y &= 255;')
        elif mn == 'PHB': L('PUSH8(cpu.db);')
        elif mn == 'PLB': L('cpu.db = PULL8(); SETNZ8(cpu.db);')
        elif mn == 'PHD': L('PUSH16(cpu.d);')
        elif mn == 'PLD': L('cpu.d = PULL16(); SETNZ16(cpu.d);')
        elif mn == 'PHK': L('PUSH8(0x%02X);' % self.bank)
        elif mn == 'PEA': L('PUSH16(0x%04X);' % i.val)
        elif mn == 'PEI': L('{ u16 _v = RD16(%s); PUSH16(_v); }' % self.dp(i))
        elif mn == 'PER': L('PUSH16(0x%04X);' % ((i.nxt + (i.val - 65536 if i.val > 32767 else i.val)) & 0xFFFF))
        elif mn == 'TAX': L('X = %s; SETNZ%d(X);' % ('A' if x16 else '(A & 255)', XW))
        elif mn == 'TAY': L('Y = %s; SETNZ%d(Y);' % ('A' if x16 else '(A & 255)', XW))
        elif mn == 'TXA':
            if m16: L('A = X; SETNZ16(A);')
            else: L('A = (u16)((A & 0xFF00) | (X & 255)); SETNZ8(A);')
        elif mn == 'TYA':
            if m16: L('A = Y; SETNZ16(A);')
            else: L('A = (u16)((A & 0xFF00) | (Y & 255)); SETNZ8(A);')
        elif mn == 'TXY': L('Y = X; SETNZ%d(Y);' % XW)
        elif mn == 'TYX': L('X = Y; SETNZ%d(X);' % XW)
        elif mn == 'TSX': L('X = %s; SETNZ%d(X);' % ('S' if x16 else '(S & 255)', XW))
        elif mn == 'TXS': L('S = X;')
        elif mn == 'TCD': L('cpu.d = A; SETNZ16(cpu.d);')
        elif mn == 'TDC': L('A = cpu.d; SETNZ16(A);')
        elif mn == 'TCS': L('S = A;')
        elif mn == 'TSC': L('A = S; SETNZ16(A);')
        elif mn == 'XBA': L('A = (u16)((A >> 8) | (A << 8)); SETNZ8(A);')
        elif mn == 'CLC': L('C = 0;')
        elif mn == 'SEC': L('C = 1;')
        elif mn == 'CLI': L('I = 0;')
        elif mn == 'SEI': L('I = 1;')
        elif mn in ('CLD', 'SED'): L('/* %s: decimal mode is tracked statically */' % mn)
        elif mn == 'CLV': L('V = 0;')
        elif mn == 'REP':
            v = i.val
            if v & 0x80: L('N = 0;')
            if v & 0x40: L('V = 0;')
            if v & 0x04: L('I = 0;')
            if v & 0x02: L('Z = 0;')
            if v & 0x01: L('C = 0;')
            if not (v & 0xC7): L('/* REP #$%02X: only M/X/D change (static) */' % v)
        elif mn == 'SEP':
            v = i.val
            if v & 0x80: L('N = 1;')
            if v & 0x40: L('V = 1;')
            if v & 0x04: L('I = 1;')
            if v & 0x02: L('Z = 1;')
            if v & 0x01: L('C = 1;')
            if v & 0x10: L('X &= 255; Y &= 255;')
        elif mn in ('NOP', 'WDM'): L(';')
        elif mn in ('MVN', 'MVP'):
            dst, src = i.val & 255, i.val >> 8
            step = '+ 1' if mn == 'MVN' else '- 1'
            L('cpu.db = 0x%02X;' % dst)
            mask = '' if x16 else ' & 255'
            L('for (;;) { u8 _b = RD8(((u32)0x%02X << 16) | X); WR8(((u32)0x%02X << 16) | Y, _b); X = (u16)((X %s)%s); Y = (u16)((Y %s)%s); A--; if (A == 0xFFFF) break; }' % (src, dst, step, mask, step, mask))
        else:
            L('rt_bad_return(0x%04X, 0xDEAD, 0); /* unimplemented %s */' % (pc, mn))

    def emit_function(self, e, out, fn_entries):
        self.cur_entry = e
        pcs = sorted(self.fsets[e])
        targets = set()
        for p in pcs:
            for kind, t in self.a.succ.get(p, []):
                if kind in ('br', 'jmp', 'inline_cont') and t in self.fsets[e]: targets.add(t)
            i = self.a.insn[p]
        # inline-data call continuations are jump targets too
        for p in pcs:
            for kind, t in self.a.succ.get(p, []):
                if kind == 'inline_cont': targets.add(t)
        nat = self.cfg.get('native') or {}
        mids = {int(k, 0): (v[0], int(v[1], 0)) for k, v in nat.get('mid', {}).items() if int(k, 0) in self.fsets[e]}
        for _, (_, resume) in mids.items(): targets.add(resume)
        out.append('u32 f_%04X(void) {' % e)
        out.append('    u16 A = cpu.a, X = cpu.x, Y = cpu.y, S = cpu.s; u8 N = cpu.n, V = cpu.v, Z = cpu.z, C = cpu.c, I = cpu.i; u32 r; (void)r;')
        nat = self.cfg.get('native')
        if nat and ('0x%04X' % e) in {('0x%04X' % int(k, 0)) for k in nat['fast']}:
            fnm = [v for k, v in nat['fast'].items() if int(k, 0) == e][0]
            out.append('#ifdef N_USE_NATIVE')
            out.append('    SAVE(); if (%s()) { LOAD(); { u16 _r = PULL16(); SAVE(); return (u32)_r + 1; } } LOAD();' % fnm)
            out.append('#endif')
        if pcs[0] != e: out.append('    goto L_%04X;' % e)
        prev_end = None
        for p in pcs:
            i = self.a.insn[p]
            if p in targets or p == e or (prev_end is not None and prev_end != p):
                out.append('L_%04X:;' % p)
            if p in mids:
                out.append('#ifdef N_USE_NATIVE')
                out.append('    SAVE(); if (%s()) { LOAD(); goto L_%04X; } LOAD();' % mids[p])
                out.append('#endif')
            out.append('    /* %04X %s */' % (p, self.disasm(i)))
            out.append('    TRACE(0x%06X, %d, %d, %d);' % ((self.bank << 16) | p, i.m, i.x, i.d))
            if self.cfg.get('pass_hook') is not None and p in [int(self.cfg['pass_hook'], 0)] + [int(h, 0) for h in self.cfg.get('extra_pass_hooks', [])]:
                out.append('    SAVE(); if (rt_pass(0x%06X)) { rt_exit = 3; return 0; } LOAD();' % ((self.bank << 16) | p))
            self.insn(i, out, fn_entries)
            nxt = i.nxt
            prev_end = nxt
            # fall-through out of the function / after an unconditional transfer
            ends = i.mn in ('RTS', 'RTL', 'BRA', 'BRL', 'JML', 'RTI', 'STP') or (i.mn == 'JMP')
            if not ends:
                fall = [t for k, t in self.a.succ.get(p, []) if k == 'fall']
                if i.mn in ('JSR', 'JSL'):
                    inl = self.a.inline.get((self.bank << 16) | i.val) if i.mn == 'JSR' else self.a.inline.get(i.val)
                    pass
                if fall:
                    ft = fall[-1]
                    nxt_in_order = None
                    idx = pcs.index(p)
                    nxt_in_order = pcs[idx + 1] if idx + 1 < len(pcs) else None
                    if ft != nxt_in_order or (ft not in self.fsets[e]):
                        if ft in self.fsets[e]: out.append('    goto L_%04X;' % ft)
                        elif ft in self.a.insn: out.append('    SAVE(); return f_%04X();' % ft)
                        else: out.append('    rt_bad_return(0x%04X, 0xFA11, 0); /* falls into unanalysed code */' % p)
                else:
                    out.append('    rt_bad_return(0x%04X, 0xFA11, 0);' % p)
        out.append('}')

    def disasm(self, i):
        return '%s %s' % (i.mn, ('$%X' % i.val) if i.size > 1 else '')

    def emit(self):
        self.partition()
        a = self.a
        out = ['/* generated by tools/t816 from %s: do not edit */' % self.name] + (['#define T816_AUX 1'] if self.cfg.get('aux') else []) + ['#include "m816.h"', '']
        nat = self.cfg.get('native')
        if nat:
            out += ['#if !defined(T816_DEBUG) || defined(T816_FAST)', '#define N_USE_NATIVE 1', '#include "%s"' % nat['consts'], '#include "%s"' % nat.get('header', 'engine.h'), '#endif', '']
        fe = self.entries
        for e in sorted(fe): out.append('u32 f_%04X(void);' % e)
        out.append('u32 dispatch_call(u16 addr);')
        out.append('')
        for hook in sorted(set(self.cfg.get('replace', {}).values())): out.append('void %s(void);' % hook)
        out.append('')
        for e in sorted(fe):
            self.emit_function(e, out, fe)
            out.append('')
        out.append('u32 dispatch_call(u16 addr) {')
        out.append('    switch (addr) {')
        for e in sorted(fe): out.append('    case 0x%04X: return f_%04X();' % (e, e))
        out.append('    default: rt_bad_return(addr, 0xD15D, 0); return 0;')
        out.append('    }')
        out.append('}')
        return '\n'.join(out) + '\n'
