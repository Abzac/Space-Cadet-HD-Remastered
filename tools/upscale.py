"""Extract the Windows Space Cadet DAT palette/bitmaps and bake registered 4x textures.

Requires Python 3.10+, numpy, Pillow, scipy, torch. The official checkpoint is
https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth
Run: python upscale.py PINBALL.DAT realesr-general-x4v3.pth HD
"""
import argparse
import hashlib
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt
import torch
from torch import nn
import torch.nn.functional as F


def read_groups(path):
    data = path.read_bytes()
    sig, _, _, file_size, group_count, _, extra = struct.unpack_from(
        '<21s50s100sIHIH', data)
    if sig.rstrip(b'\0') != b'PARTOUT(4.0)RESOURCE' or file_size != len(data):
        raise ValueError('Unknown or truncated DAT')
    offset = 183 + extra
    groups = []
    for _ in range(group_count):
        fields = {}
        count = data[offset]
        offset += 1
        for _ in range(count):
            kind = data[offset]
            offset += 1
            if kind in (0, 2):
                size = 2
            elif kind == 13:
                size = 0
            else:
                size, = struct.unpack_from('<I', data, offset)
                offset += 4
            fields.setdefault(kind, []).append(data[offset:offset + size])
            offset += size
        groups.append(fields)
    if offset != len(data):
        raise ValueError('Unexpected bytes after groups')
    return groups


def read_palette(groups):
    entry, = [g for g in groups if g.get(3, [b''])[0].rstrip(b'\0') == b'background']
    colors = np.frombuffer(entry[5][0], dtype=np.uint8).reshape(256, 4)
    palette = np.zeros((256, 3), dtype=np.uint8)
    palette[10:246] = colors[10:246, 2::-1]
    palette[:10] = [[0, 0, 0], [128, 0, 0], [0, 128, 0], [128, 128, 0],
                    [0, 0, 128], [128, 0, 128], [0, 128, 128], [192, 192, 192],
                    [192, 220, 192], [166, 202, 240]]
    palette[255] = 255
    return palette


def read_bitmap(field):
    resolution, w, h, x, y, size, flags = struct.unpack_from('<B4hiB', field)
    if resolution != 0 or len(field) != 14 + size:
        raise ValueError('Unsupported bitmap resolution or invalid size')
    if flags & 4:
        # Sparse runs measure the initial skip in original framebuffer pixels.
        bitmap = np.full(w * h, 255, dtype='uint8')
        visible = np.zeros(w * h, dtype=bool)
        pos = 14
        dest = 0
        while True:
            skip, = struct.unpack_from('<h', field, pos)
            pos += 2
            if skip < 0:
                break
            dest += skip + (w - 600 if skip > w else 0)
            count, = struct.unpack_from('<H', field, pos)
            pos += 2
            for i in range(dest, dest + count):
                depth, bitmap[i] = struct.unpack_from('<HB', field, pos)
                visible[i] = depth != 65535
                pos += 3
            dest += count
        return bitmap.reshape(h, w)[::-1], visible.reshape(h, w)[::-1], x, y
    pixels = np.frombuffer(field, dtype='uint8', offset=14).reshape(h, (w + 3) & ~3)
    return pixels[::-1, :w], None, x, y


def z_mask(fields, shape):
    if 12 not in fields:
        return None
    z = fields[12][0]
    w, h, stride = struct.unpack_from('<hhh', z)
    if (h, w) != shape or len(z) != 14 + 2 * h * stride:
        return None
    return (np.frombuffer(z, '<u2', offset=14).reshape(h, stride)[::-1, :w] != 65535)


class Upscaler(nn.Module):
    """Real-ESRGAN compact 32-layer body with pixel-shuffle residual output."""
    def __init__(self):
        super().__init__()
        layers = [nn.Conv2d(3, 64, 3, padding=1), nn.PReLU(64)]
        for _ in range(32):
            layers.extend([nn.Conv2d(64, 64, 3, padding=1), nn.PReLU(64)])
        layers.append(nn.Conv2d(64, 3 * 16, 3, padding=1))
        self.body = nn.Sequential(*layers)

    def forward(self, low):
        return F.pixel_shuffle(self.body(low), 4) + F.interpolate(low, scale_factor=4, mode='nearest')


