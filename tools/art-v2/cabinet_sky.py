"""Fit two miniature galaxies into their visible windows on the printed table."""
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates


def finish_left_galaxy(board, scale):
    source = np.asarray(board, dtype=np.float32)

    def smooth(t):
        t = np.clip(t, 0, 1)
        return t*t*(3 - 2*t)

    def sample(image, x, y):
        return np.stack([map_coordinates(image[..., k], [y*scale-.5, x*scale-.5],
                                         order=1, mode='nearest') for k in range(3)], axis=-1)

    # Prefilter the parent galaxy before reducing its size fourfold.
    cloud_patch = source[150*scale:197*scale,260*scale:312*scale]
    cloud_patch = gaussian_filter(cloud_patch, (1.35,1.35,0))
    for left in (True, False):
        box = (63, 41, 89, 72) if left else (199, 12, 225, 23)
        xx, yy = np.meshgrid(np.arange(box[0]*scale, box[2]*scale), np.arange(box[1]*scale, box[3]*scale))
        x, y = (xx+.5)/scale, (yy+.5)/scale
        old = source[yy, xx]
        if left:
            # Rollback rubber and the silver return frame bound the little sky panel.
            inside = 71.78 - .1651*y + 4.75 + .0083*y + 1.9
            rim = np.interp(y, [41, 45, 48, 50, 52, 54, 55, 58, 61, 63, 65, 67, 70, 72],
                            [74.1, 75.7, 77.9, 80.1, 83.2, 85.5, 85.8, 84.7, 83.9, 83.2, 81.8, 80.1, 77, 75])
            clear = smooth((x-inside)*4) * smooth((rim-x)*2)
            old_radius = np.hypot((x-70.25)/19, (y-55.4)/15)
            clear *= smooth((1-old_radius)/.35)
            r, g, b = np.moveaxis(old, 2, 0)
            clear *= smooth((b-.57*g+4)/10) * smooth((g+15-r*.27)/5)
            clear *= smooth((np.hypot((x-77.9)/3.8, (y-67.5)/2.8)-1)*5)
            cx, cy, rx, ry, tilt = 76.12, 54.65, 6.38, 4.52, -.37
            # Painted arrow and rollback rail are also visible with the lamps
            # off. Keep their dark bevel, including the tip under the spiral.
            arrow_clear=np.maximum.reduce((76.65-x,57.90-.44*(x-77.)-y,y-58.90-.53*(x-77.)))
            clear*=smooth(arrow_clear*4)
        else:
            # Place the complete small companion below the straight top lip.
            old_radius = np.hypot((x-211.35)/11.2, (y-13.65)/7.7)
            clear = smooth((1-old_radius)/.38) * smooth((y-12.22)*7) * smooth((20.1-y)*2)
            cx, cy, rx, ry, tilt = 211.2, 15.92, 5.65, 3.27, .13
        # Both regions have the same printed dark star field as the sliver
        # between the larger clusters. Do not move the fixed metalwork.
        bx = 228.0 + .51*(x-cx) + .13*(y-cy)
        by = 16.8 + .14*(y-cy) - .04*(x-cx)
        sky = np.clip(sample(source, bx, by), [12,13,16], [32,28,35])
        dx, dy = x-cx, y-cy
        co, si = np.cos(tilt), np.sin(tilt)
        u, v = (co*dx+si*dy)/rx, (-si*dx+co*dy)/ry
        rad = np.hypot(u,v)
        # Sample the stars and filaments of the approved right-hand cloud.
        # A different orientation and aspect ratio fit each miniature pocket.
        angle = .51 if left else -1.17
        nu, nv = u*np.cos(angle)-v*np.sin(angle), u*np.sin(angle)+v*np.cos(angle)
        stars = sample(cloud_patch, 25.7 + 20.*nu, 23.6 + 18.5*nv)
        cloud = smooth((1-rad)/.44)[...,None]
        finish = sky*(1-cloud) + stars*cloud
        composed = old + clear[...,None]*(finish-old)
        board.paste(Image.fromarray(np.uint8(np.clip(composed+.5,0,255))), (box[0]*scale,box[1]*scale))
