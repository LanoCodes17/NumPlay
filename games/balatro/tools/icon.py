#!/usr/bin/env python3
"""Makes src/icon.png: the Joker card over the title screen's paint swirl (the same shader as src/bg.c)."""
import os
import numpy as np
from PIL import Image
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
def paint(sx, sy, mode, t, spin=0.0, contrast=1.0, W=320, H=240):
    diag=np.hypot(W,H)
    ux=(sx-W/2)/diag; uy=(sy-H/2)/diag
    if mode==0: ux=ux-0.12
    ln=np.hypot(ux,uy)
    if mode==0:
        sp=0*0.1+302.2
        ang=np.arctan2(uy,ux)+sp-0.5*20*(spin*ln+(1-spin))
        speed=t*2
    else:
        s=t*0.4; ms=min(6,s)
        ang=np.arctan2(uy,ux)+(2.2+0.4*ms)*ln-1-s*0.05-ms*s*0.02
        speed=t*6*0.4+1033
    vx=ln*np.cos(ang)*30; vy=ln*np.sin(ang)*30
    u2x=vx+vy; u2y=vx+vy
    for i in range(5):
        m=np.sin(np.maximum(vx,vy))
        u2x=u2x+m+vx; u2y=u2y+m+vy
        vx=vx+0.5*np.cos(5.1123314+0.353*u2y+speed*0.131121)
        vy=vy+0.5*np.sin(u2x-0.113*speed)
        d=np.cos(vx+vy)-np.sin(vx*0.711-vy)
        vx=vx-d; vy=vy-d
    l=np.hypot(vx,vy)
    if mode==0:
        cm=0.25*contrast+0.5*spin+1.2
        return np.clip(l*0.035*cm,0,2)
    r=np.clip(1.5+l*0.12-0.17*min(10,t*1.2-4),-2,2)
    r=np.where(r<0.2,(r-0.2)*0.6+0.2,r)
    return r
def colorize(r, mode, c1=(0x50,0x84,0x6e)):
    if mode==1:
        c1p=np.maximum(0,1-2*np.abs(1-r)); c2p=np.maximum(0,1-2*r); cb=1-np.minimum(1,c1p+c2p)
        red=np.array([0xFE,0x5F,0x55])/255; blue=np.array([0,0x9D,0xFF])/255; black=0.6*np.array([79,99,103])/255
        out=c1p[...,None]*red+c2p[...,None]*blue+cb[...,None]*black
        fl=np.maximum(np.maximum(c1p,c2p)*5-4.4,0)[...,None]
        out=out*(1-fl)+fl
    else:
        c=np.array(c1)/255; C=c*0.9; L=np.minimum(c*1.3,1); D=c*0.7
        cm=0.25*1+1.2
        c1p=np.maximum(0,1-cm*np.abs(1-r)); c2p=np.maximum(0,1-cm*np.abs(r)); c3p=1-np.minimum(1,c1p+c2p)
        k=0.3
        out=k*C+(1-k)*(C*c1p[...,None]+L*c2p[...,None]+D*c3p[...,None])
    return (np.clip(out,0,1)*255).astype(np.uint8)
from PIL import Image, ImageDraw
W,H=55,56
yy,xx=np.mgrid[0:240,0:320].astype(float)
bg=colorize(paint(xx,yy,1,24.0),1)
bgim=Image.fromarray(bg).crop((160-60,120-61,160+60,120+61)).resize((W,H),Image.BOX).convert('RGBA')
J=Image.open(os.path.join(ROOT, 'assets', 'Jokers.png')).convert('RGBA').crop((0,0,71,95))
card=J.resize((36,48),Image.BOX)
rot=card.rotate(-8,resample=Image.BICUBIC,expand=True)
sh=Image.new('RGBA',rot.size,(0,0,0,0)); a=np.array(rot)[:,:,3]; sh.putalpha(Image.fromarray((a*0.45).astype(np.uint8)))
bgim.alpha_composite(sh,((W-rot.width)//2+2,(H-rot.height)//2+3))
bgim.alpha_composite(rot,((W-rot.width)//2,(H-rot.height)//2))
mask=Image.new('L',(W,H),0); ImageDraw.Draw(mask).rounded_rectangle((0,0,W-1,H-1),radius=6,fill=255)
out=Image.new('RGBA',(W,H),(0,0,0,0)); out.paste(bgim,(0,0),mask)
out.save(os.path.join(ROOT, 'src', 'icon.png'))
