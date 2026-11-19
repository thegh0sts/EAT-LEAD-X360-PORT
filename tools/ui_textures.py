# tools/ui_textures.py <name>...: decode Eat Lead UI textures from your own Games/0000000100000001.gam to PNGs
# (development: measuring prompt rectangles for assets/prompts/playstation.txt). Set EATLEAD_GAM to the file.
import struct, sys
from PIL import Image
import os
GAM=os.environ.get("EATLEAD_GAM", "eatlead-game/Games/0000000100000001.gam")
d=open(GAM,'rb').read()
def T(x,y,w,l):
    aw=(w+31)&~31
    macro=((x>>5)+(y>>5)*(aw>>5))<<(l+7)
    micro=((x&7)+((y&0xE)<<2))<<l
    off=macro+((micro&~0xF)<<1)+(micro&0xF)+((y&1)<<4)
    return ((off&~0x1FF)<<3)+((y&16)<<7)+((off&0x1C0)<<2)+(((((y&8)>>2)+(x>>3))&3)<<6)+(off&0x3F)
def sw16(b): return bytes([b[i^1] for i in range(len(b))])
def c565(v): return (((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31)
def dxt_block(b, kind):
    b=sw16(b)  # 8in16
    if kind=='dxt1': cb=b
    else: cb=b[8:]
    c0,c1=struct.unpack('<HH',cb[:4]); idx=struct.unpack('<I',cb[4:8])[0]
    p0,p1=c565(c0),c565(c1)
    if kind!='dxt1' or c0>c1: pal=[p0,p1,tuple((2*a+b)//3 for a,b in zip(p0,p1)),tuple((a+2*b)//3 for a,b in zip(p0,p1))]
    else: pal=[p0,p1,tuple((a+b)//2 for a,b in zip(p0,p1)),(0,0,0)]
    alpha=[255]*16
    if kind=='dxt1' and c0<=c1: alpha=[0 if ((idx>>(2*i))&3)==3 else 255 for i in range(16)]
    if kind=='dxt5':
        a0,a1=b[0],b[1]; ai=int.from_bytes(b[2:8],'little')
        if a0>a1: ap=[a0,a1]+[((6-i)*a0+(i+1)*a1)//7 for i in range(6)]
        else: ap=[a0,a1]+[((4-i)*a0+(i+1)*a1)//5 for i in range(4)]+[0,255]
        alpha=[ap[(ai>>(3*i))&7] for i in range(16)]
    if kind=='dxt3':
        av=int.from_bytes(b[0:8],'little'); alpha=[((av>>(4*i))&15)*17 for i in range(16)]
    return [pal[(idx>>(2*i))&3]+(alpha[i],) for i in range(16)]
def decode(off,w,h,kind):
    im=Image.new('RGBA',(w,h)); px=im.load()
    if kind=='argb':
        for y in range(h):
            for x in range(w):
                t=d[off+T(x,y,w,2):][:4]
                px[x,y]=(t[2],t[3],t[0],t[1])
        return im
    bs=8 if kind=='dxt1' else 16; l=3 if bs==8 else 4
    bw,bh=w//4,h//4
    for by in range(bh):
        for bx in range(bw):
            i=off+T(bx,by,bw,l)
            for k,c in enumerate(dxt_block(d[i:i+bs],kind)):
                px[bx*4+k%4,by*4+k//4]=c
    return im
def record(name):
    n=d.find(name.encode()+b'\0',0)
    while n>=0 and d[n-4:n]!=b'\0\0\0\x24': n=d.find(name.encode()+b'\0',n+1)
    w,h=struct.unpack('>HH',d[n+0x36:n+0x3a]); end=struct.unpack('>I',d[n+0x50:n+0x54])[0]
    return n,w,h,end

def info(name):
    n,w,h,end=record(name)
    fmt=struct.unpack('>I',d[n+0x44:n+0x48])[0]; alpha=d[n+0x48]
    return n,w,h,end,fmt,alpha
def load(name):
    n,w,h,end,fmt,alpha=info(name); o=n+0x61
    im=Image.new('RGBA',(w,h)); px=im.load()
    if fmt==3:
        for y in range(h):
            for x in range(w):
                i=o+T(x,y,w,2); t=d[i:i+4]; px[x,y]=(t[1],t[2],t[3],t[0])
    else:
        kind='dxt5' if alpha else 'dxt1'; bs=16 if alpha else 8; l=4 if alpha else 3
        bw,bh=w//4,h//4
        for by in range(bh):
            for bx in range(bw):
                i=o+T(bx,by,bw,l)
                for k,c in enumerate(dxt_block(d[i:i+bs],kind)): px[bx*4+k%4,by*4+k//4]=c
    return im

if __name__ == '__main__':
    for name in sys.argv[1:]:
        n, w, h, end, fmt, alpha = info(name)
        print(name, w, h, 'format', fmt, 'alpha', alpha)
        load(name).save(name + '.png')
