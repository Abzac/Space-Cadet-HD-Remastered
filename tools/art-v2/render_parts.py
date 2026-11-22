"""Generate registered 4x physical table parts for the Windows Space Cadet DAT.

Usage: python render_parts.py PINBALL.DAT HD-base HD-output [--board registered.png]
Uses the extraction routines in ../upscale.py.
Only the DAT group identity, placement and original light colors determine which
object is rendered. The ray-profile chrome, enamel, polymer, spring and electrical
discharge surfaces are evaluated at 12 samples per original pixel per axis.
"""
from pathlib import Path
import argparse
import sys
import shutil
import struct
from itertools import combinations

import numpy as np
from scipy import ndimage as ndi
from scipy.spatial import ConvexHull
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from upscale import read_groups, read_palette, read_bitmap, z_mask
from insert_depth import mounting_visibility, read_depth, yellow_ink
from route_inserts import route_insert
from purple_ramp import finish_ramp
from cabinet_sky import finish_left_galaxy
from lane_detectors import material as detector_material, seat as seat_detector

RATE=12
S=4
PI=np.pi

def clamp(v): return np.clip(v,0,1)
def color(v): return np.asarray(v,dtype=np.float32)
def rgb(v): return np.broadcast_to(v[...,None],v.shape+(3,))
def blend(a,b,k): return a+(b-a)*clamp(k)[...,None]
def smooth(v): 
    t=clamp(v)
    return t*t*(3-2*t)
def norm(x,y,z):
    mag=np.sqrt(x*x+y*y+z*z)+1.e-6
    return x/mag,y/mag,z/mag

def chromed(nx,ny,nz,rough=.07,warm=0):
    # A high-reflectance conductor in an arcade with a bright ceiling,
    # dark cabinet cavity, a pale counter/apron and two reflected light banks.
    rx=2*nx*nz; ry=2*ny*nz; rz=2*nz*nz-1
    sky=smooth((-ry+.19)*2.3)
    floor=smooth((ry-.28)*2.6)
    c=(color([23,25,30])+rgb(sky)*color([101,111,117])+rgb(floor)*color([111,111,100]))
    ribbon=.12+rough
    strip=np.exp(-((ry+.50+rx*.18)/ribbon)**4)*clamp((rz+.95)*2)
    c+=rgb(strip)*color([85,83,76])
    sidelight=np.exp(-((rx-.63)/(.10+rough))**4 - ((ry+.06)/.8)**4)
    c+=rgb(sidelight)*color([111,114,119])
    return np.clip(c+color([9,3,-8])*warm,0,255)

class Canvas:
    def __init__(self,w,h):
        self.w=w; self.h=h
        yy,xx=np.mgrid[:h*RATE,:w*RATE]
        self.x=(xx.astype(np.float32)+.5)/RATE
        self.y=(yy.astype(np.float32)+.5)/RATE
        self.a=np.zeros_like(self.x)
        self.c=np.zeros(self.x.shape+(3,),np.float32)
    def layer(self,mask,col):
        a=clamp(np.asarray(mask,dtype=np.float32))
        self.c=self.c*(1-a[...,None])+a[...,None]*col
        self.a+=a*(1-self.a)
    def disc(self,cx,cy,rx,ry):
        u=(self.x-cx)/rx; v=(self.y-cy)/ry
        return u,v,np.hypot(u,v)
    def screw(self,cx,cy,r,aspect=.82):
        u,v,n=self.disc(cx,cy,r,r*aspect)
        z=np.sqrt(clamp(1-n*n))
        metal=chromed(u,v,z,.11)*.78+24
        slot=(np.abs(v-.46*u)<.10)&(n<.61)
        metal=blend(metal,color([25,28,31]),slot*.94)
        self.layer(n<1,metal)
    def output(self):
        # Resample premultiplied channels and alpha separately to remove fringe.
        target=(self.w*S,self.h*S)
        def down(chan):
            return np.asarray(Image.fromarray(chan.astype('float32')).resize(target,Image.Resampling.LANCZOS))
        a=clamp(down(self.a)); c=np.stack([down(self.c[:,:,i]) for i in range(3)],-1)
        c=np.uint8(np.clip(c/np.maximum(.001,a[...,None])+.5,0,255))
        return Image.fromarray(c),Image.fromarray(np.uint8(a*255+.5))

def bumper(w,h,group,frame,where):
    cv=Canvas(w,h)
    contact=frame%2; level=frame//2
    # Lower rim is visible below the mushroom cap, especially at rest.
    rad=(w-1)*.488; rx=rad; ry=rad*(.72 if group<405 else .79)
    cx=(w+.35)*.50; cy=ry+.45+(.12 if contact else 0)
    if group==388:
        # In the DAT silhouettes the cap projects slightly left of the bore.
        cx=163.5-where[0]
    x,y=cv.x,cv.y
    parts=[(.79,h-1.5-.79*ry,1.20,True),(.60,cy+2.0,3.5,False),
           (.80,cy+.46,2.0,True),(.99,cy,.95,False)]
    for part,(rr,yy,height,chrome) in enumerate(parts):
        R=rr*rx; B=rr*ry
        axis=cx
        if group==388 and part<2:
            # Seat the washer on the bore axis and let the neck bridge to the cap.
            axis=(164.05 if part==0 else 163.78)-where[0]
        elif group==396:
            # The cap projects above and to the left of the stationary foot.
            # Anchor the lower bearing in table coordinates in both DAT poses.
            if part==0:
                axis=103.64-where[0]; yy=47.53-where[1]
                R,B,height=8.43,6.02,.42
            elif part==1:
                axis+=.86
        u=(x-axis)/R
        arc=np.sqrt(clamp(1-u*u))
        v=(y-yy)/B
        foot=(abs(u)<1)&(v>=-arc)&(v<=arc+height/B)
        nx,ny,nz=norm(u*.97,arc*.42,arc*.86+.001)
        side=(y-yy)-B*arc
        if chrome:
            # The seating washer is horizontal; the turned lip has radial normals.
            root=np.hypot(u,v)
            roll=clamp((root-.76)/.24)
            ax,ay,az=norm(u*roll,-.70+.65*v*roll,.80)
            face=chromed(ax,ay,az,.09,.20)*.63+rgb(np.sin(root*115)*3.5)
            arc_light=190*np.exp(-((u+.55)/.22)**2)+83*np.exp(-((u-.72)/.23)**2)
            wall=rgb(22+arc_light)+color([6,10,13])
            wall*= (.77+.24*np.cos(np.clip(side,0,height)*PI*2/max(height,.1)))[...,None]
            c=np.where((side>-.04)[...,None],wall,face)
            if group==396 and part==0:
                # A diffuse reflected strip on the front of the turned washer.
                c+=rgb(31*clamp(v+.03)*np.exp(-((u+.12)/.79)**2))
        else:
            c=color([20,25,28])+rgb(arc)*color([10,13,17])+rgb(np.exp(-((u+.53)/.19)**2))*color([45,44,41])
        under=clamp((y-cy)/max(.1,h-cy))
        c*= (.62+.38*under)[...,None]
        cv.layer(foot,c)
    u,v,r=cv.disc(cx,cy,rx,ry); angle=np.arctan2(v,u)
    theta=angle-PI/2
    # Eight sharply drawn enamel radial points are aligned to the cap axis.
    pts=[]
    for j in range(16):
        a=2*PI*j/16-PI/2
        R=.864 if j%2==0 else .484
        pts.append((np.cos(a)*R,np.sin(a)*R))
    star=polygon(u,v,pts)
    z=np.sqrt(clamp(1-r*r))
    nx,ny,nz=norm(.26*u,.26*v-.21,.93+z*.08)
    dome=clamp(1-r*r)
    illumination=.70+.24*dome-.15*v-.08*u
    overhead=np.exp(-((v+.50+u*.23)/.22)**2-((u+.24)/.78)**4)
    pearl=rgb(illumination)*color([222,219,204])+rgb(overhead)*color([34,37,41])
    lacquer=color([147,18,16] if group<405 else [24,44,93])
    printCol=rgb(.77-.20*v-.09*u)*lacquer+rgb(np.exp(-((v+.63+u*.18)/.24)**2))*color([33,31,27])
    printCol=printCol+rgb(overhead)*color([49,43,40])
    face=blend(pearl,printCol,star)
    edge_dist=polyline_distance(u,v,pts+[pts[0]])
    face=blend(face,color([240,204,159]),clamp(1-edge_dist/.024)*.69)
    face+=rgb((np.sin(141*u+4*v)*np.sin(126*v)+np.sin(97*r))*.7)
    # Thin turned-metal edge and opaque enamel inset: no star crosses a rim.
    taper=clamp((r-.88)/.12)
    ex,ey,ez=norm(u*taper,v*taper-.14,1-.8*taper)
    cv.layer(r<1,chromed(ex,ey,ez,.08,.2))
    cv.layer(r<.931,face)
    # A very fine ring and two level-specific screw heads remain fixed when fired.
    cv.layer((r>.879)&(r<.901),color([142,137,120]))
    for side in (-1,1):
        cv.screw(cx+rx*.743*side, cy+ry*.215, max(.43,rx*.054))
    # Mount the jewel in a machined concentric ferrule.
    cu,cw,cr=cv.disc(cx,cy-.13,rx*.438,ry*.438)
    cz=np.sqrt(clamp(1-cr*cr))
    cv.layer(cr<1,chromed(cu,cw,cz,.06,.45))
    cv.layer(cr<.849,color([30,31,32]))
    t=cr/.777; dz=np.sqrt(clamp(1-t*t))
    hue=color([[25,78,202],[20,142,59],[215,157,14],[201,43,21]][level])/255
    px,py,pz=norm(cu/.777,cw/.777,dz)
    col=lens_color(px,py,pz,t,hue,bool(contact))
    cv.layer(t<1,col)
    return cv.output()

