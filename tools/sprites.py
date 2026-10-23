import struct
from PIL import Image
def load_table(path):
    d=open(path,'rb').read(); w=struct.unpack('<%dH'%(len(d)//2),d); N=w[0]
    arr=[w[1+k*N:1+(k+1)*N] for k in range(7)]
    return [dict(w=arr[0][i],words=arr[1][i],hot=arr[2][i],off=arr[3][i]) for i in range(N)]
def palette(data,pal_index=0):
    pal=data[32256:32768]; cols=[]
    for i in range(16):
        v=pal[pal_index*32+i*2]|(pal[pal_index*32+i*2+1]<<8)
        cols.append((((v>>8)&15)*17,((v>>4)&15)*17,(v&15)*17))
    return cols
def frame(img,msk,fr,cols,bg=(255,0,255)):
    w=fr['w']; rows=fr['words']//w; o=fr['off']
    im=Image.new('RGB',(w*4,rows),bg); px=im.load()
    for y in range(rows):
        for x in range(w*2):
            b=img[o+y*w*2+x]; m=msk[o+y*w*2+x]
            for k,(c,mm) in enumerate(((b>>4,m>>4),(b&15,m&15))):
                if mm==0: px[x*2+k,y]=cols[c]
    return im
def sheet(table,img,msk,cols,cols_per_row=24,cell=(28,60)):
    N=len(table); rows=(N+cols_per_row-1)//cols_per_row
    S=Image.new('RGB',(cols_per_row*cell[0],rows*cell[1]),(60,60,60))
    for i,fr in enumerate(table):
        im=frame(img,msk,fr,cols); S.paste(im,((i%cols_per_row)*cell[0],(i//cols_per_row)*cell[1]))
    return S
