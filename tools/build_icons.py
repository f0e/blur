# /// script
# requires-python = ">=3.11"
# dependencies = ["fonttools==4.60.1", "skia-pathops==0.9.2"]
# ///
"""Builds the gui's icon font (src/gui/fonts/icons.h) from the svgs in src/gui/fonts/icons.

icons.toml maps each icon name to the character it's drawn with, and each name to <name>.svg.
usage: uv run tools/build_icons.py
"""

from io import BytesIO
from pathlib import Path

import pathops
import tomllib
from fontTools.fontBuilder import FontBuilder
from fontTools.misc.timeTools import timestampSinceEpoch
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import SVGPath

ROOT = Path(__file__).resolve().parent.parent
ICONS_DIR = ROOT / "src" / "gui" / "fonts" / "icons"
HEADER_PATH = ROOT / "src" / "gui" / "fonts" / "icons.h"

UNITS_PER_EM = 1000
ASCENT = 850
DESCENT = -150
FAMILY_NAME = "icons"

# imgui's stb_compress (dependencies/imgui/misc/fonts/binary_to_compressed_c.cpp), so the output loads with
# AddFontFromMemoryCompressedTTF
STB_WINDOW = 0x40000
STB_HASH_SIZE = 32768
U32 = 0xFFFFFFFF


def adler32(adler: int, data: bytes) -> int:
    s1, s2 = adler & 0xFFFF, adler >> 16
    for b in data:
        s1 = (s1 + b) % 65521
        s2 = (s2 + s1) % 65521
    return (s2 << 16) | s1


def stb_compress(data: bytes) -> bytes:
    out = bytearray()

    def out_n(v: int, n: int) -> None:
        out.extend(((v >> (8 * i)) & 0xFF) for i in reversed(range(n)))

    def out_literals(start: int, count: int) -> None:
        while count > 65536:
            out_literals(start, 65536)
            start += 65536
            count -= 65536
        if count == 0:
            return
        if count <= 32:
            out_n(0x20 + count - 1, 1)
        elif count <= 2048:
            out_n(0x800 + count - 1, 2)
        else:
            out_n(0x070000 + count - 1, 3)
        out.extend(data[start : start + count])

    def not_crap(best: int, dist: int) -> bool:
        return (best > 2 and dist <= 0x100) or (best > 5 and dist <= 0x4000) or (best > 7 and dist <= 0x80000)

    def match_len(a: int, b: int, max_len: int) -> int:
        i = 0
        while i < max_len and data[a + i] == data[b + i]:
            i += 1
        return i

    length = len(data)
    mask = STB_HASH_SIZE - 1
    chash: list[int | None] = [None] * STB_HASH_SIZE

    out_n(0x57BC, 2)
    out_n(0, 2)
    out_n(0, 4)
    out_n(length, 4)
    out_n(STB_WINDOW, 4)

    def scramble(h: int) -> int:
        return (h + (h >> 16)) & mask

    def hc2(q: int, h: int, c: int, d: int) -> int:
        return (((h << 14) & U32) + (h >> 18) + (data[q + c] << 7) + data[q + d]) & U32

    q = 0
    lit_start = 0
    while q < length and q + 12 < length:
        best, dist = 2, 0
        match_max = min(length - q, 65536)

        h = (data[q] << 14) + (data[q + 1] << 7) + data[q + 2]
        h1 = scramble(h)
        h = hc2(q, h, 3, 4)
        h2 = scramble(h)
        h = hc2(q, hc2(q, h, 5, 6), 7, 8)
        h3 = scramble(h)
        h = hc2(q, hc2(q, h, 9, 10), 11, 12)
        h4 = scramble(h)

        for slot, check_dist in ((h1, False), (h2, True), (h3, True), (h4, True)):
            t = chash[slot]
            if t is None or (check_dist and dist == q - t):
                continue
            m = match_len(t, q, match_max)
            if m > best and q - t <= STB_WINDOW and (m > 9 or not_crap(m, q - t)):
                best, dist = m, q - t

        chash[h1] = chash[h2] = chash[h3] = chash[h4] = q

        if best < 3:
            q += 1
            continue

        if best <= 0x80 and dist <= 0x100:
            ops = [(0x80 + best - 1, 1), (dist - 1, 1)]
        elif best > 5 and best <= 0x100 and dist <= 0x4000:
            ops = [(0x4000 + dist - 1, 2), (best - 1, 1)]
        elif best > 7 and best <= 0x100 and dist <= 0x80000:
            ops = [(0x180000 + dist - 1, 3), (best - 1, 1)]
        elif best > 8 and best <= 0x10000 and dist <= 0x80000:
            ops = [(0x100000 + dist - 1, 3), (best - 1, 2)]
        elif best > 9 and dist <= 0x1000000:
            best = min(best, 65536)
            if best <= 0x100:
                ops = [(0x06, 1), (dist - 1, 3), (best - 1, 1)]
            else:
                ops = [(0x04, 1), (dist - 1, 3), (best - 1, 2)]
        else:
            q += 1
            continue

        out_literals(lit_start, q - lit_start)
        q += best
        lit_start = q
        for v, n in ops:
            out_n(v, n)

    out_literals(lit_start, length - lit_start)
    out_n(0x05FA, 2)
    out_n(adler32(1, data), 4)
    return bytes(out)