def polygon(x,y,points):
    inside=np.zeros(x.shape,dtype=bool)
    a=points[-1]
    for b in points:
        dy=b[1]-a[1]
        if abs(dy)>1e-8: inside^=((a[1]>y)!=(b[1]>y))&(x<(b[0]-a[0])*(y-a[1])/dy+a[0])
        a=b
    return inside

def lens_color(nx,ny,nz,r,hue,lit):
    # Tinted polymer retains saturated edges. A bright internal LED illuminates
    # the center independently of the small, neutral reflected softbox above it.
    L=clamp((-.42*nx-.54*ny+.70*nz)*.72+.23)
    tube=np.exp(-((ny+.49+nx*.20)/.117)**2-((nx+.19)/.49)**4)
    pin=np.exp(-((nx+.38)/.17)**2-((ny+.37)/.20)**2)
    lower=np.exp(-((ny-.73)/.14)**2-((nx-.15)/.53)**2)
    facet=np.cos(12.*np.arctan2(ny,nx)+.46)*np.exp(-((r-.55)/.22)**2)
    if lit:
        filament=np.exp(-((nx-.10)**2+(ny-.15)**2)*3.5)
        c=rgb(.45+.62*nz+.21*L)*hue*211+rgb(filament)*color([60,58,46])+rgb(nz**3)*color([24,24,21])
        c+=rgb(np.maximum(0,1-np.abs(r-.78)*12))*hue*16+rgb(facet)*hue*13
    else:
        c=color([8,10,15])+rgb(.095+.46*nz+.12*L)*hue*175
        c+=rgb(lower)*hue*36+rgb(facet)*hue*7
    return np.clip(c+rgb(tube*(77 if lit else 69)+pin*(67 if lit else 72)),0,255)

def native_distance(mask,cv,blur=.48):
    # Reconstruct a continuous signed silhouette from the old object boundary.
    m=np.pad(mask.astype(bool),2)
    sdf=ndi.distance_transform_edt(m)-ndi.distance_transform_edt(~m)
    sdf=ndi.gaussian_filter(sdf,blur)
    return ndi.map_coordinates(sdf,[cv.y+1.5,cv.x+1.5],order=3,mode='nearest')

def footprint(mask):
    y,x=np.where(mask)
    return x.min(),y.min(),x.max()+1.,y.max()+1.

def tri_points(mask):
    y,x=np.where(mask)
    arr=np.stack((x+.5,y+.5),axis=1)
    outline=arr[ConvexHull(arr).vertices]
    def area(t):
        a,b,c=t
        return abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
    points=np.array(max(combinations(outline,3),key=area))
    center=points.mean(0); return center+(points-center)*1.13

def shape_distance(gid,cv,mask):
    x0,y0,x1,y1=footprint(mask)
    cx=(x0+x1)/2; cy=(y0+y1)/2; rx=(x1-x0)/2; ry=(y1-y0)/2
    x=cv.x; y=cv.y
    if gid in (*range(116,125),143,144,147,148,*range(161,164),192,217,218,219,220,221,222,223,225,226,227,228):
        p=tri_points(mask)
        # Half-plane field gives bevel normals aligned to each straight edge.
        vals=[]
        for a,b in zip(p,np.roll(p,-1,axis=0)):
            e=b-a
            v=(e[0]*(y-a[1])-e[1]*(x-a[0]))/np.linalg.norm(e)
            sign=np.sign(e[0]*(cy-a[1])-e[1]*(cx-a[0]))
            vals.append(v*sign)
        d=np.min(vals,axis=0)
        return d,(x-cx)/rx,(y-cy)/ry
    if gid==179:
        # The extra-ball insert is the diagonal cross on the original table.
        u=(x-cx)*(1./rx);v=(y-cy)*(1./ry)
        a=(u+v)*.70710678;b=(v-u)*.70710678
        da=rectangular_sdf(a,b,.99,.33,.095);db=rectangular_sdf(a,b,.33,.99,.095)
        return np.maximum(da,db)*min(rx,ry),u,v
    if gid in set(range(139,143))|set(range(173,176))|set(range(193,196))|set(range(255,261)):
        yy,xx=np.where(mask)
        points=np.stack([xx+.5,yy+.5],axis=-1); center=points.mean(0)
        _,vec=np.linalg.eigh(np.cov(points.T)); axis=vec[:,-1]
        a=(x-center[0])*axis[0]+(y-center[1])*axis[1]
        b=-(x-center[0])*axis[1]+(y-center[1])*axis[0]
        samples=(points-center)@vec
        A=np.ptp(samples[:,1])*.5+.48; B=max(1,np.ptp(samples[:,0])*.5+.34)
        corner=min(.78,B*.28)
        da=abs(a)-(A-corner); db=abs(b)-(B-corner)
        d=corner-(np.hypot(np.maximum(0,da),np.maximum(0,db))+np.minimum(np.maximum(da,db),0))
        return d,(x-cx)/rx,(y-cy)/ry
    if gid==229:
        # lite198 includes the surrounding violet plastic in its DAT mask.
        # The socket follows the red ink itself, including its perspective tilt.
        dx=x-5.08; dy=y-4.18
        u=(.9804*dx-.1972*dy)/4.54; v=(.1972*dx+.9804*dy)/3.56
        return (1-np.hypot(u,v))*3.56,u,v
    u=(x-cx)/rx; v=(y-cy)/ry
    return (1-np.hypot(u,v))*min(rx,ry),u,v

