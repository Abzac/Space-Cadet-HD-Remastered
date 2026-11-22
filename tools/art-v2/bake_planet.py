"""Recover a complete albedo map from the painted green planet on the poster."""
from pathlib import Path
import sys

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, map_coordinates


def smooth(x):
    x = np.clip(x, 0., 1.)
    return x * x * (3. - 2. * x)


def sunlight(x, y, z):
    # Fixed irradiance estimated from the painted globe, in display RGB.
    return .62 + np.maximum(0., .27*x - .42*y + .46*z)


def bake(paint, destination):
    art = np.asarray(Image.open(paint).convert('RGB'), dtype=np.float32)
    if art.shape[:2] != (1181, 1332):
        raise ValueError("Expected the approved 1332 x 1181 poster master")
    # Register against the colored limb in the full-resolution poster.
    cx, cy, rx, ry = 108.75, 945.91, 128.46, 129.25
    cloud = art[812:1080, :245].copy()
    # Resolve the smaller folds in the high-resolution paint. This operates
    # on the painted cloud bands, not on a procedural noise overlay.
    cloud += .24 * (cloud - gaussian_filter(cloud, (.95, .95, 0)))

    def sample(x, y):
        x = np.clip(x, (1.-cx)/rx, 1.)
        z = np.sqrt(np.maximum(0., 1.-x*x-y*y))
        coords = [cy-812 + ry*y, cx + rx*x]
        color = np.stack([map_coordinates(cloud[:, :, i], coords, order=1, mode='nearest')
                          for i in range(3)], axis=-1)
        return color / sunlight(x, y, z)[..., None]

    width, height = 768, 384
    lat, lng = np.indices((height, width), dtype=float)
    lng = (lng+.5) * (2*np.pi/width) - np.pi
    lat = (lat+.5) * (np.pi/height) - np.pi/2
    y = np.sin(lat)
    x = np.cos(lat)*np.sin(lng)
    z = np.cos(lat)*np.cos(lng)

    # The rear hemisphere was never painted. Three overlapping projections
    # continue the existing pigment and avoid smearing it at the far meridian.
    # The painted near side takes precedence wherever it contains valid ink.
    far = np.zeros((height, width, 3))
    total = np.zeros((height, width))
    for angle, tilt in [(np.pi, .38), (.5*np.pi, -1.89), (-.5*np.pi, 1.42)]:
        forward = np.cos(lat)*np.cos(lng-angle)
        cross = np.cos(lat)*np.sin(lng-angle)
        weight = .00003 + np.maximum(0., .15+forward)**8
        sx = .84*(cross*np.cos(tilt)-y*np.sin(tilt)) + .035
        sy = .90*(cross*np.sin(tilt)+y*np.cos(tilt))
        far += weight[..., None] * sample(sx, sy)
        total += weight
    far /= total[..., None]
    visible = smooth((z-.18)/.23) * smooth((cx+rx*x-1)/11)
    albedo = far + (sample(x, y)-far)*visible[..., None]
    Image.fromarray(np.uint8(np.clip(albedo+.5, 0, 255)), 'RGB').save(destination)


if __name__ == '__main__':
    bake(Path(sys.argv[1]), Path(sys.argv[2]))