def generate(model, rgb, device):
    """Extend crop edges before inference so separately drawn sprites have no black fringe."""
    low = torch.from_numpy(rgb.copy()).to(device=device)
    low = low.permute(2, 0, 1).unsqueeze(0).to(dtype=next(model.parameters()).dtype) / 255
    border = 16
    low = F.pad(low, (border,) * 4, mode='replicate')
    with torch.inference_mode():
        up = model(low)[0, :, border*4:-border*4, border*4:-border*4]
        prediction = up.permute(1, 2, 0).clamp(0, 1).mul(255).round().byte().cpu().numpy()
    # Keep original fine marks and colour values. Use smooth low-frequency
    # error correction, retaining neural sub-pixel detail and avoiding block boundaries.
    h, w = rgb.shape[:2]
    pooled = prediction.reshape(h, 4, w, 4, 3).mean((1, 3))
    correction = rgb.astype(np.float32) - pooled
    correction_hr = np.stack([
        np.array(Image.fromarray(correction[:, :, ch].astype(np.float32)).resize(
            (w * 4, h * 4), Image.Resampling.BICUBIC))
        for ch in range(3)
    ], axis=2)
    return Image.fromarray(np.uint8(np.clip(np.rint(prediction + correction_hr * 0.85), 0, 255)))


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('dat', type=Path)
    cli.add_argument('weights', type=Path)
    cli.add_argument('out', type=Path)
    cli.add_argument('--inspect', action='store_true')
    args = cli.parse_args()
    groups = read_groups(args.dat)
    palette = read_palette(groups)
    if not args.inspect:
        args.out.mkdir(parents=True, exist_ok=True)
        device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
        model = Upscaler().eval()
        model.load_state_dict(torch.load(args.weights, map_location='cpu', weights_only=True)['params'])
        model = model.to(device)
        if device.type == 'cuda':
            model = model.half()
        print('4x texture bake using', device, 'weights SHA256', hashlib.sha256(args.weights.read_bytes()).hexdigest(), flush=True)
    texels = 0
    count = 0
    for gid, fields in enumerate(groups):
        if 1 not in fields:
            continue
        assert len(fields[1]) == 1
        indices, mask, x, y = read_bitmap(fields[1][0])
        if mask is None:
            mask = z_mask(fields, indices.shape)
        name = fields.get(3, [b''])[0].rstrip(b'\0').decode('ascii', 'replace')
        h, w = indices.shape
        if args.inspect:
            print(gid, name, w, h, x, y, 'visible', round(mask.mean(), 3) if mask is not None else 'all')
            continue
        rgb = palette[indices]
        # Avoid pulling sentinel white/black from outside a sprite into its edge.
        if name == 'table':
            # The zero-depth exterior is visible in the cabinet's black margins.
            mask = None
        if mask is not None and mask.any() and not mask.all():
            nearest = distance_transform_edt(~mask, return_distances=False, return_indices=True)
            rgb = rgb[tuple(nearest)]
        hr = generate(model, rgb, device)
        hr.save(args.out / f'{gid}.bmp')
        texels += hr.width * hr.height
        count += 1
        if w >= 300 or count % 100 == 0:
            result = np.asarray(hr.resize((w, h), Image.Resampling.BOX)).astype('float32')
            rmse = np.mean((result - rgb)**2) ** 0.5
            psnr = 20 * math.log10(255 / rmse) if rmse > 0 else math.inf
            print(f'{count:3d} textures | {gid} {name} | {hr.size} | roundtrip {psnr:.1f} dB', flush=True)
    print(f'{count} baked textures, {texels} texels')


if __name__ == '__main__':
    main()
