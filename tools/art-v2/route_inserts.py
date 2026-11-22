"""Perspective geometry of the 23 mission-guide projections in Windows PINBALL.DAT."""
import numpy as np
from PIL import Image


# Tail centre, longitudinal vector, half-width perpendicular to that vector,
# shear along it, shaft/head width ratio, and the length fraction of the head.
# Measured on lite300 through lite322 from their depth masks and the visible
# colour change against the unlit DAT background.
# Coordinates are local native pixels. Perspective foreshortens both axes.
_ROUTE = (
    (2.253, 2.461, 12.280, 8.364, 6.097, -0.100, 0.325, 0.456),
    (6.052, 0.653, 1.547, 13.359, 6.042, -0.774, 0.334, 0.438),
    (6.677, 0.800, 1.189, 13.166, 5.812, -0.684, 0.366, 0.442),
    (2.561, 8.970, 10.375, -6.583, 4.563, 0.705, 0.353, 0.456),
    (2.461, 10.002, 7.052, -9.337, 5.121, 0.828, 0.376, 0.452),
    (7.089, 10.429, -1.285, -9.926, 4.622, -0.498, 0.414, 0.442),
    (2.226, 7.060, 7.349, -6.292, 3.742, 1.077, 0.341, 0.458),
    (9.707, 2.122, -8.308, 3.969, 3.362, 1.268, 0.279, 0.489),
    (7.475, 8.240, -3.354, -7.128, 3.875, -1.053, 0.439, 0.492),
    (5.386, 9.207, 1.403, -8.510, 3.972, 0.400, 0.427, 0.437),
    (8.161, 7.869, -5.277, -7.265, 3.993, -1.372, 0.397, 0.480),
    (1.538, 6.747, 11.103, -2.487, 3.859, 0.366, 0.292, 0.436),
    (7.661, 1.321, -2.874, 8.024, 5.009, 0.854, 0.301, 0.420),
    (11.801, 6.338, -10.753, -2.968, 3.453, -1.178, 0.354, 0.448),
    (12.284, 1.778, -10.933, 5.026, 4.964, 0.538, 0.303, 0.482),
    (8.943, 10.328, -5.061, -8.720, 5.017, -1.061, 0.340, 0.437),
    (4.525, 1.060, 2.879, 10.332, 5.445, -0.160, 0.385, 0.411),
    (9.903, 8.796, -9.249, -7.714, 5.077, -1.081, 0.341, 0.467),
    (9.255, 10.030, -6.173, -9.287, 5.193, -0.632, 0.376, 0.456),
    (10.656, 9.124, -10.177, -7.523, 5.657, -0.797, 0.333, 0.465),
    (7.671, 1.002, -1.594, 12.923, 4.918, 0.734, 0.375, 0.438),
    (9.978, 1.521, -8.158, 11.405, 5.623, 0.501, 0.334, 0.450),
    (13.088, 2.667, -11.646, 8.436, 6.039, -0.235, 0.327, 0.471),
)


def outline(group, width, height):
    x, y, dx, dy, half_width, shear, shaft_ratio, head_fraction = _ROUTE[group - 232]
    tail = np.array((x, y))
    length = np.array((dx, dy))
    flank = (half_width * np.array((-dy, dx)) + shear * length) / np.linalg.norm(length)
    tip = tail + length
    neck = tip - length * head_fraction
    vertices = np.array((tip, neck + flank, neck + shaft_ratio * flank, tail + shaft_ratio * flank,
                         tail - shaft_ratio * flank, neck - shaft_ratio * flank, neck - flank))
    # The DAT crops are very tight. Fit overhanging vertices to the pixel
    # boundary as a whole so a tip or tail corner never gets sliced off.
    low = np.minimum(0, vertices.min(0) - .06)
    size = np.array((width, height))
    high = np.maximum(size, vertices.max(0) + .06)
    return (vertices - low) * size / (high - low)


def route_insert(width, height, group, tint, scale):
    vertices = outline(group, width, height)
    rate = scale * 4
    y, x = (np.mgrid[:height*rate, :width*rate].astype('float32') + .5) / rate
    inside = np.zeros(x.shape, dtype=bool)
    distance = np.full(x.shape, np.inf, dtype='float32')
    for a, b in zip(vertices, np.roll(vertices, -1, axis=0)):
        dx, dy = b - a
        u, v = x - a[0], y - a[1]
        along = np.clip((u*dx + v*dy) / (dx*dx + dy*dy), 0, 1)
        distance = np.minimum(distance, np.hypot(u - dx*along, v - dy*along))
        if abs(dy) > 1e-8:
            inside ^= ((a[1] > y) != (b[1] > y)) & (x < a[0] + v*dx/dy)
    # These are projected guide lights. In DAT the background colours show
    # through each arrow. Keep a hint of their tint inside a pale diffuser.
    warm = np.asarray(tint, dtype='float32')
    center = np.array((202, 212, 230), dtype='float32') + 21*warm
    edge = np.array((110, 130, 158), dtype='float32') + 22*warm
    face = np.clip(distance / .62, 0, 1)
    face = face*face*(3 - 2*face)
    emitted = edge + face[..., None]*(center - edge)
    emitted += (3.2*(.5 - y/height))[..., None]
    # Area sampling avoids a ringing fringe in the narrow gaps between a
    # head shoulder and the shaft. Store an unassociated colour/alpha pair.
    def downsample(array):
        return array.reshape(height*scale, 4, width*scale, 4).mean(axis=(1, 3))
    coverage = downsample(inside)
    channels = [downsample(inside * emitted[:, :, c]) / np.maximum(1e-8, coverage) for c in range(3)]
    albedo = np.uint8(np.clip(np.rint(np.stack(channels, axis=-1)), 0, 255))
    # Three projections run under a raised part. In the original depth map
    # those blockers are recorded at native pixel resolution. Follow the
    # front lip in the registered 4x painting, measured in the same local
    # coordinates, before sample integration. A real curved lip clips the
    # polygon along a single continuous edge in both SDR and HDR.
    if group == 237:  # Blue return rail below the right spinner
        coverage = downsample(inside & (x < 10.58 - .314*y))
    elif group == 244:  # Red rubber shoulder above the left spinner
        coverage = downsample(inside & (x < 11.35 - .619*y + .0200*y*y))
    elif group == 252:  # Raised purple ramp over the left return lane
        coverage = downsample(inside & (x < 9.58 + .140*y - .0139*y*y))
    return Image.fromarray(albedo), Image.fromarray(np.uint8(np.rint(255*coverage)))
