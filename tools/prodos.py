import sys, struct
def load(path):
    d=open(path,'rb').read()
    if d[:4]==b'2IMG':
        off=struct.unpack('<I',d[24:28])[0]; ln=struct.unpack('<I',d[28:32])[0]
        fmt=struct.unpack('<I',d[12:16])[0]
        print('2IMG header: creator',d[4:8],'fmt',fmt,'off',off,'len',ln, 'flags',hex(struct.unpack('<I',d[16:20])[0]))
        d=d[off:off+ln] if ln else d[off:]
    return d
def blk(d,n): return d[n*512:(n+1)*512]
def walk(d,keyblk,path,out,depth=0):
    b=keyblk
    while b:
        data=blk(d,b)
        nxt=struct.unpack('<H',data[2:4])[0]
        for i in range(13):
            e=data[4+i*39:4+(i+1)*39]
            st=e[0]>>4; nl=e[0]&15
            if st==0: continue
            if st==0xF or st==0xE: continue  # headers
            name=e[1:1+nl].decode('latin1')
            ft=e[16]; kp=struct.unpack('<H',e[17:19])[0]; bu=struct.unpack('<H',e[19:21])[0]
            eof=e[21]|(e[22]<<8)|(e[23]<<16); aux=struct.unpack('<H',e[31:33])[0]
            out.append((path+name,st,ft,kp,bu,eof,aux))
            if st==0xD: walk(d,kp,path+name+'/',out,depth+1)
        b=nxt
def readfile(d,st,kp,eof):
    if st==1: return blk(d,kp)[:eof]
    if st==2:
        idx=blk(d,kp); blks=[idx[i]|(idx[256+i]<<8) for i in range(256)]
    elif st==3:
        m=blk(d,kp); blks=[]
        for j in range(256):
            ib=m[j]|(m[256+j]<<8)
            if ib==0: blks+= [0]*256
            else:
                idx=blk(d,ib); blks+=[idx[i]|(idx[256+i]<<8) for i in range(256)]
    else: return b''
    out=b''.join(blk(d,x) if x else b'\0'*512 for x in blks)
    return out[:eof]
if __name__=='__main__':
    d=load(sys.argv[1])
    v=blk(d,2); print('vol',v[5:5+(v[4]&15)])
    out=[]; walk(d,2,'/',out)
    for o in out: print('%-40s st=%x type=$%02X blocks=%4d eof=%7d aux=$%04X'%(o[0],o[1],o[2],o[4],o[5],o[6]))
