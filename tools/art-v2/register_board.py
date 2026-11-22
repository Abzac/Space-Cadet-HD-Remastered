"""Register ImageGen's board paint to the Windows table (source indices never move).

Run with the original preview, AI paint and target path; prints held-out errors in native pixels.
The map here is an offline texture bake. No OpenCV dependency in the game.
"""
import sys
import cv2 as cv
import numpy as np
from scipy.interpolate import RBFInterpolator
from scipy.spatial import cKDTree

def load(path):
    return cv.imdecode(np.fromfile(path, np.uint8), cv.IMREAD_COLOR)

def features(pic):
    gray = cv.cvtColor(pic, cv.COLOR_BGR2GRAY)
    smooth = cv.GaussianBlur(gray,(0,0),1.1)
    return cv.createCLAHE(2.4,(12,14)).apply(smooth)

def register(refpath, artpath, dest):
    ref, art = load(refpath), load(artpath)
    h,w = ref.shape[:2]
    a, b = features(ref), features(cv.resize(art,(w,h),interpolation=cv.INTER_AREA))
    # Direct descriptor matches allow small changes in shape, brightness and ink.
    sift = cv.SIFT_create(contrastThreshold=.013)
    k1,d1=sift.detectAndCompute(a,None)
    k2,d2=sift.detectAndCompute(b,None)
    bf=cv.BFMatcher()
    pairs=bf.knnMatch(d1,d2,k=2)
    reverse=bf.match(d2,d1)
    matches=[m for m,n in pairs if m.distance<.79*n.distance and reverse[m.trainIdx].trainIdx==m.queryIdx]
    p=np.array([k1[m.queryIdx].pt for m in matches])
    q=np.array([k2[m.trainIdx].pt for m in matches])
    keep=np.linalg.norm(q-p,axis=1)<32
    p,q=p[keep],q[keep]
    # Reject contradictory matches against the local displacement median.
    for _ in range(2):
        _,idx=cKDTree(p).query(p,k=min(12,len(p)))
        err=np.linalg.norm((q-p)-np.median((q-p)[idx],axis=1),axis=1)
        keep=err<7
        p,q=p[keep],q[keep]
    _,idx=np.unique(np.round(p/3),axis=0,return_index=True)
    p,q=p[idx],q[idx]
    # File corners and straight external cabinet bounds, in 2x preview coordinates.
    anchors=np.array([[0,0],[w-1,0],[0,h-1],[w-1,h-1],[365,0],[365,937]])
    def fit(p,q):
        return RBFInterpolator(np.vstack((p,anchors))/100, np.vstack((q-p,np.zeros_like(anchors))),
                               smoothing=.025,kernel='thin_plate_spline',neighbors=50)
    val=np.arange(len(p))%7==0
    errors=np.linalg.norm(fit(p[~val],q[~val])(p[val]/100)-(q[val]-p[val]),axis=1)/2
    print('SIFT landmarks',len(p),'held-out native pixel median/p90',np.percentile(errors,[50,90]))
    field=fit(p,q)
    # Fit a smooth field on a coarse mesh and resample the original AI output only once.
    ys=np.arange(0,h+8,8); xs=np.arange(0,w+8,8)
    xx,yy=np.meshgrid(xs,ys)
    delta=field(np.stack((xx.ravel(),yy.ravel()),1)/100).reshape((*xx.shape,2)).astype('f')
    X,Y=np.meshgrid((np.arange(w*2,dtype='f')+.5)/2-.5,(np.arange(h*2,dtype='f')+.5)/2-.5)
    fine=cv.remap(delta,X/8,Y/8,cv.INTER_CUBIC,borderMode=cv.BORDER_REPLICATE)
    mapx=(X+fine[:,:,0]+.5)*art.shape[1]/w-.5
    mapy=(Y+fine[:,:,1]+.5)*art.shape[0]/h-.5
    aligned=cv.remap(art,mapx,mapy,cv.INTER_LANCZOS4,borderMode=cv.BORDER_CONSTANT)
    cv.imencode('.png',aligned)[1].tofile(dest)
if __name__=='__main__': register(*sys.argv[1:])
