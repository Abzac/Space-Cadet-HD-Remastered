"""Visibility of a table insert behind the fixed metalwork in PINBALL.DAT."""
import struct
import numpy as np
from PIL import Image
from scipy import ndimage as ndi


def read_depth(group):
    data = group[12][0]
    width, height, stride = struct.unpack_from('<hhh', data)
    return np.frombuffer(data, '<u2', offset=14).reshape(height, stride)[::-1, :width].astype(np.float32)


def yellow_ink(guide):
    r,g,b=np.moveaxis(guide.astype(float),2,0)
    return (g>60) & (r>65) & (r>g*.72) & (r<g*1.8) & (b<g*.53)


def mounting_visibility(group, board_depth, background, x, y, scale, padding, yellow_socket=False, on_ramp=False):
    """Return visible coverage including the apron used to clean the printed socket."""
    depth = read_depth(group)
    h, w = depth.shape
    valid = depth < 65535
    if not valid.any():
        return None
    yy, xx = np.nonzero(valid)
    fit = np.stack((xx, yy, np.ones_like(xx)), axis=1)
    slope_x, slope_y, _ = np.linalg.lstsq(fit, depth[valid], rcond=None)[0]
    # Extend the local depth slope into the margin; a plate above the lamp
    # may occupy both the edge of the new lens and the old painted socket.
    margin = (padding + scale - 1) // scale + 3
    dy, dx = np.mgrid[-margin:h+margin, -margin:w+margin]
    padded = np.pad(valid, margin)
    iy, ix = ndi.distance_transform_edt(~padded, return_distances=False, return_indices=True)
    iy -= margin
    ix -= margin
    expected = depth[iy,ix] + (dx-ix)*slope_x + (dy-iy)*slope_y
    board = board_depth[np.clip(y+dy,0,board_depth.shape[0]-1),np.clip(x+dx,0,board_depth.shape[1]-1)]
    # Intersecting stampings are thousands of native range units nearer to
    # the camera; the bevel of the insert itself belongs to the receiving face.
    front = (expected-board) > 640
    if not np.any(front[margin:margin+h,margin:margin+w] & valid):
        return None
    distance = ndi.distance_transform_edt(~front) - ndi.distance_transform_edt(front)
    signed = np.asarray(Image.fromarray(distance.astype('float32')).resize(
        (distance.shape[1]*scale,distance.shape[0]*scale), Image.Resampling.BICUBIC))
    # Guide the reconstructed subpixel boundary with the registered artwork.
    # Two nearby chromatic samples along the depth normal distinguish, for
    # example, the purple ramp from the yellow bulb just behind its rim.
    gy, gx = np.gradient(signed.astype(float))
    magnitude = np.maximum(1e-5, np.hypot(gx,gy))
    gx,gy = gx/magnitude,gy/magnitude
    yp,xp = np.indices(signed.shape, dtype=float)
    box=((x-margin)*scale,(y-margin)*scale,(x+w+margin)*scale,(y+h+margin)*scale)
    guide=np.asarray(background.crop(box).convert('RGB'),dtype=float)
    def probe(direction):
        ix=np.clip(np.rint(xp+direction*gx*scale*1.6).astype(int),0,signed.shape[1]-1)
        iy=np.clip(np.rint(yp+direction*gy*scale*1.6).astype(int),0,signed.shape[0]-1)
        return guide[iy,ix]
    back, raised = probe(1), probe(-1)
    contrast = np.linalg.norm(back-raised,axis=2)
    preference = (np.linalg.norm(guide-raised,axis=2)-np.linalg.norm(guide-back,axis=2))/np.maximum(contrast,24)
    signed = signed + np.clip(preference,-1,1)*np.clip(contrast/60,0,1)*1.10
    if yellow_socket:
        # The registered illustration moves the little red posts sideways by
        # part of a source pixel. The printed yellow lens is on the lower face.
        signed=np.where(yellow_ink(guide),np.maximum(signed,1.),signed)
        r,g,b=np.moveaxis(guide,2,0)
        red=(r>70) & (r>1.9*g) & (r>1.9*b)
        silver=(np.maximum.reduce((r,g,b))-np.minimum.reduce((r,g,b))<50) & (g>78)
        if on_ramp:
            purple=(r>g*1.18) & (b>g*1.2) & (g>24)
            signed=np.where(purple,np.maximum(signed,1.),signed)
        post=red | (silver & (ndi.distance_transform_edt(~red)<scale*1.6))
        signed=np.where(post & (distance.repeat(scale,0).repeat(scale,1)<2.8),np.minimum(signed,-.8),signed)
    alpha=np.uint8(np.rint(255*np.clip(.5+signed*1.7,0,1)))
    offset=margin*scale-padding
    return Image.fromarray(alpha[offset:offset+h*scale+2*padding,offset:offset+w*scale+2*padding])
