#!/usr/bin/env python3
"""Minimal 65816 flow-following disassembler with M/X tracking.
usage: dis816.py file base entry[,entry...] [m=1 x=1] [--linear]"""
import sys
# addressing modes: (fmt, operand bytes) ; 'IM' = immediate sized by M ('m') or X ('x')
MODES = {
 'imp':('',0),'acc':('A',0),'imm8':('#$%02X',1),'immM':('#$%s',-1),'immX':('#$%s',-2),
 'dp':('$%02X',1),'dpx':('$%02X,X',1),'dpy':('$%02X,Y',1),'ind':('($%02X)',1),'indx':('($%02X,X)',1),'indy':('($%02X),Y',1),
 'indl':('[$%02X]',1),'indly':('[$%02X],Y',1),'abs':('$%04X',2),'absx':('$%04X,X',2),'absy':('$%04X,Y',2),
 'absind':('($%04X)',2),'absindx':('($%04X,X)',2),'absindl':('[$%04X]',2),'long':('$%06X',3),'longx':('$%06X,X',3),
 'rel':('REL',1),'rell':('RELL',2),'sr':('$%02X,S',1),'sry':('($%02X,S),Y',1),'blk':('BLK',2),
}
T={}
def op(o,n,m): T[o]=(n,m)
# group 1 ALU: ORA AND EOR ADC STA LDA CMP SBC
alu=['ORA','AND','EOR','ADC','STA','LDA','CMP','SBC']
g1={0x01:'indx',0x03:'sr',0x05:'dp',0x07:'indl',0x09:'immM',0x0D:'abs',0x0F:'long',0x11:'indy',0x12:'ind',0x13:'sry',0x15:'dpx',0x17:'indly',0x19:'absy',0x1D:'absx',0x1F:'longx'}
for i,n in enumerate(alu):
    for lo,m in g1.items():
        o=i*0x20+lo
        if n=='STA' and m=='immM': continue
        op(o,n,m)
# group 2/RMW
for o,n,m in [(0x06,'ASL','dp'),(0x0A,'ASL','acc'),(0x0E,'ASL','abs'),(0x16,'ASL','dpx'),(0x1E,'ASL','absx'),
 (0x26,'ROL','dp'),(0x2A,'ROL','acc'),(0x2E,'ROL','abs'),(0x36,'ROL','dpx'),(0x3E,'ROL','absx'),
 (0x46,'LSR','dp'),(0x4A,'LSR','acc'),(0x4E,'LSR','abs'),(0x56,'LSR','dpx'),(0x5E,'LSR','absx'),
 (0x66,'ROR','dp'),(0x6A,'ROR','acc'),(0x6E,'ROR','abs'),(0x76,'ROR','dpx'),(0x7E,'ROR','absx'),
 (0xC6,'DEC','dp'),(0x3A,'DEC','acc'),(0xCE,'DEC','abs'),(0xD6,'DEC','dpx'),(0xDE,'DEC','absx'),
 (0xE6,'INC','dp'),(0x1A,'INC','acc'),(0xEE,'INC','abs'),(0xF6,'INC','dpx'),(0xFE,'INC','absx'),
 (0x04,'TSB','dp'),(0x0C,'TSB','abs'),(0x14,'TRB','dp'),(0x1C,'TRB','abs'),
 (0x24,'BIT','dp'),(0x2C,'BIT','abs'),(0x34,'BIT','dpx'),(0x3C,'BIT','absx'),(0x89,'BIT','immM'),
 (0x84,'STY','dp'),(0x8C,'STY','abs'),(0x94,'STY','dpx'),
 (0x86,'STX','dp'),(0x8E,'STX','abs'),(0x96,'STX','dpy'),
 (0xA0,'LDY','immX'),(0xA4,'LDY','dp'),(0xAC,'LDY','abs'),(0xB4,'LDY','dpx'),(0xBC,'LDY','absx'),
 (0xA2,'LDX','immX'),(0xA6,'LDX','dp'),(0xAE,'LDX','abs'),(0xB6,'LDX','dpy'),(0xBE,'LDX','absy'),
 (0xC0,'CPY','immX'),(0xC4,'CPY','dp'),(0xCC,'CPY','abs'),(0xE0,'CPX','immX'),(0xE4,'CPX','dp'),(0xEC,'CPX','abs'),
 (0x64,'STZ','dp'),(0x74,'STZ','dpx'),(0x9C,'STZ','abs'),(0x9E,'STZ','absx'),
 (0x10,'BPL','rel'),(0x30,'BMI','rel'),(0x50,'BVC','rel'),(0x70,'BVS','rel'),(0x90,'BCC','rel'),(0xB0,'BCS','rel'),(0xD0,'BNE','rel'),(0xF0,'BEQ','rel'),(0x80,'BRA','rel'),(0x82,'BRL','rell'),
 (0x4C,'JMP','abs'),(0x5C,'JML','long'),(0x6C,'JMP','absind'),(0x7C,'JMP','absindx'),(0xDC,'JML','absindl'),
 (0x20,'JSR','abs'),(0x22,'JSL','long'),(0xFC,'JSR','absindx'),
 (0x00,'BRK','imm8'),(0x02,'COP','imm8'),(0x42,'WDM','imm8'),(0xC2,'REP','imm8'),(0xE2,'SEP','imm8'),
 (0xF4,'PEA','abs'),(0xD4,'PEI','ind'),(0x62,'PER','rell'),(0x54,'MVN','blk'),(0x44,'MVP','blk'),
]: op(o,n,m)
for o,n in [(0x18,'CLC'),(0x38,'SEC'),(0x58,'CLI'),(0x78,'SEI'),(0xB8,'CLV'),(0xD8,'CLD'),(0xF8,'SED'),(0xFB,'XCE'),
 (0xAA,'TAX'),(0xA8,'TAY'),(0x8A,'TXA'),(0x98,'TYA'),(0xBA,'TSX'),(0x9A,'TXS'),(0x9B,'TXY'),(0xBB,'TYX'),
 (0x5B,'TCD'),(0x7B,'TDC'),(0x1B,'TCS'),(0x3B,'TSC'),(0xEB,'XBA'),(0xE8,'INX'),(0xC8,'INY'),(0xCA,'DEX'),(0x88,'DEY'),(0xEA,'NOP'),
 (0x48,'PHA'),(0x68,'PLA'),(0xDA,'PHX'),(0xFA,'PLX'),(0x5A,'PHY'),(0x7A,'PLY'),(0x08,'PHP'),(0x28,'PLP'),(0x8B,'PHB'),(0xAB,'PLB'),(0x0B,'PHD'),(0x2B,'PLD'),(0x4B,'PHK'),
 (0x60,'RTS'),(0x6B,'RTL'),(0x40,'RTI'),(0xCB,'WAI'),(0xDB,'STP')]: op(o,n,'imp')
