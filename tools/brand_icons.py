#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Makes every product icon from one image: branding/logo/ to branding/theme/.

  brand_icons.py [--source PNG] [--small PNG] [--out DIR] [--preview DIR]

The source is the logo on a white background, or on a transparent one. A
simplified version for 16 to 32 px may sit beside it as small.png. The output
is committed; patches/0027 points Chromium's icon references at it. Replacing
the logo means replacing the source and running this again.
"""

from __future__ import annotations

import argparse
import base64
import io
import re
import struct
import sys
from collections import deque
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parent.parent
LOGO_DIR = REPO / "branding" / "logo"
THEME_DIR = REPO / "branding" / "theme"

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
# Around the logo, as a fraction of the square's side on each edge.
MARGIN = 1 / 32
# A pixel whose channels are all at least this, connected to the image's
# border, is background.
WHITE_MIN = 245
# Pixels this close to the background are anti-aliased edges: their white is
# un-mixed into transparency instead of left as a pale fringe.
EDGE_BAND = 2
# Logos up to this size use small.png when it exists.
SMALL_MAX = 32
# Start tiles: upstream's pixel sizes and the logo's share of them.
TILES = {"win/tiles/Logo.png": (600, 220), "win/tiles/SmallLogo.png": (176, 118)}

PAGE = (250, 250, 252, 255)
PAGE_EDGE = (150, 152, 165, 255)
FOLD = (218, 220, 230, 255)
PDF_RED = (214, 40, 40, 255)

OUTPUTS = (
    "win/app.ico", "win/doc.ico", "win/pdf.ico", *TILES,
    *(f"default_{scale}00_percent/product_logo_{n}.png" for scale in (1, 2, 3) for n in (16, 32)),
    *(f"product_logo_{n}.png" for n in (16, 24, 64, 128, 256)),
    "product_logo.svg",
)


class BrandIconsError(Exception):
    pass


def remove_background(img: Image.Image) -> Image.Image:
    """The logo with its white surroundings made transparent."""
    img = img.convert("RGBA")
    if img.getchannel("A").getextrema()[0] < 255:
        return img  # already transparent: nothing to guess
    w, h = img.size
    r, g, b, _ = img.split()
    darkest = ImageChops.darker(ImageChops.darker(r, g), b).tobytes()
    white = bytes(1 if v >= WHITE_MIN else 0 for v in darkest)

    background = bytearray(w * h)
    queue = deque()
    border = ({*range(w), *range((h - 1) * w, h * w)}
              | {y * w for y in range(h)} | {y * w + w - 1 for y in range(h)})
    for i in border:
        if white[i]:
            background[i] = 1
            queue.append(i)
    while queue:
        i = queue.popleft()
        x, y = i % w, i // w
        for j, inside in ((i - 1, x > 0), (i + 1, x < w - 1), (i - w, y > 0), (i + w, y < h - 1)):
            if inside and white[j] and not background[j]:
                background[j] = 1
                queue.append(j)

    near = Image.frombytes("L", (w, h), bytes(background)).point(lambda v: 255 if v else 0)
    near = near.filter(ImageFilter.MaxFilter(2 * EDGE_BAND + 1)).tobytes()
    rgba = bytearray(img.tobytes())
    for i in range(w * h):
        p = 4 * i
        if background[i]:
            rgba[p:p + 4] = b"\0\0\0\0"
        elif near[i]:
            # Un-mix from white: the most saturated channel decides the opacity.
            alpha = max(255 - rgba[p], 255 - rgba[p + 1], 255 - rgba[p + 2]) / 255
            if alpha == 0:
                rgba[p:p + 4] = b"\0\0\0\0"
                continue
            for c in range(3):
                rgba[p + c] = max(0, min(255, round(255 - (255 - rgba[p + c]) / alpha)))
            rgba[p + 3] = round(alpha * 255)
    out = Image.frombytes("RGBA", (w, h), bytes(rgba))

    alpha = out.getchannel("A")
    if alpha.getextrema()[1] == 0:
        raise BrandIconsError("no logo: the whole source is white background")
    edges = [alpha.getpixel((x, y)) for x in range(w) for y in (0, h - 1)]
    edges += [alpha.getpixel((x, y)) for y in range(h) for x in (0, w - 1)]
    if any(edges):
        raise BrandIconsError("the source's border is not white background; give a source "
                              "on white, or with transparency")
    return out


def frame(img: Image.Image) -> Image.Image:
    """The logo, cropped and centred on a transparent square with a margin."""
    logo = img.crop(img.getchannel("A").getbbox())
    side = round(max(logo.size) / (1 - 2 * MARGIN))
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(logo, ((side - logo.width) // 2, (side - logo.height) // 2))
    return square


def resized(img: Image.Image, n: int) -> Image.Image:
    # Premultiplied, so transparent pixels' colour doesn't bleed into edges.
    return img.convert("RGBa").resize((n, n), Image.Resampling.LANCZOS).convert("RGBA")


def placed(logo: Image.Image, side: int, logo_side: int) -> Image.Image:
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    offset = (side - logo_side) // 2
    canvas.alpha_composite(resized(logo, logo_side), (offset, offset))
    return canvas


def page_icon(logo: Image.Image, n: int, pdf: bool) -> Image.Image:
    """The logo on a page with a folded corner; a red band marks PDF."""
    s = 1024  # drawn large, then reduced
    page = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(page)
    left, top, right, bottom, fold = 176, 64, 848, 960, 192
    draw.polygon([(left, top), (right - fold, top), (right, top + fold), (right, bottom),
                  (left, bottom)], fill=PAGE, outline=PAGE_EDGE, width=16)
    draw.polygon([(right - fold, top), (right - fold, top + fold), (right, top + fold)],
                 fill=FOLD, outline=PAGE_EDGE, width=16)
    if pdf:
        draw.rectangle([left + 64, top + 96, left + 352, top + 176], fill=PDF_RED)
    page.alpha_composite(resized(logo, 512), ((s - 512) // 2, 400))
    return resized(page, n)


def png_bytes(img: Image.Image) -> bytes:
    buffer = io.BytesIO()
    img.save(buffer, "PNG")
    return buffer.getvalue()


def _bitmap(img: Image.Image) -> bytes:
    """A 32-bit BGRA DIB with its AND mask, as icons store images below 256 px."""
    w, h = img.size
    header = struct.pack("<IiiHHIIiiII", 40, w, 2 * h, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    alpha = rows.getchannel("A").tobytes()
    stride = (w + 31) // 32 * 4
    mask = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if alpha[y * w + x] == 0:
                mask[y * stride + x // 8] |= 0x80 >> (x % 8)
    return header + rows.tobytes("raw", "BGRA") + bytes(mask)


def ico_bytes(images: list[Image.Image]) -> bytes:
    blobs = [png_bytes(img) if img.width >= 256 else _bitmap(img) for img in images]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for img, blob in zip(images, blobs):
        out += struct.pack("<BBBBHHII", img.width % 256, img.height % 256, 0, 0, 1, 32,
                           len(blob), offset)
        offset += len(blob)
    return out + b"".join(blobs)


def svg_bytes(img: Image.Image) -> bytes:
    data = base64.b64encode(png_bytes(img)).decode("ascii")
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{img.width}" height="{img.height}" '
            f'viewBox="0 0 {img.width} {img.height}"><image width="{img.width}" '
            f'height="{img.height}" href="data:image/png;base64,{data}"/></svg>\n').encode()


def svg_png(svg: bytes) -> bytes:
    """The PNG inside an SVG svg_bytes made."""
    return base64.b64decode(re.search(rb"base64,([A-Za-z0-9+/=]+)", svg).group(1))


def outputs(source: Image.Image, small: Image.Image | None) -> dict[str, bytes]:
    master = frame(remove_background(source))
    small_master = frame(remove_background(small)) if small is not None else master

    def logo(n: int) -> Image.Image:
        return resized(small_master if n <= SMALL_MAX else master, n)

    files = {
        "win/app.ico": ico_bytes([logo(n) for n in ICO_SIZES]),
        "win/doc.ico": ico_bytes([page_icon(master, n, pdf=False) for n in ICO_SIZES]),
        "win/pdf.ico": ico_bytes([page_icon(master, n, pdf=True) for n in ICO_SIZES]),
        **{name: png_bytes(placed(master, side, logo_side))
           for name, (side, logo_side) in TILES.items()},
        **{f"default_{scale}00_percent/product_logo_{n}.png": png_bytes(logo(n * scale))
           for scale in (1, 2, 3) for n in (16, 32)},
        **{f"product_logo_{n}.png": png_bytes(logo(n)) for n in (16, 24, 64, 128, 256)},
        "product_logo.svg": svg_bytes(logo(256)),
    }
    assert set(files) == set(OUTPUTS)
    return files


def _open(path: Path) -> Image.Image:
    img = Image.open(path)
    img.load()
    return img


def generate(source: Path, small: Path) -> dict[str, bytes]:
    if not source.exists():
        raise BrandIconsError(f"{source} is missing")
    img = _open(source)
    if min(img.size) < 256:
        raise BrandIconsError(f"{source} is {img.width}x{img.height}; it needs at least 256 px "
                              "on its shorter side")
    return outputs(img, _open(small) if small.exists() else None)


def write(out: Path, files: dict[str, bytes]) -> None:
    """Makes `out` hold exactly `files`: a file the tool no longer makes doesn't linger."""
    for old in [p for p in out.rglob("*") if p.is_file()] if out.exists() else []:
        if old.relative_to(out).as_posix() not in files:
            old.unlink()
    for name, data in files.items():
        path = out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def contact_sheet(files: dict[str, bytes]) -> Image.Image:
    """Every icon at its size, then enlarged 4x up to 48 px, on light and on dark."""
    rows = []
    for name in ("win/app.ico", "win/doc.ico", "win/pdf.ico"):
        ico = Image.open(io.BytesIO(files[name])).ico
        images = [ico.getimage((n, n)).convert("RGBA") for n in ICO_SIZES]
        rows.append(images)
        rows.append([img.resize((img.width * 4, img.height * 4), Image.Resampling.NEAREST)
                     for img in images if img.width <= 48])
    rows.append([Image.open(io.BytesIO(files[name])).convert("RGBA") for name in TILES])
    pad = 16
    width = max(sum(img.width + pad for img in row) for row in rows) + pad
    height = sum(max(img.height for img in row) + pad for row in rows) + pad
    sheet = Image.new("RGBA", (2 * width, height), (243, 243, 243, 255))
    sheet.paste((32, 32, 32, 255), (width, 0, 2 * width, height))
    y = pad
    for row in rows:
        x = pad
        for img in row:
            sheet.alpha_composite(img, (x, y))
            sheet.alpha_composite(img, (width + x, y))
            x += img.width + pad
        y += max(img.height for img in row) + pad
    return sheet


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=LOGO_DIR / "source.png")
    parser.add_argument("--small", type=Path, default=LOGO_DIR / "small.png",
                        help=f"used up to {SMALL_MAX} px when it exists")
    parser.add_argument("--out", type=Path, default=THEME_DIR)
    parser.add_argument("--preview", type=Path, help="also write sheet.png here")
    args = parser.parse_args(argv)
    try:
        files = generate(args.source, args.small)
    except BrandIconsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    write(args.out, files)
    print(f"wrote {len(files)} files to {args.out}")
    if args.preview:
        args.preview.mkdir(parents=True, exist_ok=True)
        contact_sheet(files).save(args.preview / "sheet.png")
        print(f"contact sheet: {args.preview / 'sheet.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