def determine_hue(rgb8,mask):
    arr=rgb8[mask].astype('float32')
    maximum=arr.max(1); chroma=maximum-arr.min(1)
    i=np.argsort(chroma*np.sqrt(maximum))[-max(2,len(arr)//3):]
    tint=arr[i].mean(0); tint=tint/max(1,tint.max())
    # Preserve orange/amber distinctions and maintain density through glass.
    return clamp((tint-.04)/.96)*.90+.027

def lamp(w,h,gid,mask,hue,lit):
    cv=Canvas(w,h)
    d,u,v=shape_distance(gid,cv,mask)
    R=max(1.,min(w,h)*.44); rim=min(.69,.115*R)
    nx,ny=np.gradient(-d,1/RATE)
    # np.gradient returns image y then x
    ex,ey,ez=norm(ny,nx,.32+clamp(d/rim))
    edge=chromed(ex,ey,ez,.12)*.53+color([15,13,12])
    cv.layer(d>-.06,edge)
    cv.layer(d>rim*.60,color([13,16,21]))
    local=clamp((d-rim)/max(.7,R*.70))
    nz=np.sqrt(clamp(local*(2-local)))
    px,py,pz=norm(ny*(1-local),nx*(1-local),np.maximum(.03,nz))
    # A single broad highlight traverses the lens, including planar facets.
    px=clamp((px*.47+u*.53+1)/2)*2-1
    py=clamp((py*.47+v*.53+1)/2)*2-1
    col=lens_color(px,py,pz,1-local,hue,lit)
    cv.layer(d>rim,col)
    return cv.output()

def segment_distance(x,y,a,b):
    dx,dy=b[0]-a[0],b[1]-a[1]
    t=clamp(((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy+.00001))
    return np.hypot(x-a[0]-t*dx,y-a[1]-t*dy)

def polyline_distance(x,y,pts):
    return np.minimum.reduce([segment_distance(x,y,a,b) for a,b in zip(pts,pts[1:])])

def tapered_paddle(w,h,source,mask,right,where):
    # Native face bearings remain fixed through all eight frames. Compute the
    # angle from the stock lit face, excluding the extruded rubber underside.
    cv=Canvas(w,h)
    r,g,b=source.astype('float32').transpose(2,0,1)
    yy,xx=np.where(mask&(g>72)&(b>70)&(r<1.7*g+15))
    _,axes=np.linalg.eigh(np.cov(np.stack((xx,yy))))
    axis=axes[:,-1]
    if (axis[0]>0)==right:axis=-axis
    ax=(233.65 if right else 132.1)-where[0]; ay=382.78-where[1]
    dx,dy=axis; length=38.25
    def profile(x,y):
        t=clamp(((x-ax)*dx+(y-ay)*dy)/length)
        X=x-(ax+t*length*dx);Y=y-(ay+t*length*dy)
        n=np.hypot(X,Y)
        return 6.10-2.63*t-n,X/(n+.001),Y/(n+.001),t
    d,nx,ny,t=profile(cv.x,cv.y)
    # Thick red rebound rubber; the molded ivory top sits above its sidewall.
    for height in np.linspace(2.64,0,12):
        edge,u,v,_=profile(cv.x,cv.y-height)
        # A satin rubber rebound band. A diffuse red shoulder sits above a
        # shaded belt and a fine molding seam on its lower half.
        sky=clamp(.59-.22*u-.34*v)
        side=rgb(.37+.64*sky)*color([182,47,37])
        side+=rgb(np.exp(-((height-.76)/.62)**2)*(.50+.50*sky))*color([35,12,7])
        side-=rgb(np.exp(-((height-2.22)/.21)**2))*color([16,4,2])
        cv.layer(edge>=0,side)
    facing=clamp(.30-.37*nx-.79*ny)
    cv.layer(d>.03,color([107,22,22])+rgb(facing)*color([125,47,30]))
    cv.layer(d>.66,color([112,103,89])+rgb(facing)*color([65,62,48]))
    # Cross-section of a slightly crowned injection-molded cap. Its shoulder
    # rounds into the top, producing a broad softbox reflection on the
    # light-facing rim and a shaded roll on the opposite side.
    radius=6.10-2.63*t
    shoulder=np.exp(-np.maximum(d-.80,0)/1.38)
    curved=clamp(1-d/np.maximum(1.,radius))**1.36
    axis_distance=np.hypot((cv.x-ax)/6.2,(cv.y-ay)/5.65)
    axis_round=np.exp(-((axis_distance-.66)/.25)**2)
    wash=np.exp(-((d-1.57)/.89)**2)*facing
    edge_kick=np.exp(-((d-.91)/.27)**2)*(.20+.80*facing)
    light=.97-.19*shoulder+(.25*facing-.082)*curved-.026*t
    face=rgb(light)*color([211,205,189])
    face+=rgb(wash)*color([45,45,42])+rgb(edge_kick)*color([42,42,38])
    face+=rgb(axis_round*(facing-.45))*color([18,17,15])
    cv.layer(d>.81,face)
    # An inset washer gives scale to the bearing and catches the cabinet
    # light without turning the whole ivory face into polished metal.
    u,v,r=cv.disc(ax,ay,1.02,.83)
    cv.layer(r<=1,rgb(.62+.23*clamp(.45-u*.27-v*.49))*color([178,166,142]))
    cv.screw(ax,ay,.70,.77)
    return cv.output()

def plunger(w,h,where,head_y=None):
    # The seat and axis are stationary in table space. The eight DAT frames
    # displace the piston head by 24 pixels; a real four-and-a-half turn helix
    # compresses into the eleven native pixels visible at full pull.
    cv=Canvas(w,h); gy=cv.y+where[1]
    x=cv.x+where[0]-(327.15+.147*(gy-381))
    head=(where[1] if head_y is None else head_y)+.18; upper=head+1.72; seat=414.24
    R=3.08; turns=4.5; pitch=(seat-upper)/turns
    rod=x/.83; rz=np.sqrt(clamp(1-rod*rod))
    column=(abs(rod)<1)&(gy>=head+.5)
    cv.layer(column,color([27,33,39])+rgb(rz)*color([75,84,90])+rgb(np.exp(-((rod+.39)/.23)**2))*color([78,81,82]))
    # Render back half-turns before the front half. The spaces between the
    # turns are transparent, except for the slender piston passing through.
    u=x/R; arc=np.sqrt(clamp(1-u*u)); phase=np.arcsin(np.clip(u,-1,1))/(2*PI)
    wire_radius=min(.61,pitch*.25)
    for front in (False,True):
        zsign=1. if front else -1.
        angle=phase if front else .5-phase
        cy=upper+(angle+.24)*pitch+zsign*.37*arc
        k=np.clip(np.rint((gy-cy)/pitch),0,turns-.5)
        v=(gy-cy-k*pitch)/wire_radius
        nz=np.sqrt(clamp(1-v*v))
        shade=clamp(.26+.58*nz-.18*np.clip(v,-1,1)+.14*arc)
        co=rgb(shade)*color([159,171,177])+color([23,26,31])
        co+=rgb(np.exp(-((v+.33)/.29)**2))*color([56,60,64])
        co+=rgb(np.exp(-((u+.58)/.26)**2)*nz)*color([24,22,18])
        if not front:co=co*.43+color([8,11,15])
        cv.layer((abs(u)<1)&(abs(v)<=1)&(gy>=upper-.1)&(gy<=seat+.06),co)
    for cy,width in ((upper-.27,3.22),(seat+.19,3.32)):
        v=(gy-cy)/.54; u=x/width
        col=rgb(53+106*clamp(1-v*v)+59*np.exp(-((u+.44)/.27)**2))+color([4,8,11])
        cv.layer((abs(u)<1)&(abs(v)<1),col)
    tip=(gy>=head)&(gy<upper-.54)&(abs(x)<2.73)
    cv.layer(tip,color([76,32,29])+rgb(np.sqrt(clamp(1-(x/2.74)**2)))*color([73,39,34]))
    return cv.output()


def plunger_motion(groups, out):
    # SPR1: little-endian frame count, scale (u16), then the 105 intermediate
    # poses in order. Each pose stores x/y offsets from the rest frame and w/h
    # (u16 native pixels) followed by unassociated, top-down BGRA8 at 4x.
    _, _, bx, by = read_bitmap(groups[107][1][0])
    keys = []
    for group in groups[473:481]:
        bitmap, _, x, y = read_bitmap(group[1][0])
        h, w = bitmap.shape
        keys.append(np.array([x-bx, y-by, x-bx+w, y-by+h], dtype=float))
    with (out/'plunger-motion.bin').open('wb') as stream:
        stream.write(struct.pack('<4s2H', b'SPR1', 113, S))
        for lower, upper in zip(keys, keys[1:]):
            for substep in range(1, 16):
                box = lower + (upper-lower)*(substep/16)
                left, top = np.floor(box[:2]).astype(int)
                right, bottom = np.ceil(box[2:]).astype(int)
                w, h = right-left, bottom-top
                image, mask = plunger(w, h, (left,top), head_y=box[1])
                pixels = np.empty((h*S,w*S,4), dtype=np.uint8)
                pixels[:,:,:3] = np.asarray(image)[:,:,::-1]
                pixels[:,:,3] = np.asarray(mask)
                stream.write(struct.pack('<4H', left-int(keys[0][0]), top-int(keys[0][1]), w, h))
                stream.write(pixels.tobytes())

def rectangular_sdf(x,y,hx,hy,r):
    u=abs(x)-hx+r;v=abs(y)-hy+r
    return r-np.hypot(np.maximum(u,0),np.maximum(v,0))-np.minimum(np.maximum(u,v),0)

# Center of each slot in group 107; axis goes from one target to the next.
def target_geometry(gid):
    banks=[((252.32,189.50),(0.90,10.22),(0.35,4.23),(2.18,-4.80)),
           ((166.93,139.62),(10.10,2.91),(4.12,1.16),(-.64,-6.14)),
           ((138.0,78.48),(8.54,-4.14),(3.78,-1.82),(-.68,-6.32))]
    bank,n=divmod(gid-432,3);c,step,a,rise=banks[bank]
    return np.array(c)+n*np.array(step),np.array(a),np.array(rise)

def drop_target(w,h,gid,where):
    cv=Canvas(w,h)
    center,along,up=target_geometry(gid); center-=where
    right_bank=432<=gid<=434
    if right_bank:
        # Seat the raised face a fraction lower in its existing slot. In
        # particular the 7 x 14 middle DAT frame needs room for the top
        # bevel and the dark return underneath, with the full face width.
        center+=np.array((-.20,.45))
    # A projected yellow blank. Coordinates follow bank orientation and the
    # physical pop-up direction; PCA would rotate these nearly-square faces.
    def uv(cx,cy):
        dx=cv.x-cx;dy=cv.y-cy
        det=along[0]*up[1]-along[1]*up[0]
        return ((dx*up[1]-dy*up[0])/det,
                (along[0]*dy-along[1]*dx)/det)
    u,v=uv(*center); length=np.linalg.norm(along)
    def face(u,v):return rectangular_sdf(u*length,(v-.50)*5.7,length,2.85,.43)
    # Stem visible beneath the face, then two charcoal/amber bevels.
    stem_x,stem_y=(.15,.46) if right_bank else (.30,.82)
    dv=face(*uv(center[0]+stem_x,center[1]+stem_y))
    cv.layer(dv>0,color([55,49,41]))
    d=face(u,v)
    cv.layer(d>0,rgb(.85+.11*v-.08*u)*color([172,112,18]))
    shade=.94+.055*v-.04*u+.035*np.exp(-((v-.88)/.20)**2)
    cv.layer(d>.24,rgb(shade)*color([246,196,44]))
    cv.layer((d>.23)&(d<.46)&(v>.6),color([255,221,96]))
    return cv.output()

def spinner(w,h,gid,where,phase_index=None):
    cv=Canvas(w,h); second=gid>=489
    j=gid-(489 if second else 481) if phase_index is None else phase_index
    center=np.array((256.55,136.85) if second else (74.17,108.95))-where
    a=np.array((.891,.454) if second else (1.,.012)); a/=np.linalg.norm(a)
    n=np.array((-a[1],a[0])); xx=cv.x-center[0];yy=cv.y-center[1]
    u=xx*a[0]+yy*a[1]; v=xx*n[0]+yy*n[1]
    phase=(1-j)*PI/4; co=np.cos(phase);si=np.sin(phase)
    # The axle and its two bearings never change their table-space position.
    half=6.05; support= ([(2.25,3.0),(0.32,10.0),(23.1,12.8),(22.2,19)] if second
                          else [(1.62,6.1),(3.60,13.1),(17.8,6.25),(20.1,13.1)])
    origin=np.array((243,129) if second else (65,103))-where
    for p,q in ((support[0],support[1]),(support[2],support[3])):
        ds=segment_distance(cv.x,cv.y,np.array(p)+origin,np.array(q)+origin)
        cv.layer(ds<=.58, rgb(92+70*np.sqrt(clamp(1-(ds/.59)**2)))+color([9,12,13]))
    shaft=(abs(u)<(12.1 if second else 8.5))&(abs(v)<.49)
    cv.layer(shaft,rgb(87+89*np.sqrt(clamp(1-(v/.5)**2))-.32*v*40)+color([7,10,12]))
    for pos in (-(half+.36),half+.36):
        cv.layer((abs(u-pos)<.50)&(abs(v)<.78),color([117,106,77])+rgb(np.exp(-((v+.24)/.26)**2))*color([98,95,75]))
    half_h=max(.53,abs(si)*5.72+.41*abs(co))
    shear=.205*co*si
    d=rectangular_sdf(u-shear*v,v,half,half_h,min(.38,half_h*.32))
    cv.layer(d>=-.12,color([132,127,113]))
    diffuse=.86+.10*co-.025*np.clip(v/half_h,-1,1)
    cv.layer(d>.16,rgb(diffuse)*color([248,244,228]))
    return cv.output()

def spinner_atlases(groups, out):
    _, _, bx, by = read_bitmap(groups[107][1][0])
    for first in (481, 489):
        # The fully face-on original encloses the complete fixed axle, posts
        # and rotating paddle. Every subframe shares this one table rectangle.
        native, _, x, y = read_bitmap(groups[first+3][1][0])
        h, w = native.shape
        where = np.array((x-bx, y-by))
        dimensions = (w*S*8, h*S*4)
        albedo = Image.new('RGB', dimensions)
        coverage = Image.new('L', dimensions)
        for index in range(32):
            face, mask = spinner(w, h, first, where, index/4)
            corner = ((index%8)*w*S, (index//8)*h*S)
            albedo.paste(face, corner)
            coverage.paste(mask, corner)
        write_asset(out, f'spinner-{first}', (albedo, coverage))

def bolt_path(rng,start,end,amplitude,steps):
    a=np.array(start); b=np.array(end); v=b-a
    perp=np.array([-v[1],v[0]])/np.linalg.norm(v)
    t=np.linspace(0,1,steps+1)
    envelope=np.sin(PI*t)**.72
    return (a+v*t[:,None]+np.outer(rng.uniform(-amplitude,amplitude,len(t))*envelope,perp))

def slingshot(w,h,right,lit,variant=0):
    cv=Canvas(w,h)
    X=w-cv.x if right else cv.x; Y=cv.y
    pts=[(8.0,5.4),(4.0,48.1),(28.8,63.9)]
    inside=polygon(X,Y,pts)
    dist=polyline_distance(X,Y,pts+[pts[0]])
    d=np.where(inside,dist,-dist)+3.04
    nx=np.gradient(-d,axis=1)*RATE; ny=np.gradient(-d,axis=0)*RATE
    px,py,pz=norm(nx,ny,.50)
    cv.layer(d>=0,chromed(px,py,pz,.12)*.64+13)
    cv.layer(d>.57,rgb(.61+.28*clamp((-nx-ny)*.6+.5))*color([205,37,29])+color([7,8,9]))
    cv.layer(d>1.70,color([19,20,28]))
    dust=2*np.sin(X*6.41+Y*4.91)*np.sin(Y*13.35-X*3.31)
    plate=color([12,26,46])+rgb(.32+.52*smooth((Y+X*.52-21)/38))*color([17,24,36])+rgb(dust)
    inset=np.exp(-((d-2.90)/.115)**2)+.40*np.exp(-((d-3.3)/.15)**2)
    etched=clamp((Y-10)/22)*clamp((X-3)/12)
    plate+=rgb(inset)*color([28,36,45])+rgb(np.exp(-((Y-X*.4-21)/7.5)**2))*color([5,7,12])
    plate+=rgb(etched*np.sin((Y-X*.35)*5.3))*color([1.2,1.5,1.6])
    cv.layer(d>2.00,plate)
    # The unpowered artwork is a delicate etched trace. Three electrical
    # exposures branch from fixed electrodes across the glazed panel.
    rng=np.random.default_rng(442+variant*47+(9 if right else 0))
    def p(a,b,amp,n):return bolt_path(rng,a,b,amp if lit else amp*.64,n)
    A=p((7.1,13.0),(16.0,36.1),1.68,11)
    B=p((16.,36.1),(5.45,48.1),1.90,9)
    C=p((5.45,48.1),(24.6,57.1),1.55,8)
    D=p((16.,36.1),(17.0,28.7),1.06,5)
    E=p(A[7],(7.0,32.9),1.45,5)
    F=p(C[4],(14.,52.),.72,4)
    main=np.minimum.reduce([polyline_distance(X,Y,z) for z in [A,B,C]])
    branch=np.minimum.reduce([polyline_distance(X,Y,z) for z in [D,E,F]])
    if lit:
        halo=np.exp(-main*main/12)*.76+np.exp(-branch*branch/3)*.28
        glow=plate+rgb(halo)*color([44,71,164])
        glow+=rgb(np.exp(-(main/.73)**2))*color([116,155,194])+rgb(np.exp(-(main/.25)**2))*color([159,139,82])
        glow+=rgb(np.exp(-(branch/.31)**2))*color([94,130,184])
        spark=np.exp(-((X-16)**2+(Y-36.1)**2)/3.7)
        glow+=rgb(spark)*color([68,106,178])
        cv.layer(d>2,np.clip(glow,0,255))
    else:
        etch=np.exp(-(main/.33)**2)*.35+np.exp(-(branch/.16)**2)*.26
        cv.layer((d>2)*etch,color([59,86,121]))
    for cx,cy,rr in [(8.0,5.4,1.65),(28.8,63.9,1.75)]:
        cv.screw(w-cx if right else cx,cy,rr)
    return cv.output()

def contact_band(w,h,right):
    """Tensioned rubber viewed beside the slingshot's inclined cover.

    The DAT animation includes the hidden lever, red end tags and the post
    behind the cover. Only the smoothly deflected outer rubber is visible.
    Trace its two ends under the same authored cover contour as groups 180/181.
    """
    cv=Canvas(w,h)
    x=w-cv.x if right else cv.x
    y=cv.y
    points=np.array([
        ((.10,1.1),(2.2,3.8),(9.5,12.1),(13.7,17.8)),
        ((13.7,17.8),(15.4,20.0),(15.8,21.0),(16.02,24.4)),
        ((16.02,24.4),(16.5,31.9),(16.8,37.8),(16.0,44.8))])
    t=np.linspace(0,1,25)[:,None]
    weights=np.hstack(((1-t)**3,3*(1-t)**2*t,3*(1-t)*t**2,t**3))
    path=np.concatenate([weights@p if i==0 else (weights@p)[1:] for i,p in enumerate(points)])
    distance=np.full(x.shape,np.inf)
    side=np.zeros_like(x)
    for (a,b),(c,d) in zip(path,path[1:]):
        dx,dy=c-a,d-b
        along=clamp(((x-a)*dx+(y-b)*dy)/(dx*dx+dy*dy))
        vx=x-(a+along*dx); vy=y-(b+along*dy)
        squared=vx*vx+vy*vy
        near=squared<distance
        side=np.where(near,(vx*dy-vy*dx)/np.hypot(dx,dy),side)
        distance=np.minimum(distance,squared)
    radius=1.62
    u=side/radius
    nz=np.sqrt(clamp(1-u*u))
    nx=u if right else -u
    base=rgb(.53+.37*nz+.10*nx)*color([226,223,207])
    speckle=(np.sin(x*18+y*11)*np.cos(y*14-x*12))*.85
    color_band=base+rgb(speckle)+rgb(np.exp(-((nx+.18)/.40)**2))*color([15,15,17])
    # Positions in the left cover mesh. Reflect the right assembly before clipping.
    plate_x=x+12; plate_y=y+11
    triangle=[(8.0,5.4),(4.0,48.1),(28.8,63.9)]
    boundary=polyline_distance(plate_x,plate_y,triangle+[triangle[0]])
    visible=(~polygon(plate_x,plate_y,triangle)) & (boundary>=3.04)
    cv.layer((distance<radius**2)&visible,color_band)
    return cv.output()

def rebound(w,h,mask,source):
    cv=Canvas(w,h)
    d=native_distance(mask,cv,.63)
    # A resilient pale contact strip on a red insulated base. Match the
    # individual electrode layout revealed in each original plate strike.
    nx=np.gradient(-d,axis=1)*RATE; ny=np.gradient(-d,axis=0)*RATE
    nx,ny,nz=norm(nx,ny,1.4*clamp(d/2)+.15)
    rr=ndi.map_coordinates((source[:,:,0].astype('float32')-source[:,:,1].astype('float32'))/100,
                           [cv.y-.5,cv.x-.5],order=1,mode='nearest')
    rub=rgb(.59+.25*nz-.19*ny-.13*nx)*color([242,235,209])+color([12,12,14])
    co=blend(rub,color([153,43,30]),smooth((rr-.30)*2.3))
    cv.layer(d>0,co)
    return cv.output()

def component_frame(cv,mask):
    yy,xx=np.where(mask)
    pts=np.stack((xx+.5,yy+.5),axis=-1)
    _,axis=np.linalg.eigh(np.cov(pts.T))
    proj=pts@axis
    lo=proj.min(axis=0); hi=proj.max(axis=0)
    r=(hi-lo)/2+.44; center=(hi+lo)/2
    a=(cv.x*axis[0,1]+cv.y*axis[1,1]-center[1])/r[1]
    b=(cv.x*axis[0,0]+cv.y*axis[1,0]-center[0])/r[0]
    return a,b,r

def switch_hardware(w,h,gid,mask,source):
    cv=Canvas(w,h); native=native_distance(mask,cv,.55)
    dy,dx=np.gradient(-native,1/RATE)
    nx,ny,nz=norm(dx,dy,.25+clamp(native*.69))
    wire=chromed(nx,ny,nz,.09)
    cv.layer(native>0,wire)
    src=source.astype('float32'); r,g,b=src[...,0],src[...,1],src[...,2]
    if gid in (501,502):
        a,t,_=component_frame(cv,mask)
        sheen=rgb(.46+.14*smooth(1-abs(t)))
        cv.layer(native>0,sheen*wire+rgb(np.exp(-((t+.35)/.21)**2))*color([95,98,99]))
    elif gid==500:
        x0,y0,x1,y1=footprint(mask); u,v,rr=cv.disc((x0+x1)/2,(y0+y1)/2,(x1-x0)/2,(y1-y0)/2)
        nz=np.sqrt(clamp(1-rr*rr))
        cv.layer(rr<1,chromed(u,v,nz,.15))
    elif gid==537:
        return green_lens(w,h,True)
    elif 505<=gid<=508:
        xx=cv.x; yy=cv.y
        iy,ix=np.where(mask); slope,intercept=np.polyfit(iy+.5,ix+.5,1)
        u=(xx-intercept-slope*yy)/2.55; t=(yy-iy.min())/(iy.max()-iy.min()+1)
        coat=rgb(.50+.31*np.sqrt(clamp(1-u*u)))*color([55,34,35])
        coil=np.exp(-((np.sin(PI*(t*13-u*.22)))/.45)**2)
        cv.layer((native>0)&(t>(.41 if gid%2==0 else .17)),coat+rgb(coil)*color([49,28,24]))
    elif 442<=gid<=467:
        face=mask&(r>g+20)
        if face.sum()>3:
            a,t,R=component_frame(cv,face)
            radius=np.hypot(a,t);dist=(1-radius)*R[0]
            face_color=color([183,34,37]);dots=np.exp(-((radius-.53)/.075)**2)*.37
            cv.layer(dist>-.10,color([72,21,25]))
            bevel=smooth((dist-.17)/.74)
            light=.77+.18*bevel-.08*t-.07*a
            col=rgb(light)*face_color+rgb(np.exp(-((t+.48+.13*a)/.18)**2))*color([42,36,24])
            col=blend(col,color([109,28,30]),dots*.68)
            cv.layer(dist>.22,col)
    return cv.output()

def green_lens(w,h,lit):
    cv=Canvas(w,h); u,v,r=cv.disc(6.56,4.97,5.72,3.98)
    nz=np.sqrt(clamp(1-r*r))
    ring=chromed(u,v,nz,.12)*.55+color([7,17,11])
    cv.layer(r<1,ring)
    c=color([3,22,14])+rgb(.20+.55*nz)*color([14,109,46])
    if lit:c+=rgb(.37+.60*nz)*color([20,120,45])
    c+=rgb(np.exp(-((u+.22)/.38)**2-((v+.40+.15*u)/.19)**2))*color([59,68,60])
    c+=rgb(np.exp(-((u-.03)/.45)**2-((v-.58)/.18)**2))*color([3,18,7])
    cv.layer(r<.89,np.clip(c,0,255))
    return cv.output()

def frame_sides(y):
    return 71.78-.1651*y,294.0+.1628*y

def table_coverage(valid):
    # Keep the native depth silhouette for the hidden hardware below the apron.
    # Along the two visible sidewalls use the actual straight metal edges.
    h,w=valid.shape
    yy,xx=np.mgrid[:h*S,:w*S].astype('float32')
    x=(xx+.5)/S; y=(yy+.5)/S
    native=ndi.map_coordinates(valid.astype('float32'),(y-.5,x-.5),order=1,mode='nearest')
    alpha=clamp((native-.30)/.40)
    left,right=frame_sides(y)
    side=np.minimum(x-left,right-x)
    # A subpixel bevel at the cut metal edge also resolves under fractional
    # cabinet scaling. Grain and the inner bevel retain their native sharpness.
    edge=smooth(.5+side/.65)
    return Image.fromarray(np.uint8(np.where((y<427)&(side<2),edge,alpha)*255+.5))

def refinish_table(board):
    # All coordinates are measured on the original 365 x 470 table. Each
    # local patch is sampled on its final artwork grid and kept registered.
    def patch(box,paint):
        box=tuple(int(z*S) for z in box)
        base=np.asarray(board.crop(box)).copy().astype('float32')
        yy,xx=np.mgrid[box[1]:box[3],box[0]:box[2]].astype('float32')
        out=paint((xx+.5)/S,(yy+.5)/S,base)
        board.paste(Image.fromarray(np.uint8(np.clip(out+.5,0,255))),box[:2])
    def ink(bg,col,coverage):return blend(bg,col,np.asarray(coverage,dtype='float32'))
    # Register the painted socket to the centers and radii of its source DAT
    # bore. The warp falls off into the surrounding print before the lamps.
    def upper_socket(x,y,bg):
        dx=x-103.67;dy=y-47.90
        r=np.hypot(dx/8.88,dy/6.46)
        influence=smooth((1.85-r)/.63)
        offset_x=(104.25+dx*(9.5/8.88)-x)*influence
        offset_y=(49.28+dy*(7.65/6.46)-y)*influence
        yy,xx=np.indices(x.shape,dtype='float32')
        lookup=(yy+offset_y*S,xx+offset_x*S)
        return np.stack([ndi.map_coordinates(bg[:,:,i],lookup,order=1,mode='nearest')
                         for i in range(3)],-1)
    patch((87,35,122,64),upper_socket)
    def dome(x,y,bg):
        u=(x-182.72)/36.73;v=(y-269.50)/34.55;r=np.hypot(u,v)
        # Recover the fine engraved branches in the registered field painting.
        # Normalized filtering keeps the coloured lenses out of the relief and
        # suppresses the old halftone dots without flattening the radial print.
        red,green,blue=np.moveaxis(bg,2,0)
        glass=(green>red*1.22+2)&(blue>red*1.27+4)&(r<.982)
        luminance=.21*red+.66*green+.13*blue
        def glass_detail(sigma):
            weights=ndi.gaussian_filter(glass.astype('float32'),sigma*S)
            ink=ndi.gaussian_filter(luminance*glass,sigma*S)
            return ink/np.maximum(weights,.001)
        soft=glass_detail(.39)
        coarse=glass_detail(2.5)
        weight=glass*ndi.gaussian_filter(glass.astype('float32'),.44*S)
        # Hairline engravings persist along one direction; isolated dots in
        # the original screen print do not. Use the continuity of the relief
        # to preserve the branching lines without restoring the halftone.
        groove=np.clip(coarse-soft,0,38)
        reach=1.13*S
        along=np.zeros_like(groove)
        for angle in np.arange(8)*np.pi/8:
            shift=(np.sin(angle)*reach,np.cos(angle)*reach)
            a=ndi.shift(groove,shift,order=1,mode='nearest')
            b=ndi.shift(groove,(-shift[0],-shift[1]),order=1,mode='nearest')
            along=np.maximum(along,np.minimum(a,b))
        relief=(np.clip(soft-coarse,-38,24)*.31-np.minimum(groove,along)*.88)*weight
        relief+=np.clip(luminance-soft,-15,14)*.10*weight
        radius=(u-.015)**2+(v+.035)**2
        attenuation=np.exp(-2.62*radius)
        # The central lamp is about nine native pixels across. Its diffuser
        # retains a broad luminous shoulder through the next 20–25 pixels.
        halo=np.exp(-2.55*radius)
        grad=color([6,25,34])+rgb(attenuation)*color([30,96,126])+rgb(halo)*color([11,28,34])
        grad+=rgb(relief)*color([.43,.91,1.04])
        grad+=rgb(np.exp(-((r-.943)/.021)**2))*color([4,7,8])
        return ink(bg,grad,smooth((1-r)*32))
    patch((144,233,221,306),dome)
    # Three down-state banks. Each recessed housing has thin, intact yellow
    # target tops flush with its guide. These remain on group 107 when the
    # original drop-target component removes its raised sprite.
    for first in (432,435,438):
        first_center,axis,_=target_geometry(first);last_center,_,_=target_geometry(first+2)
        mid=(first_center+last_center)*.5; a=axis/np.linalg.norm(axis); normal=np.array((-a[1],a[0]))
        span=np.linalg.norm(last_center-first_center)*.5+np.linalg.norm(axis)+1.16
        half=2.73 if first==432 else 2.49
        def slot(x,y,bg):
            u=(x-mid[0])*a[0]+(y-mid[1])*a[1];v=(x-mid[0])*normal[0]+(y-mid[1])*normal[1]
            d=rectangular_sdf(u,v,span,half,.37)
            metal=rgb(112+39*np.exp(-((v+.94)/.76)**2)-32*np.tanh(v*.74))+color([14,7,-7])
            res=ink(bg,metal,smooth((d+.1)*3.5))
            res=ink(res,color([19,22,23]),smooth((d-.36)*3.4))
            for gid in range(first,first+3):
                c,along,_=target_geometry(gid)
                du=(x-c[0])*a[0]+(y-c[1])*a[1];dv=(x-c[0])*normal[0]+(y-c[1])*normal[1]
                ds=rectangular_sdf(du,dv,np.linalg.norm(along)+.06,1.25,.20)
                res=ink(res,color([103,71,23]),smooth((ds+.05)*4))
                top=rgb(.92+.036*np.cos(du/4)-.05*dv)*color([232,190,48])
                res=ink(res,top,smooth((ds-.23)*4))
            return res
        left=np.minimum(first_center,last_center)-abs(a)*7-4
        right=np.maximum(first_center,last_center)+abs(a)*7+4
        patch((np.floor(left[0]),np.floor(left[1]),np.ceil(right[0]),np.ceil(right[1])),slot)
    # Redraw the entire orange mounting ring under the green rollover. Its
    # clean glass and jewelry replace the decorative AI pattern at rest.
    def green_base(x,y,bg):
        u=(x-109.56)/10.20;v=(y-86.97)/6.82;r=np.hypot(u,v)
        res=ink(bg,color([19,18,20]),smooth((1.035-r)*24))
        val=117-.34*v*80-.24*u*65+60*np.exp(-((r-.90)/.055)**2)
        res=ink(res,rgb(val)*color([1.20,.75,.33])+color([37,18,3]),smooth((.984-r)*38))
        return ink(res,color([18,31,22]),smooth((.68-r)*32))
    patch((97,79,123,95),green_base)
    green,mask=green_lens(13,10,False);board.paste(green,(103*S,82*S),mask)
    # Dark steel channel behind the spring. Matte walls and a narrow floor
    # keep even the compressed turns clearly separated from the piston.
    def well(x,y,bg):
        u=x-(327.15+.147*(y-381))
        d=rectangular_sdf(u,y-396.9,3.9,16.15,.55)
        rail=rgb(74+61*np.exp(-((u+3.60)/.31)**2))+color([5,8,12])
        res=ink(bg,rail,smooth((d+.04)*3.5))
        floor=color([26,27,28])+rgb(9*np.exp(-((u+2.48)/.56)**2)+4*np.cos(u*.83))
        return ink(res,floor,smooth((d-.46)*3.5))
    patch((320,380,338,415),well)
    # Outer brushed-metal rails are measured straight lines, independent of
    # texture frequencies. Freshly machined top/bottom bevels meet at mitres.
    def frame(x,y,bg):
        left,right=frame_sides(y)
        sideWidth=4.75+.0083*y
        leftDist=x-left;rightDist=right-x
        side=np.minimum(leftDist,rightDist)
        # Clear the previous warped outer silhouette only around its perimeter.
        use=(y<427)&(side<sideWidth+2.25)
        res=ink(bg,color([8,9,11]),use*smooth((sideWidth+2.25-side)*2))
        def steel(t,grainAxis,across):
            val=88+56*np.exp(-((t-.37)/.34)**2)+32*t
            val+=82*np.exp(-((t-.115)/.049)**2)-43*np.exp(-((t-.91)/.075)**2)
            val+=2.30*np.sin(across*17.4+.29*np.sin(grainAxis*.27))+1.2*np.sin(across*51.1+grainAxis*.91)
            return rgb(val)+color([7.5,7,6])
        t=side/sideWidth
        # Supply the metal color through the fringe; the coverage map alone
        # determines the outline against black.
        res=ink(res,steel(clamp(t),y,side),smooth((side+0.7)*4)*smooth((sideWidth-side)*4)*(y<427))
        topheight=12.22
        topmask=smooth((x-left)*3)*smooth((right-x)*3)*smooth((topheight-y)*3)*(y>=0)
        topmetal=steel(y/topheight,x,y)
        e=np.minimum((x-left)/sideWidth,(right-x)/sideWidth)
        res=ink(res,topmetal,topmask*smooth((e-y/topheight)*14))
        lowMask=smooth((x-left)*3)*smooth((right-x)*3)*smooth((y-420.65)*4)*smooth((427.00-y)*4)
        lower=steel((y-420.65)/6.35,x,y)
        res=ink(res,lower,lowMask*smooth((e-(427-y)/6.35)*14))
        for cx,cy in ((74.8,3.8),(291.2,3.8),(5.1,423.87),(358.5,423.87)):
            U=(x-cx)/1.65;V=(y-cy)/1.17;q=np.hypot(U,V)
            nz=np.sqrt(clamp(1-q*q));alloy=chromed(U,V,nz,.12)*.77+9
            alloy=ink(alloy,color([35,37,38]),smooth((.098-abs(U*.40+V))*10)*smooth((.65-q)*13))
            res=ink(res,alloy,smooth((1-q)*11))
        return res
    old_lower=np.asarray(board.crop((0,425*S,365*S,433*S))).copy()
    floor=old_lower[-2:].mean(axis=0)
    gy,gx=np.mgrid[:old_lower.shape[0],:old_lower.shape[1]].astype('float32')
    gy=425+(gy+.5)/S;gx=(gx+.5)/S
    coverage=(gy>=426.75)*(abs(gx-(327.15+.147*(gy-381)))>=2.3)*smooth((432-gy)/1.2)
    old_lower=np.uint8(np.clip(ink(old_lower.astype('float32'),floor,coverage)+.5,0,255))
    board.paste(Image.fromarray(old_lower),(0,425*S))
    # Keep temporary paint buffers small even on a CPU-only asset workstation.
    for top in range(0,428,32):
        patch((0,top,365,min(428,top+32)),frame)

def write_asset(out,name,texture):
    albedo,coverage=texture
    albedo.save(out/f'{name}.bmp'); coverage.convert('RGB').save(out/f'{name}-mask.bmp')

def replace_insert(board,art,where,native_mask,visibility=None,yellow_socket=False,rollover_lane=None,right_bank=False):
    # The registered restoration can differ by around one native texel. Clean
    # the tiny apron surrounding the socket before seating its new hardware,
    # so an old painted bulb never shows through an antialiased silhouette.
    albedo,alpha=art
    x,y=where; pad=10
    window=(max(0,x-pad),max(0,y-pad),min(board.width,x+albedo.width+pad),min(board.height,y+albedo.height+pad))
    ox,oy=x-window[0],y-window[1]; original=board.crop(window); im=np.asarray(original).copy()
    occupied=np.zeros(im.shape[:2],bool)
    gap=np.repeat(np.repeat(native_mask,S,0),S,1)| (np.asarray(alpha)>8)
    occupied[oy:oy+gap.shape[0],ox:ox+gap.shape[1]]=gap
    cleaned=ndi.distance_transform_edt(~occupied)<(1.32*S)
    if right_bank:
        # The original artwork has a narrow strip of blue playfield between
        # these two lamp bezels and the straight corner of the brass holder.
        # Keep the mounting, including its shadow, out of the lamp cleanup.
        yy,xx=np.indices(cleaned.shape)
        by=(yy+window[1]+.5)/S; bx=(xx+window[0]+.5)/S
        holder=(by>=182.25)&(by<=217.75)&(bx>=246.95+.079*(by-183))
        cleaned &= ~holder
    if yellow_socket:
        printed=yellow_ink(im) & (ndi.distance_transform_edt(~occupied)<1.5*S)
        if printed.any():
            cleaned=ndi.distance_transform_edt(~printed)<.92*S
    if np.any(~cleaned):
        donors=~cleaned
        if yellow_socket or right_bank:
            r,g,b=np.moveaxis(im.astype(float),2,0)
            floor=donors & (b>r*.83) & (b>g*1.18)
            if np.count_nonzero(floor)>=8: donors=floor
        ix=ndi.distance_transform_edt(~donors,return_distances=False,return_indices=True)
        patch=im[tuple(ix)]
        for k in range(3):patch[:,:,k]=ndi.gaussian_filter(patch[:,:,k].astype('float32'),1.1)
        im[cleaned]=patch[cleaned]
        board.paste(Image.fromarray(im),window[:2])
    if rollover_lane:
        # These two sling aprons end at the detector lane. Borrowing the
        # detector's bright rim as a floor colour smeared chrome up to the
        # neighbouring red guide. Reuse the registered floor underneath the
        # new sling in this strip; its own alpha still defines the guide edge.
        rx,ry,rw,rh=rollover_lane
        region=(rx*S,ry*S,(rx+rw)*S,(ry+rh)*S)
        area=(region[0]-window[0],region[1]-window[1],region[2]-window[0],region[3]-window[1])
        left=max(0,area[0]); top=max(0,area[1])
        right=min(original.width,area[2]); bottom=min(original.height,area[3])
        if left<right and top<bottom:
            lane=np.array(original.crop((left,top,right,bottom)))
            red,green,blue=np.moveaxis(lane.astype('int16'),2,0)
            floor=(blue>red+6)&(blue>green+2)
            # The original raised sling was fractionally wider. Fill its
            # exposed old red outline with neighbouring playfield blue before
            # laying down the precisely straight modern cover.
            px=(window[0]+left+np.arange(right-left)+.5)/S
            old_guide=px>97 if rx==89 else px<267.8
            worn=~floor & old_guide[None,:]
            if floor.any():
                donors=ndi.distance_transform_edt(~floor,return_distances=False,return_indices=True)
                lane[worn]=lane[tuple(donors)][worn]
            board.paste(Image.fromarray(lane),(window[0]+left,window[1]+top))
    board.paste(albedo,where,alpha)
    if visibility is not None:
        start_x,start_y=pad-ox,pad-oy
        keep=visibility.crop((start_x,start_y,start_x+original.width,start_y+original.height))
        board.paste(Image.composite(board.crop(window),original,keep),window[:2])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dat',type=Path);parser.add_argument('base',type=Path);parser.add_argument('out',type=Path)
    parser.add_argument('--board',type=Path);parser.add_argument('--preview',type=Path)
    parser.add_argument('--switches-only',action='store_true',help='Refresh moving switches, flags and kickbacks. Use a full bake after changing the fixed mounting geometry.')
    args=parser.parse_args()
    groups=read_groups(args.dat); palette=read_palette(groups)
    args.out.mkdir(parents=True,exist_ok=True)
    if not args.switches_only:
        for f in args.base.glob('*.bmp'):
            if f.parent.resolve()!=args.out.resolve():shutil.copyfile(f,args.out/f.name)
    board_src=args.board or args.base/'107.bmp'
    board=Image.open(board_src).convert('RGB'); board=board.resize((365*S,470*S),Image.Resampling.LANCZOS)
    board_native,_,bx,by=read_bitmap(groups[107][1][0])
    if not args.switches_only:
        refinish_table(board)
    mounting_art=board.copy()
    if not args.switches_only:
        # Visibility reconstruction reads the registered edges before material
        # detail is applied, so nearby moving hardware retains its exact mask.
        finish_ramp(board, S)
        finish_left_galaxy(board, S)
    table_depth=read_depth(groups[107])
    spinner_atlases(groups, args.out)
    if not args.switches_only:
        plunger_motion(groups,args.out)
    off_coords=set()
    detector_seats=[]
    updated=[]
    for gid,fg in enumerate(groups):
        if 1 not in fg:continue
        pix,mask,x,y=read_bitmap(fg[1][0])
        if mask is None:mask=z_mask(fg,pix.shape)
        if mask is None or not mask.any():continue
        off=None
        visibility=None
        yellow_socket=gid in (135,136,145,146,214,215,216)
        h,w=pix.shape; col=palette[pix]
        where=np.array((x-bx,y-by))
        if 432<=gid<=440:art=drop_target(w,h,gid,where)
        elif 481<=gid<=496:art=spinner(w,h,gid,where)
        elif 520<=gid<=530:
            art=detector_material(gid,w,h,S,True)
            detector_seats.append((where,w,h,gid))
        elif 442<=gid<=467 or gid in (500,501,502,505,506,507,508,537):
            art=switch_hardware(w,h,gid,mask,col)
        elif args.switches_only:continue
        elif any(base<=gid<base+8 for base in (372,380,388,396,405,413,421)):
            first=next(base for base in (372,380,388,396,405,413,421) if base<=gid<base+8)
            art=bumper(w,h,first,gid-first,where)
        elif 355<=gid<=370:
            art=tapered_paddle(w,h,col,mask,gid>=363,where)
        elif 473<=gid<=480:
            art=plunger(w,h,where)
        elif gid in (180,181):
            art=slingshot(w,h,gid==181,True)
            off=slingshot(w,h,gid==181,False)
            for var in (1,2):write_asset(args.out,f'{gid}-arc{var}',slingshot(w,h,gid==181,True,var))
        elif gid in (469,470):art=contact_band(w,h,gid==470)
        elif gid in (471,472):art=rebound(w,h,mask,col)
        elif 115<=gid<=260:
            hue=determine_hue(col,mask)
            if 232<=gid<=254:
                # These projected lights use the registered HD rail edges.
                # Clip their straight sides before reducing supersampled coverage.
                art=route_insert(w,h,gid,hue,S)
            else:
                art=lamp(w,h,gid,mask,hue,True)
                visibility=mounting_visibility(fg,table_depth,mounting_art,x-bx,y-by,S,10,yellow_socket,214<=gid<=216)
            if visibility is not None:
                aperture=visibility.crop((10,10,10+w*S,10+h*S))
                alpha=np.uint16(art[1])*np.asarray(aperture,dtype=np.uint16)
                art=art[0],Image.fromarray(np.uint8((alpha+127)//255))
            if gid<232 or gid>=255:
                ux=x-bx; uy=y-by
                if 0<=ux and 0<=uy and ux+w<=board_native.shape[1] and uy+h<=board_native.shape[0]:
                    old=palette[board_native[uy:uy+h,ux:ux+w]]
                    darkhue=determine_hue(old,mask)
                    if np.std(darkhue)<.18:darkhue=hue
                    off=lamp(w,h,gid,mask,darkhue,False)
        else:continue
        # Original target housings and ramp brackets stand in front of
        # these bumper feet, even when the head changes height after a hit.
        if 372<=gid<=379 or 405<=gid<=420:
            front=mounting_visibility(fg,table_depth,mounting_art,x-bx,y-by,S,0)
            if front is not None:
                alpha=np.asarray(art[1],dtype=np.uint16)*np.asarray(front,dtype=np.uint16)
                art=art[0],Image.fromarray(np.uint8((alpha+127)//255))
        write_asset(args.out,gid,art);updated.append(gid)
        if off and (x,y) not in off_coords:
            off_coords.add((x,y))
            nearby_lane={180:(89,292,18,23),181:(261,292,17,23)}.get(gid)
            seat=((x-bx)*S,(y-by)*S)
            if 205<=gid<=213:
                # The diffuser already replaces the old orange lamp print.
                # Its dark glass reaches under each new bezel. Sampling an
                # extra apron here dragged the outer cyan rim onto the glass.
                board.paste(off[0],seat,off[1])
            else:
                replace_insert(board,off,seat,mask,visibility,yellow_socket,nearby_lane,gid in (164,167))
    if not args.switches_only:
        for where,w,h,gid in detector_seats:
            seat_detector(board,where,w,h,gid,S)
        board.save(args.out/'107.bmp')
        table_coverage(z_mask(groups[107],board_native.shape)).convert('RGB').save(args.out/'107-mask.bmp')
    print(f'{len(updated)} reauthored group frames; {len(off_coords)} registered unlit surfaces; {len(list(args.out.glob("*-mask.bmp")))} coverage files.')
    if args.preview:
        args.preview.mkdir(parents=True,exist_ok=True)
        photo=board.copy()
        for gid in (372,380,388,396,405,413,421,355,363,473):
            _,_,x,y=read_bitmap(groups[gid][1][0]); a=Image.open(args.out/f'{gid}.bmp'); m=Image.open(args.out/f'{gid}-mask.bmp').convert('L')
            photo.paste(a,((x-bx)*S,(y-by)*S),m)
        photo.save(args.preview/'table-assembly.png')
if __name__=='__main__':
    main()