def dis(data,base,entries,m=1,x=1,linear=False,labels=None):
    seen={}; todo=[(e,m,x) for e in entries]; out={}
    def rd(a,n): 
        v=0
        for i in range(n): v|=data[a-base+i]<<(8*i)
        return v
    while todo:
        pc,m,x=todo.pop()
        while base<=pc<base+len(data):
            if (pc,m,x) in seen: break
            o=data[pc-base]; 
            if o not in T: break
            n,mode=T[o]; fmt,sz=MODES[mode]
            if sz==-1: sz=1 if m else 2
            elif sz==-2: sz=1 if x else 2
            if pc-base+1+sz>len(data): break
            val=rd(pc+1,sz) if sz else 0
            if mode in('immM','immX'): txt=n+' '+('#$%02X'%val if sz==1 else '#$%04X'%val)
            elif mode=='rel': t=(pc+2+(val-256 if val>127 else val))&0xFFFF; txt=n+' $%04X'%t
            elif mode=='rell': t=(pc+3+(val-65536 if val>32767 else val))&0xFFFF; txt=n+' $%04X'%t
            elif mode=='blk': txt=n+' $%02X,$%02X'%(val>>8,val&255)
            elif mode=='imp': txt=n
            elif mode=='acc': txt=n+' A'
            else: txt=n+' '+fmt%val
            seen[(pc,m,x)]=1; out[pc]=(o,sz,m,x,txt,data[pc-base:pc-base+1+sz])
            nm,nx=m,x
            if n=='REP': nm=0 if val&0x20 else m; nx=0 if val&0x10 else x
            if n=='SEP': nm=1 if val&0x20 else m; nx=1 if val&0x10 else x
            if n=='XCE': nm=1;nx=1  # assume emu
            if mode in('rel','rell'):
                todo.append((t,nm,nx))
                if n in('BRA','BRL'): break
            if n in('JMP','JML','RTS','RTL','RTI','STP'):
                if n=='JMP' and mode=='abs': todo.append((val,nm,nx))
                if n=='JML' and mode=='long': todo.append((val&0xFFFF,nm,nx))
                break
            if n=='JSR' and mode=='abs': todo.append((val,nm,nx))
            if n=='JSL': todo.append((val&0xFFFF,nm,nx)) if (val>>16)==0 or True else None
            m,x=nm,nx; pc+=1+sz
    return out
if __name__=='__main__':
    f=sys.argv[1]; base=int(sys.argv[2],16); ents=[int(e,16) for e in sys.argv[3].split(',')]
    m=0;x=0
    data=open(f,'rb').read()
    out=dis(data,base,ents,m,x)
    for a in sorted(out):
        o,sz,mm,xx,txt,raw=out[a]
        print('%04X: %-12s %s   ;m%d x%d'%(a,raw.hex(),txt,mm,xx))
