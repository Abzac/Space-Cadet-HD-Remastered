"""Rebuild the complete Space Cadet HD texture set from the Windows DAT."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


TOOLS = Path(__file__).resolve().parent
DATA_SHA256 = "42defd5d2a339a3953fe643716aff4e35b2cea7a3e54505c82d1e78d5325bc04"
WEIGHTS_SHA256 = "8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292"


def check_input(path, expected):
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError(f"Unexpected SHA-256 for {path}: {actual} (expected {expected})")


def run(script, *args):
    subprocess.run([sys.executable, "-B", str(script), *map(str, args)], check=True)


def finish(dat, base, out):
    if base.resolve() == out.resolve():
        raise ValueError("Choose separate folders for the base textures and final game textures.")
    if sum(path.stem.isdigit() for path in base.glob("*.bmp")) != 318:
        raise ValueError(f"Expected 318 numeric DAT texture groups in {base}")
    run(TOOLS / "render_parts.py", dat, base, out, "--board", TOOLS / "board-source.png")
    run(TOOLS / "bake_backglass.py", base / "2.bmp", TOOLS / "backglass-generated.png", out / "2.bmp")
    run(TOOLS / "bake_planet.py", TOOLS / "backglass-generated.png", out / "poster-planet.bmp")
    counts = (sum(p.stem.isdigit() for p in out.glob("*.bmp")),
              len(list(out.glob("*-mask.bmp"))),
              len(list(out.glob("*-arc[12].bmp"))), len(list(out.glob("spinner-[0-9][0-9][0-9].bmp"))),
              int((out / "poster-planet.bmp").is_file()))
    if counts != (318, 306, 4, 2, 1):
        raise ValueError(f"Unexpected numbers of textures, coverage maps, arc variants, spinners and planet maps: {counts}")
    with (out / "plunger-motion.bin").open("rb") as spring:
        if spring.read(8) != b"SPR1\x71\x00\x04\x00":
            raise ValueError("Expected the HD spring atlas with 113 positions at 4x scale")
    print(f"Complete: {sum(counts)} BMP files and the plunger motion atlas in {out}")


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("dat", type=Path, help="Original Windows PINBALL.DAT")
    cli.add_argument("out", type=Path, help="Destination HD folder")
    source = cli.add_mutually_exclusive_group(required=True)
    source.add_argument("--weights", type=Path, help="Official Real-ESRGAN general-x4v3 checkpoint")
    source.add_argument("--base", type=Path, help="Previously generated, unmodified output of tools/upscale.py")
    args = cli.parse_args()
    check_input(args.dat, DATA_SHA256)
    if args.base:
        finish(args.dat, args.base, args.out)
    else:
        check_input(args.weights, WEIGHTS_SHA256)
        with tempfile.TemporaryDirectory(prefix="spacecadet-base-") as temp:
            base = Path(temp)
            run(TOOLS.parent / "upscale.py", args.dat, args.weights, base)
            finish(args.dat, base, args.out)


if __name__ == "__main__":
    main()
