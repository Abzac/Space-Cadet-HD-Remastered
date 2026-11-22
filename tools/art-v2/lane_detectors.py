"""Registered spring-wire rollover switches on the Windows Space Cadet table."""
import numpy as np
from PIL import Image


# Top and bottom centres, their widths, and the slope of each cross-edge.
# Local coordinates of the actual steel mount in the 4x registered plate.
# Its long edges converge with the perspective of the surrounding red guides.
MOUNTS = (
    (3.77, .04, 4.21, 3.25, 8.77, 4.29, -.012, .018),
    (3.24, .32, 4.20, 3.02, 9.34, 4.30, .022, .024),
    (3.31, .08, 4.26, 3.53, 9.25, 4.32, -.035, -.018),
    (5.64, .76, 6.45, 3.70, 15.82, 6.74, -.048, -.094),
    (5.33, .89, 5.88, 3.82, 15.60, 6.04, -.005, -.011),
    (3.98, .13, 5.88, 3.64, 15.48, 6.00, -.022, -.028),
    (3.82, 1.02, 5.87, 5.08, 15.05, 5.98, -.018, -.049),
    (3.58, 1.05, 5.63, 5.14, 14.76, 5.70, .031, .031),
    (3.76, .80, 4.43, 2.25, 9.82, 4.46, -.008, -.026),
    (3.70, .43, 4.39, 2.66, 9.56, 4.51, -.013, -.032),
    (4.18, -.91, 4.14, 3.40, 9.20, 4.20, .008, .031),
)


def ease(x):
    t = np.clip(x, 0, 1)
    return t*t*(3-2*t)


def material(gid, width, height, scale, raised, padding=0):
    # Shade both positions in one fixed mount space. Only the interior of the
    # pressed wire pocket is replaced by the live sprite; the rim and pin are
    # baked onto the board. Sample coverage before reducing to the 4x asset.
    rate = scale*4
    h, w = height+2*padding, width+2*padding
    y, x = (np.mgrid[:h*rate, :w*rate].astype('float32')+.5)/rate-padding
    xt, yt, wt, xb, yb, wb, mt, mb = MOUNTS[gid-520]
    v = (y-yt)/(yb-yt)
    # Invert the projected quadrilateral, including the transverse tilt.
    for _ in range(2):
        center = xt + v*(xb-xt)
        v = (y-yt - (x-center)*(mt+v*(mb-mt)))/(yb-yt)
    half = (wt+(wb-wt)*v)*.5
    u = x-(xt+(xb-xt)*v)
    length = yb-yt
    a, b = half-abs(u), length*np.minimum(v,1-v)
    def rounded(a, b, r):
        return r-(np.hypot(np.maximum(r-a,0),np.maximum(r-b,0)) + np.minimum(np.maximum(r-a,r-b),0))
    d = rounded(a,b,.34)
    side = np.where(u<0,.89,.48)
    steel = np.where(a<b, side, np.where(v<.5,.99,.63))
    steel += .12*np.exp(-((v-.27)/.28)**2) - .08*ease(v)
    def tint(r,g,b): return np.array((r,g,b),dtype='float32')
    def mix(back, fore, amount):
        t=np.clip(amount,0,1)[...,None]
        return back*(1-t)+fore*t
    plate = tint(39,42,46)+(steel*151)[...,None]*tint(1.02,1.02,1.)
    plate += (28*np.exp(-((d-.23)/.15)**2))[...,None]
    plate = mix(tint(32,33,36),plate,ease((d+.04)/.20))
    wall = .16*min(wt,wb)
    inner = rounded(a-wall,b-.080*length,.32)
    cavity = tint(13,16,20)+(10*ease(v*.94)+4*ease(u+.4))[...,None]
    plate = mix(plate,tint(45,46,48)+(32*(1-steel))[...,None],ease((inner+.21)/.26))
    plate = mix(plate,cavity,ease((inner-.06)/.26))
    # Short oblique collars at both ends locate the leaf in its channel.
    collar = (abs(u) < .38*half) & (v>.79) & (v<.85)
    plate = mix(plate,tint(59,63,65),collar)
    # The grounded end has identical XY in both states. A standing wire
    # foreshortens and throws a narrow shadow; a pressed wire lies in its slot.
    start, stop = (.17 if raised else .235), .806
    t = np.clip((v-start)/(stop-start),0,1)
    lift = np.sin(np.pi*t)
    wire_x = -half*(.025+.047*lift) if raised else -half*.025
    rad = (.18 if raised else .155)*half
    # Cylindrical cross-section with a rounded bend fading into the socket.
    gap = np.maximum(np.maximum(start-v,v-stop),0)*length
    cross = (u-wire_x)/rad
    q = np.hypot(cross,gap/rad)
    if raised:
        shadow = np.exp(-((u-wire_x-.29-.40*lift)/(rad*.93))**2)*ease((v-start)*28)*ease((stop-v)*32)
        plate = mix(plate,tint(3,5,8),shadow*.88)
        round_z=np.sqrt(np.clip(1-q*q,0,1))
        lit = 35+47*round_z + 123*np.exp(-((cross+.37)/.42)**2)*(.59+.41*lift)
    else:
        lit = 34 + 27*np.sqrt(np.clip(1-q*q,0,1)) + 32*np.exp(-((cross+.29)/.51)**2)
    plate = mix(plate,lit[...,None]*tint(.96,1.,1.03),q<1)
    gate = np.ones_like(u)
    if gid==524:
        # Lip of the purple return tube and right-hand red lane separator.
        lip = np.interp(x,[-2,0,1.7,3.2,4.8,6.1,7.0,8.4],
                            [5.22,5.51,5.49,5.18,4.55,3.80,3.00,1.72])
        gate = ease((y-lip-.04)/.24)
        edge = np.interp(y,[0,9.1,12.7,13.5,14.5,16],[8.18,7.22,6.83,7.02,7.70,8.4])
        gate *= ease((edge-x)/.20)
        plate *= (.51+.49*ease((y-lip)/2.1))[...,None]
    elif gid==527:
        gate = ease((x-.96-.099*y)/.18)
    elif gid==528:
        gate = ease((6.27-.113*y-x)/.20)
    # All moving pixels stay within the mouth. Keep a little common well
    # around the old and new wire silhouettes for clean fall/rise transitions.
    if raised:
        cover = (np.abs(u)<.68*half) & (v>.137) & (v<.851) & (inner>.19)
    else:
        cover = d>-.035
    alpha = cover.astype('float32')*gate
    def average(values):
        return values.reshape(h*scale,4,w*scale,4).mean(axis=(1,3))
    aa=average(alpha)
    col=np.stack([average(plate[...,i]*alpha)/np.maximum(aa,1e-7) for i in range(3)],axis=-1)
    return (Image.fromarray(np.uint8(np.clip(np.rint(col),0,255))),
            Image.fromarray(np.uint8(np.rint(255*aa))))


def seat(board, where, width, height, gid, scale):
    x,y=map(int,where); pad=2
    pos=((x-pad)*scale,(y-pad)*scale)
    layer,alpha=material(gid,width,height,scale,False,pad)
    board.paste(layer,pos,alpha)
