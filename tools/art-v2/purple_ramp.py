"""Extend the finish of the raised violet plastic into its flat control panel."""
import numpy as np
from PIL import Image
from scipy import ndimage as ndi


def finish_ramp(board, scale):
    # Sample the material between the rolled lips of the upper arch. Unwrap
    # the bending strip first, so the material follows the panel at its foot.
    def read_box(box):
        return np.asarray(board.crop(tuple(n * scale for n in box)), dtype=np.float32)

    donor = read_box((79, 127, 152, 192))
    luminance = donor @ np.array([.25, .54, .21], dtype=np.float32)
    detail = luminance - ndi.gaussian_filter(luminance, 2.65 * scale)
    # The curving centre line stays on exposed plastic clear of the side rim,
    # bracket and rivet. The actual pores and mould marks supply the pattern.
    uv_y, uv_x = np.mgrid[0:14*scale, 0:65*scale].astype(np.float32)
    arch_x = 82 + (uv_x + .5) / scale
    arch_y = np.interp(arch_x, [82, 91, 100, 112, 119, 126, 134, 140, 147],
                       [141.7, 141.5, 142.4, 146.2, 151.9, 158.2, 165.5, 172.2, 180.5])
    samples = [(arch_y - 127) * scale + uv_y - 7 * scale, (arch_x - 79) * scale - .5]
    material = ndi.map_coordinates(detail, samples, order=1, mode='nearest')
    material -= ndi.gaussian_filter(material, 4 * scale)

    box = (43, 143, 104, 253)
    panel = read_box(box)
    yy, xx = np.indices(panel.shape[:2], dtype=np.float32)
    x = box[0] + (xx + .5) / scale
    y = box[1] + (yy + .5) / scale
    r, g, b = np.moveaxis(panel, 2, 0)
    # Colour isolates the violet lacquer. Keep the printed graphic, chrome,
    # housing edges and lens free of the plastic material's grain.
    purple = (r > 60) & (g > 34) & (r - g > 26) & (b - r > 0) & (b - r < 28) & (g > .46*r)
    coverage = np.clip((ndi.distance_transform_edt(purple) - .7) / 3.5, 0, 1)
    falloff = np.clip((y-144)/16, 0, 1) * np.clip((253-y)/27, 0, 1)
    falloff *= np.clip((x-43)/5, 0, 1) * np.clip((104-x)/7, 0, 1)
    # Tailor reflected material samples to the orientation of the ramp. A
    # second oblique, irregular pass avoids repeated scratches in the finish.
    u = (y-145)*.98 + .24*(x-64)
    v = (x-55)*.72 + .15*(y-166) + 1.75*np.sin((y-169)/16)
    def sample(s, t):
        return ndi.map_coordinates(material, (t*scale, s*scale), order=1, mode='reflect')
    relief = .88*sample(u, v) + .39*sample(.47*u-.54*v+23, .58*u+.71*v+2)
    # A broad satin highlight follows the bend into the flat section.
    highlight = 4.2*np.exp(-((x-62-.19*(183-y))/8.5)**2)-1.3
    shift = (np.clip(relief, -14, 17) + highlight) * falloff * coverage
    panel += shift[..., None] * np.array([1, .88, 1.03], dtype=np.float32)
    board.paste(Image.fromarray(np.uint8(np.clip(panel+.5, 0, 255))), (box[0]*scale, box[1]*scale))
