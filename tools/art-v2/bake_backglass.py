"""Assemble group 2. All display apertures stay in the original pixel coordinates."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def bake(original, illustration, dest):
    size = Image.open(original).size
    if size != (812, 1576):
        raise ValueError(f"Unexpected Space Cadet backglass size: {size}")
    width, height = size
    y, x = np.indices((height, width))
    rng = np.random.default_rng(107)
    grain = rng.normal(0, 1.4, (height, 1)) + rng.normal(0, .65, (height, width))
    metal = 65 - 21*y/height + 15*np.exp(-((x*.7+y*.16-270)/270)**2) + grain
    casing = np.stack((metal*.96, metal*.985, metal*1.04), axis=-1)
    panel = Image.fromarray(np.uint8(np.clip(casing, 0, 255)), 'RGB')
    draw = ImageDraw.Draw(panel)
    def bevel(box, recessed):
        left, top, right, bottom = box
        bright, dark = ((137,143,150),(21,24,30))
        for i in range(5):
            first, second = (dark,bright) if recessed else (bright,dark)
            draw.line([(left+i,bottom-i),(left+i,top+i),(right-i,top+i)],fill=first,width=1)
            draw.line([(right-i,top+i),(right-i,bottom-i),(left+i,bottom-i)],fill=second,width=1)
    bevel((0,0,width-1,height-1),False)
    # Score displays are drawn directly by the table at runtime.
    for box in ((32,32,778,698),(32,711,166,857),(170,711,778,857),
                (32,879,778,1110),(32,1136,778,1544)):
        draw.rectangle(box,fill=(1,2,4))
        bevel(box,True)
    art = Image.open(illustration).convert('RGB').resize((728,646),Image.Resampling.LANCZOS)
    panel.paste(art,(40,40))
    # The poster master ends three columns before the silver bevel. Under
    # the rotating globe, carry the painted limb (including its soft edge)
    # to the aperture. A neighboring column subtends a shorter chord as it
    # approaches the limb; copying it straight across would flatten the arc.
    cx, cy, rx = 99.71, 557.68, 70.21
    source_x = 40
    chord = lambda x: np.sqrt(1 - ((x + .5 - cx) / rx) ** 2)
    first_y, end_y = 510, 607
    ink = np.asarray(panel.crop((source_x, first_y, source_x+1, end_y)))[:, 0]
    rows = np.arange(first_y, end_y)
    for px in range(37, source_x):
        source_y = cy - .5 + (rows + .5 - cy) * chord(source_x) / chord(px)
        strip = np.column_stack([np.interp(source_y, rows, channel) for channel in ink.T])
        # Outside the painted globe retain the existing dark aperture.
        covered = (strip[:, 1] > strip[:, 2]+2) & (strip[:, 1] > 5)
        for y, color in zip(rows[covered], np.rint(strip[covered]).astype(np.uint8)):
            panel.putpixel((px, int(y)), tuple(color))
    draw.rectangle((632,544,759,671),fill=(0,0,0),outline=(150,35,29),width=3)
    # This label belongs to the print. The number beside it is a live game sprite.
    fonts = [Path(r'C:/Windows/Fonts/bahnschrift.ttf'),'DejaVuSans.ttf']
    for name in fonts:
        try:
            font = ImageFont.truetype(str(name),70)
            break
        except OSError:
            continue
    else:
        raise RuntimeError("Install Bahnschrift or DejaVu Sans to render the BALL label.")
    draw.text((613,648),'BALL',font=font,anchor='rs',fill=(216,53,43))
    panel.save(dest)


if __name__ == '__main__':
    bake(*sys.argv[1:])
