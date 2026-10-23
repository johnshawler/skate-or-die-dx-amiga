"""Skate or Die (Apple IIGS) resource decompressor -- variable-width LZW,
reconstructed from the INTER routine at $07:71F9."""
import struct
class Bits:
    def __init__(s,d): s.d=d; s.pos=0; s.acc=0; s.n=0
    def read(s,w):
        while s.n<w:
            if s.pos>=len(s.d): raise EOFError
            s.acc|=s.d[s.pos]<<s.n; s.pos+=1; s.n+=8
        v=s.acc&((1<<w)-1); s.acc>>=w; s.n-=w; return v
def decompress(data):
    b=Bits(data)
    w1=b.read(16); count=b.read(16)
    first9=b.read(9)
    out=bytearray()
    def reset():
        return [0xFFFF]*256+[0]*(0x1000-256), list(range(256))+[0]*(0x1000-256)
    prefix,suffix=reset(); nxt=0x100; thresh=0x100; width=8
    remaining=count
    try:
        prev=b.read(width); remaining-=1
        def emit(code):
            s=[]
            while True:
                s.append(suffix[code]); p=prefix[code]
                if p==0xFFFF: break
                code=p
            s.reverse(); out.extend(s); return s[0]
        first=emit(prev)
        while remaining>0:
            if nxt==0x1000:
                prefix,suffix=reset(); nxt=0x100; thresh=0x100; width=8
            if nxt==thresh: width+=1; thresh*=2
            code=b.read(width); remaining-=1
            if code!=nxt:
                first=emit(code)
                prefix[nxt]=prev; suffix[nxt]=first; nxt+=1
            else:
                prefix[nxt]=prev; suffix[nxt]=first; nxt+=1
                first_new=emit(code)
            prev=code
    except EOFError:
        pass
    return w1,count,first9,bytes(out)
if __name__=='__main__':
    import sys
    w1,c,f9,o=decompress(open(sys.argv[1],'rb').read())
    print('word1=%04x count=%d first9=%03x out=%d bytes'%(w1,c,f9,len(o)))
    open(sys.argv[2],'wb').write(o)