def stb_decompress(src: bytes) -> bytes:
    def be(i: int, n: int) -> int:
        return int.from_bytes(src[i : i + n], "big")

    if be(0, 4) != 0x57BC0000 or be(4, 4) != 0:
        raise ValueError("not an stb compressed stream")

    out = bytearray()

    def match(dist: int, n: int) -> None:
        start = len(out) - dist
        for k in range(n):
            out.append(out[start + k])

    i = 16
    while True:
        c = src[i]
        if c >= 0x80:
            match(src[i + 1] + 1, c - 0x80 + 1)
            i += 2
        elif c >= 0x40:
            match(be(i, 2) - 0x4000 + 1, src[i + 2] + 1)
            i += 3
        elif c >= 0x20:
            n = c - 0x20 + 1
            out.extend(src[i + 1 : i + 1 + n])
            i += 1 + n
        elif c >= 0x18:
            match(be(i, 3) - 0x180000 + 1, src[i + 3] + 1)
            i += 4
        elif c >= 0x10:
            match(be(i, 3) - 0x100000 + 1, be(i + 3, 2) + 1)
            i += 5
        elif c >= 0x08:
            n = be(i, 2) - 0x0800 + 1
            out.extend(src[i + 2 : i + 2 + n])
            i += 2 + n
        elif c == 0x07:
            n = be(i + 1, 2) + 1
            out.extend(src[i + 3 : i + 3 + n])
            i += 3 + n
        elif c == 0x06:
            match(be(i + 1, 3) + 1, src[i + 4] + 1)
            i += 5
        elif c == 0x04:
            match(be(i + 1, 3) + 1, be(i + 4, 2) + 1)
            i += 6
        elif c == 0x05 and src[i + 1] == 0xFA:
            if len(out) != be(8, 4) or adler32(1, bytes(out)) != be(i + 2, 4):
                raise ValueError("stb stream failed its length or checksum check")
            return bytes(out)
        else:
            raise ValueError(f"bad stb opcode {c:#x} at {i}")


def load_icons() -> dict[str, str]:
    with (ICONS_DIR / "icons.toml").open("rb") as f:
        icons = tomllib.load(f)

    chars = list(icons.values())
    for name, char in icons.items():
        if len(char) != 1 or not char.isascii():
            raise SystemExit(f"{name}: '{char}' should be a single ascii character")
        if chars.count(char) > 1:
            raise SystemExit(f"{name}: '{char}' is used by more than one icon")
        if not (ICONS_DIR / f"{name}.svg").exists():
            raise SystemExit(f"{name}: missing {name}.svg")

    return icons


def load_outline(path: Path) -> pathops.Path:
    recording = RecordingPen()
    SVGPath(str(path), transform=(1, 0, 0, -1, 0, ASCENT)).draw(recording)

    contours: list[pathops.Path] = []
    pen = None
    for op, args in recording.value:
        if op == "moveTo":
            contours.append(pathops.Path())
            pen = contours[-1].getPen()
        getattr(pen, op)(*args)

    for contour in contours:
        contour.simplify(fix_winding=True)

    # whether a contour is a hole goes by how deeply it's nested rather than its winding, since svg editors (and the
    # old fontello glyphs) overlap shapes drawn in opposite directions, which would cut the overlap out
    def contains(outer: pathops.Path, inner: pathops.Path) -> bool:
        return not list(pathops.op(inner, outer, pathops.PathOp.DIFFERENCE).contours)

    depths = [sum(contains(other, contour) for other in contours if other is not contour) for contour in contours]

    outline = pathops.Path()
    for depth, contour in sorted(zip(depths, contours, strict=True), key=lambda item: item[0]):
        op = pathops.PathOp.DIFFERENCE if depth % 2 else pathops.PathOp.UNION
        outline = pathops.op(outline, contour, op)

    return outline


def build_font(icons: dict[str, str]) -> bytes:
    glyph_order = [".notdef", *icons]
    glyphs = {".notdef": TTGlyphPen(None).glyph()}
    metrics = {".notdef": (UNITS_PER_EM, 0)}

    for name in icons:
        pen = TTGlyphPen(None)
        load_outline(ICONS_DIR / f"{name}.svg").draw(Cu2QuPen(pen, max_err=1.0))
        glyph = pen.glyph(dropImpliedOnCurves=True)
        glyph.recalcBounds(None)

        glyphs[name] = glyph
        metrics[name] = (UNITS_PER_EM, getattr(glyph, "xMin", 0))

    fb = FontBuilder(UNITS_PER_EM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap({ord(char): name for name, char in icons.items()})
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    fb.setupNameTable({"familyName": FAMILY_NAME, "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT, usWinAscent=ASCENT, usWinDescent=-DESCENT)
    fb.setupPost()
    # fixed so rebuilding the same svgs gives the same header
    fb.updateHead(created=timestampSinceEpoch(0), modified=timestampSinceEpoch(0))

    buf = BytesIO()
    fb.save(buf)
    return buf.getvalue()


def write_header(icons: dict[str, str], compressed: bytes) -> None:
    lines = ["#pragma once", "", "#include <string>", "", "namespace icons {"]
    lines += [f'\tinline const std::string {name.upper()} = "{char}";' for name, char in icons.items()]
    lines += [
        "}",
        "",
        f"inline constexpr std::array<unsigned char, {len(compressed)}> ICONS_COMPRESSED_DATA = {{",
        "\t// clang-format off",
    ]

    row = ""
    for b in compressed:
        row += f"{b},"
        if len(row) >= 96:
            lines.append("\t" + row)
            row = ""
    if row:
        lines.append("\t" + row)

    lines += ["\t// clang-format on", "};", ""]
    HEADER_PATH.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    icons = load_icons()
    font = build_font(icons)
    compressed = stb_compress(font)
    if stb_decompress(compressed) != font:
        raise SystemExit("compressed font didn't round trip")

    write_header(icons, compressed)
    print(f"wrote {len(icons)} icons to {HEADER_PATH.relative_to(ROOT)} ({len(font)} -> {len(compressed)} bytes)")


if __name__ == "__main__":
    main()
