"""ctypes wrapper around libcpu816.so (a minimal 65816 core, see cpu816.c)."""
import ctypes, os
_lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'libcpu816.so'))
class Regs(ctypes.Structure):
    _fields_ = [('a', ctypes.c_uint16), ('x', ctypes.c_uint16), ('y', ctypes.c_uint16), ('s', ctypes.c_uint16),
                ('d', ctypes.c_uint16), ('pc', ctypes.c_uint16), ('db', ctypes.c_uint8), ('pb', ctypes.c_uint8), ('p', ctypes.c_uint8)]
_lib.mem_ptr.restype = ctypes.POINTER(ctypes.c_uint8 * (1 << 24))
_lib.regs_ptr.restype = ctypes.POINTER(Regs)
_lib.cpu_run.argtypes = [ctypes.c_uint64]; _lib.cpu_run.restype = ctypes.c_int
_lib.hook_set.argtypes = [ctypes.c_uint32, ctypes.c_int]
for n in ('get_icount', 'get_cycles'): getattr(_lib, n).restype = ctypes.c_uint64
for n in ('get_hit_pc', 'get_fault_pc'): getattr(_lib, n).restype = ctypes.c_uint32
mem = _lib.mem_ptr().contents
cpu = _lib.regs_ptr().contents
def run(n): return _lib.cpu_run(n)
def hook(addr, on=True): _lib.hook_set(addr, 1 if on else 0)
def icount(): return _lib.get_icount()
def cycles(): return _lib.get_cycles()
def hit_pc(): return _lib.get_hit_pc()
def fault_pc(): return _lib.get_fault_pc()
def rd8(a): return mem[a & 0xFFFFFF]
def rd16(a): return mem[a & 0xFFFFFF] | (mem[(a + 1) & 0xFFFFFF] << 8)
def wr8(a, v): mem[a & 0xFFFFFF] = v & 255
def wr16(a, v): wr8(a, v); wr8(a + 1, v >> 8)
def load(addr, data):
    ctypes.memmove(ctypes.addressof(mem) + addr, bytes(data), len(data))
def rtl_at(addr): wr8(addr, 0x6B)
def rts_at(addr): wr8(addr, 0x60)

# ---- profiling (see cpu816.c) ----
_lib.prof_pc.restype = ctypes.POINTER(ctypes.c_uint32 * (8 << 16))
_lib.prof_dump.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
def prof_enable(on=1): _lib.prof_enable(on)
def prof_reset(): _lib.prof_reset()
def prof_pc(): return _lib.prof_pc().contents
def set_code_range(lo, hi):
    ctypes.c_uint32.in_dll(_lib, 'code_lo').value = lo; ctypes.c_uint32.in_dll(_lib, 'code_hi').value = hi
def prof_records(kind):
    buf = (ctypes.c_uint64 * (2 * 70000))(); n = _lib.prof_dump(kind, buf, 70000)
    return [(buf[2 * i], buf[2 * i + 1]) for i in range(n)]

_lib.prof_banks.restype = ctypes.POINTER(ctypes.c_uint32 * 256)
def prof_banks(w): return list(_lib.prof_banks(w).contents)

_lib.get_wlog_hash.restype = ctypes.c_uint64; _lib.get_wlog_n.restype = ctypes.c_uint64
_lib.wlog_start.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
def wlog_start(buf=None, cap=0): _lib.wlog_start(buf, cap)
def wlog_stop(): _lib.wlog_stop()
def wlog_hash(): return _lib.get_wlog_hash()
def wlog_n(): return _lib.get_wlog_n()

_lib.trace_start.argtypes = [ctypes.c_void_p, ctypes.c_uint64]; _lib.get_trace_n.restype = ctypes.c_uint64
def trace_start(buf, cap): _lib.trace_start(buf, cap)
def trace_stop(): _lib.trace_stop()
def trace_n(): return _lib.get_trace_n()

class IO(ctypes.Structure):
    _fields_ = [('key', ctypes.c_uint8), ('key_waiting', ctypes.c_uint8), ('anykey', ctypes.c_uint8), ('mods', ctypes.c_uint8), ('enabled', ctypes.c_uint8),
                ('btn', ctypes.c_uint8 * 3), ('pdl_len', ctypes.c_uint16 * 4), ('pdl_left', ctypes.c_uint16 * 4), ('ramrd', ctypes.c_uint8), ('ramwrt', ctypes.c_uint8)]
_lib.io_ptr.restype = ctypes.c_void_p
io = IO.from_address(_lib.io_ptr())
