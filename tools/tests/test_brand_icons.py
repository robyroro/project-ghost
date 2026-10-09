# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import io
import struct
import tempfile
import unittest
from pathlib import Path

from PIL import Image  # Required: without Pillow this test fails rather than skips.

import brand_icons
import pe_resources

BLUE = (0, 0, 255, 255)
WHITE = (255, 255, 255, 255)


def square_on_white(size=40, inner=20, color=BLUE) -> Image.Image:
    img = Image.new("RGBA", (size, size), WHITE)
    start = (size - inner) // 2
    img.paste(color, (start, start, start + inner, start + inner))
    return img


class BackgroundTest(unittest.TestCase):
    def test_white_around_the_logo_becomes_transparent(self):
        out = brand_icons.remove_background(square_on_white())
        self.assertEqual(out.getpixel((0, 0))[3], 0)
        self.assertEqual(out.getpixel((20, 20)), BLUE)

    def test_white_enclosed_by_the_logo_stays(self):
        img = square_on_white(inner=30)
        img.paste(WHITE, (15, 15, 25, 25))
        out = brand_icons.remove_background(img)
        self.assertEqual(out.getpixel((20, 20)), WHITE)

    def test_an_edge_mixed_with_white_is_unmixed(self):
        img = square_on_white()
        img.putpixel((9, 20), (128, 128, 255, 255))  # half blue, half white, beside the background
        r, g, b, a = brand_icons.remove_background(img).getpixel((9, 20))
        self.assertEqual((r, g, b), (0, 0, 255))
        self.assertIn(a, (127, 128))

    def test_a_transparent_source_is_left_as_it_is(self):
        img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        img.putpixel((4, 4), BLUE)
        self.assertEqual(brand_icons.remove_background(img).tobytes(), img.tobytes())

    def test_a_source_without_a_white_border_is_refused(self):
        with self.assertRaisesRegex(brand_icons.BrandIconsError, "border"):
            brand_icons.remove_background(Image.new("RGBA", (8, 8), BLUE))

    def test_an_all_white_source_is_refused(self):
        with self.assertRaisesRegex(brand_icons.BrandIconsError, "no logo"):
            brand_icons.remove_background(Image.new("RGBA", (8, 8), WHITE))


class FrameTest(unittest.TestCase):
    def test_centres_the_logo_on_a_square_with_a_margin(self):
        img = Image.new("RGBA", (300, 200), (0, 0, 0, 0))
        img.paste(BLUE, (50, 50, 150, 100))  # a 100 x 50 logo
        out = brand_icons.frame(img)
        self.assertEqual(out.size, (107, 107))  # 100 / (1 - 2/32), rounded
        self.assertEqual(out.getchannel("A").getbbox(), (3, 28, 103, 78))


class IcoTest(unittest.TestCase):
    def test_small_images_are_bitmaps_and_256_is_png(self):
        images = [Image.new("RGBA", (n, n), BLUE) for n in (16, 256)]
        entries = pe_resources.ico_images(brand_icons.ico_bytes(images))
        self.assertEqual(len(entries), 2)
        size, width, height = struct.unpack_from("<Iii", entries[0])
        self.assertEqual((size, width, height), (40, 16, 32))  # height counts the AND mask
        self.assertTrue(entries[1].startswith(b"\x89PNG"))

    def test_bitmap_rows_are_bottom_up_bgra(self):
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        img.putpixel((0, 15), (1, 2, 3, 255))  # bottom-left pixel comes first
        entry = pe_resources.ico_images(brand_icons.ico_bytes([img]))[0]
        self.assertEqual(entry[40:44], bytes((3, 2, 1, 255)))

    def test_windows_reads_the_sizes_back(self):
        images = [Image.new("RGBA", (n, n), BLUE) for n in brand_icons.ICO_SIZES]
        read = Image.open(io.BytesIO(brand_icons.ico_bytes(images)))
        self.assertEqual(sorted(read.info["sizes"]), [(n, n) for n in brand_icons.ICO_SIZES])


class GenerateTest(unittest.TestCase):
    def test_makes_every_file(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / "source.png"
            square_on_white(size=600, inner=400).save(source)
            files = brand_icons.generate(source, Path(d) / "small.png")
        self.assertEqual(set(files), set(brand_icons.OUTPUTS))

    def test_refuses_a_small_or_missing_source(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / "source.png"
            with self.assertRaisesRegex(brand_icons.BrandIconsError, "missing"):
                brand_icons.generate(source, Path(d) / "small.png")
            square_on_white(size=200, inner=100).save(source)
            with self.assertRaisesRegex(brand_icons.BrandIconsError, "256"):
                brand_icons.generate(source, Path(d) / "small.png")

    def test_write_removes_files_it_no_longer_makes(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            (out / "old.png").write_bytes(b"x")
            brand_icons.write(out, {"win/app.ico": b"y"})
            self.assertEqual([p.relative_to(out).as_posix() for p in out.rglob("*")
                              if p.is_file()], ["win/app.ico"])


def pixels(name: str, data: bytes):
    """What a generated file looks like, independent of how PNG compressed it."""
    def png(blob):
        img = Image.open(io.BytesIO(blob)).convert("RGBA")
        return img.size, img.tobytes()
    if name.endswith(".ico"):
        return [png(e) if e.startswith(b"\x89PNG") else e for e in pe_resources.ico_images(data)]
    if name.endswith(".svg"):
        return png(brand_icons.svg_png(data))
    return png(data)


class CommittedIconsTest(unittest.TestCase):
    def test_the_committed_icons_are_the_tools_output(self):
        files = brand_icons.generate(brand_icons.LOGO_DIR / "source.png",
                                     brand_icons.LOGO_DIR / "small.png")
        theme = brand_icons.THEME_DIR
        committed = {p.relative_to(theme).as_posix() for p in theme.rglob("*") if p.is_file()}
        self.assertEqual(committed, set(files), "run tools/brand_icons.py and commit its output")
        for name, data in files.items():
            with self.subTest(name):
                self.assertEqual(pixels(name, (theme / name).read_bytes()), pixels(name, data),
                                 "run tools/brand_icons.py and commit its output")


if __name__ == "__main__":
    unittest.main()
