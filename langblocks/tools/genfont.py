#!/usr/bin/env python3
"""Generate source/font5x7.h from readable ASCII-art glyph definitions.

The DSi build must not depend on any external font asset, so the font lives in
the repository as this script plus the header it emits.  Each glyph is 5 pixels
wide and 7 tall, drawn with '#' (ink) and '.' (paper).  Accented characters are
composed automatically from a base letter plus a diacritic pattern, which keeps
the table small and guarantees that 'e' and 'é' line up.

Usage:
    python3 tools/genfont.py            # writes source/font5x7.h + preview
    python3 tools/genfont.py --png out.png   # only the preview image
"""

import os
import sys
import zlib
import struct

W, H = 5, 7
ADVANCE = 6
LINE = 9

# ---------------------------------------------------------------------------
# ASCII 32..126.  Seven rows of five characters, '#' = ink.
# ---------------------------------------------------------------------------
GLYPHS = {
    ' ': ".....|.....|.....|.....|.....|.....|.....",
    '!': "..#..|..#..|..#..|..#..|..#..|.....|..#..",
    '"': ".#.#.|.#.#.|.....|.....|.....|.....|.....",
    '#': ".#.#.|.#.#.|#####|.#.#.|#####|.#.#.|.#.#.",
    '$': "..#..|.####|#.#..|.###.|..#.#|####.|..#..",
    '%': "##..#|##..#|...#.|..#..|.#...|#..##|#..##",
    '&': ".##..|#..#.|#.#..|.#...|#.#.#|#..#.|.##.#",
    "'": "..#..|..#..|.....|.....|.....|.....|.....",
    '(': "...#.|..#..|.#...|.#...|.#...|..#..|...#.",
    ')': ".#...|..#..|...#.|...#.|...#.|..#..|.#...",
    '*': ".....|#.#.#|.###.|#####|.###.|#.#.#|.....",
    '+': ".....|..#..|..#..|#####|..#..|..#..|.....",
    ',': ".....|.....|.....|.....|.....|..##.|..#..",
    '-': ".....|.....|.....|#####|.....|.....|.....",
    '.': ".....|.....|.....|.....|.....|..##.|..##.",
    '/': "....#|...#.|...#.|..#..|.#...|.#...|#....",
    '0': ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    '1': "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    '2': ".###.|#...#|....#|...#.|..#..|.#...|#####",
    '3': "####.|....#|....#|.###.|....#|....#|####.",
    '4': "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    '5': "#####|#....|####.|....#|....#|#...#|.###.",
    '6': "..##.|.#...|#....|####.|#...#|#...#|.###.",
    '7': "#####|....#|...#.|..#..|.#...|.#...|.#...",
    '8': ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    '9': ".###.|#...#|#...#|.####|....#|...#.|.##..",
    ':': ".....|..##.|..##.|.....|..##.|..##.|.....",
    ';': ".....|..##.|..##.|.....|..##.|..#..|.#...",
    '<': "...#.|..#..|.#...|#....|.#...|..#..|...#.",
    '=': ".....|.....|#####|.....|#####|.....|.....",
    '>': ".#...|..#..|...#.|....#|...#.|..#..|.#...",
    '?': ".###.|#...#|....#|...#.|..#..|.....|..#..",
    '@': ".###.|#...#|#.###|#.#.#|#.###|#....|.###.",
    'A': ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    'B': "####.|#...#|#...#|####.|#...#|#...#|####.",
    'C': ".###.|#...#|#....|#....|#....|#...#|.###.",
    'D': "###..|#..#.|#...#|#...#|#...#|#..#.|###..",
    'E': "#####|#....|#....|####.|#....|#....|#####",
    'F': "#####|#....|#....|####.|#....|#....|#....",
    'G': ".###.|#...#|#....|#.###|#...#|#...#|.###.",
    'H': "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    'I': ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
    'J': "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
    'K': "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    'L': "#....|#....|#....|#....|#....|#....|#####",
    'M': "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
    'N': "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
    'O': ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    'P': "####.|#...#|#...#|####.|#....|#....|#....",
    'Q': ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
    'R': "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    'S': ".###.|#...#|#....|.###.|....#|#...#|.###.",
    'T': "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    'U': "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
    'V': "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
    'W': "#...#|#...#|#...#|#.#.#|#.#.#|##.##|#...#",
    'X': "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
    'Y': "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    'Z': "#####|....#|...#.|..#..|.#...|#....|#####",
    '[': ".###.|.#...|.#...|.#...|.#...|.#...|.###.",
    '\\': "#....|.#...|.#...|..#..|...#.|...#.|....#",
    ']': ".###.|...#.|...#.|...#.|...#.|...#.|.###.",
    '^': "..#..|.#.#.|#...#|.....|.....|.....|.....",
    '_': ".....|.....|.....|.....|.....|.....|#####",
    '`': ".#...|..#..|.....|.....|.....|.....|.....",
    'a': ".....|.....|.###.|....#|.####|#...#|.####",
    'b': "#....|#....|####.|#...#|#...#|#...#|####.",
    'c': ".....|.....|.###.|#....|#....|#...#|.###.",
    'd': "....#|....#|.####|#...#|#...#|#...#|.####",
    'e': ".....|.....|.###.|#...#|#####|#....|.###.",
    'f': "..##.|.#..#|.#...|####.|.#...|.#...|.#...",
    'g': ".....|.###.|#...#|#...#|.####|....#|.###.",
    'h': "#....|#....|#.##.|##..#|#...#|#...#|#...#",
    'i': "..#..|.....|.##..|..#..|..#..|..#..|.###.",
    'j': "...#.|.....|..##.|...#.|...#.|#..#.|.##..",
    'k': "#....|#....|#..#.|#.#..|##...|#.#..|#..#.",
    'l': ".##..|..#..|..#..|..#..|..#..|..#..|.###.",
    'm': ".....|.....|##.#.|#.#.#|#.#.#|#...#|#...#",
    'n': ".....|.....|####.|#...#|#...#|#...#|#...#",
    'o': ".....|.....|.###.|#...#|#...#|#...#|.###.",
    'p': ".....|.....|####.|#...#|#...#|####.|#....",
    'q': ".....|.....|.####|#...#|#...#|.####|....#",
    'r': ".....|.....|#.##.|##..#|#....|#....|#....",
    's': ".....|.....|.###.|#....|.###.|....#|.###.",
    't': ".#...|.#...|####.|.#...|.#...|.#..#|..##.",
    'u': ".....|.....|#...#|#...#|#...#|#..##|.##.#",
    'v': ".....|.....|#...#|#...#|#...#|.#.#.|..#..",
    'w': ".....|.....|#...#|#...#|#.#.#|#.#.#|.#.#.",
    'x': ".....|.....|#...#|.#.#.|..#..|.#.#.|#...#",
    'y': ".....|.....|#...#|#...#|.####|....#|.###.",
    'z': ".....|.....|#####|...#.|..#..|.#...|#####",
    '{': "...##|..#..|..#..|.#...|..#..|..#..|...##",
    '|': "..#..|..#..|..#..|..#..|..#..|..#..|..#..",
    '}': "##...|..#..|..#..|...#.|..#..|..#..|##...",
    '~': ".....|.....|.#...|#.#.#|...#.|.....|.....",
}

# ---------------------------------------------------------------------------
# Diacritics.  Each pattern is one or two rows of five characters that sit in
# the two blank rows above a lowercase x-height letter.
# ---------------------------------------------------------------------------
ACUTE     = ["...#.", "..#.."]
GRAVE     = [".#...", "..#.."]
CIRCUMFLEX = ["..#..", ".#.#."]
DIAERESIS = [".#.#.", "....."]
TILDE     = [".##.#", "#.##."]

# (character, base letter, pattern).  Uppercase variants are generated from the
# same base letter and use only the bottom row of the pattern so that the
# capital still fits in the 7-row cell.
ACCENTED = [
    ("á", "a", ACUTE), ("é", "e", ACUTE), ("í", "i", ACUTE),
    ("ó", "o", ACUTE), ("ú", "u", ACUTE),
    ("à", "a", GRAVE), ("è", "e", GRAVE), ("ù", "u", GRAVE),
    ("â", "a", CIRCUMFLEX), ("ê", "e", CIRCUMFLEX), ("î", "i", CIRCUMFLEX),
    ("ô", "o", CIRCUMFLEX), ("û", "u", CIRCUMFLEX),
    ("ë", "e", DIAERESIS), ("ï", "i", DIAERESIS), ("ü", "u", DIAERESIS),
    ("ñ", "n", TILDE),
    ("Á", "A", ACUTE), ("É", "E", ACUTE), ("Í", "I", ACUTE),
    ("Ó", "O", ACUTE), ("Ú", "U", ACUTE),
    ("À", "A", GRAVE), ("È", "E", GRAVE), ("Ù", "U", GRAVE),
    ("Â", "A", CIRCUMFLEX), ("Ê", "E", CIRCUMFLEX), ("Î", "I", CIRCUMFLEX),
    ("Ô", "O", CIRCUMFLEX), ("Û", "U", CIRCUMFLEX),
    ("Ë", "E", DIAERESIS), ("Ï", "I", DIAERESIS), ("Ü", "U", DIAERESIS),
    ("Ñ", "N", TILDE),
]

# Characters that cannot be composed: the cedilla hangs below the baseline and
# the oe ligature is its own shape.
SPECIALS = {
    "ç": [".###.", "#....", "#....", ".###.", "..#..", "..#..", ".##.."],
    "Ç": [".###.", "#....", "#....", "#....", ".###.", "..#..", ".##.."],
    "œ": [".....", ".....", ".####", "#.#.#", "#.#.#", "#.#.#", ".####"],
    "Œ": ["#####", "#.#.#", "#.#.#", "#####", "#.#.#", "#.#.#", "#.#.#"],
    "¡": ["..#..", ".....", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "¿": ["..#..", ".....", "..#..", ".#...", "#....", "#...#", ".###."],
}


def rows_of(text):
    rows = text.split("|")
    assert len(rows) == H, (text, len(rows))
    for r in rows:
        assert len(r) == W, (text, r)
    return rows


def bits(rows):
    """Pack each row into a byte; bit 4 is the leftmost pixel."""
    out = []
    for r in rows:
        v = 0
        for i, ch in enumerate(r):
            if ch == "#":
                v |= 1 << (W - 1 - i)
        out.append(v)
    return out


def build_accented():
    """Compose á é ñ ... from their base letter plus a diacritic pattern."""
    table = {}
    for ch, base, pattern in ACCENTED:
        stem = rows_of(GLYPHS[base])
        if base.islower():
            # x-height letters already have rows 0..1 free (í uses a dotless i)
            if base == "i":
                stem = [".....", ".....", ".##..", "..#..", "..#..", "..#..", ".###."]
            rows = [pattern[0], pattern[1]] + stem[2:]
        else:
            # capital: one accent row on top, letter squeezed into 6 rows so the
            # baseline still matches its unaccented neighbours
            rows = [pattern[1]] + stem[0:6]
        table[ch] = rows
    return table


def emit_header(path):
    accented = build_accented()
    lines = []
    lines.append("/* Auto-generated by tools/genfont.py -- do not edit by hand.")
    lines.append(" * 5x7 monospace glyphs, bit 4 = leftmost pixel of each row. */")
    lines.append("#ifndef FONT5X7_H")
    lines.append("#define FONT5X7_H")
    lines.append("")
    lines.append('#include "platform.h"')
    lines.append("")
    lines.append("#define FONT_W 5")
    lines.append("#define FONT_H 7")
    lines.append("#define FONT_ADVANCE 6")
    lines.append("#define FONT_LINE 9")
    lines.append("")
    lines.append("/* Printable ASCII, 0x20..0x7e. */")
    lines.append("static const u8 fontAscii[95][FONT_H] = {")
    for code in range(0x20, 0x7F):
        ch = chr(code)
        data = bits(rows_of(GLYPHS[ch]))
        label = "space" if ch == " " else ch
        if ch == "\\":
            label = "backslash"
        lines.append("    { %s },  /* %s */" % (", ".join("0x%02x" % b for b in data), label))
    lines.append("};")
    lines.append("")
    lines.append("/* Accented Latin-1/Unicode code points not present above. */")
    lines.append("typedef struct { u16 cp; u8 rows[FONT_H]; } ExtraGlyph;")
    lines.append("")
    lines.append("static const ExtraGlyph fontExtra[] = {")
    entries = [(ord(c), rows) for c, rows in accented.items()]
    entries += [(ord(c), rows) for c, rows in SPECIALS.items()]
    entries.sort(key=lambda e: e[0])
    for cp, rows in entries:
        data = bits(rows)
        lines.append("    { 0x%04x, { %s } },  /* %s */"
                     % (cp, ", ".join("0x%02x" % b for b in data), chr(cp)))
    lines.append("};")
    lines.append("")
    lines.append("static const unsigned int fontExtraCount =")
    lines.append("    (unsigned int)(sizeof(fontExtra) / sizeof(fontExtra[0]));")
    lines.append("")
    lines.append("#endif /* FONT5X7_H */")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d ASCII + %d extra glyphs)" % (path, 95, len(entries)))
    return accented


# ---------------------------------------------------------------------------
# Optional preview PNG so the glyphs can be eyeballed without an emulator.
# ---------------------------------------------------------------------------
def png_write(path, w, h, pixels):
    raw = b""
    for y in range(h):
        raw += b"\x00" + bytes(pixels[y * w * 3:(y + 1) * w * 3])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    hdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", hdr) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def render_preview(path):
    scale = 3
    samples = [
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        "abcdefghijklmnopqrstuvwxyz",
        "0123456789 !?.,:;'\"-+/()[]{}",
        "Español: ¿Qué tal? ¡Muy bien!",
        "Français: Ça va très bien, à bientôt.",
        "El año pasado estudié francés.",
        "ñ á é í ó ú ü ç è ê ë î ï ô ù û œ",
        "Ñ Á É Í Ó Ú Ü Ç È Ê Ë Î Ï Ô Ù Û Œ",
    ]
    cw, ch = (W + 1) * scale, LINE * scale
    w = 20 + max(len(s) for s in samples) * cw + 20
    h = 40 + len(samples) * (ch + 6) + 20
    pix = bytearray(w * h * 3)
    for i in range(w * h):
        pix[i * 3:i * 3 + 3] = b"\xf2\xf6\xff"
    tiles = {}
    for code in range(0x20, 0x7f):
        tiles[chr(code)] = bits(rows_of(GLYPHS[chr(code)]))
    for chx, rows in build_accented().items():
        tiles[chx] = bits(rows)
    for chx, rows in SPECIALS.items():
        tiles[chx] = bits(rows)

    def draw_text(txt, ox, oy):
        x = ox
        for chh in txt:
            data = tiles.get(chh)
            if data is None:
                x += cw
                continue
            for ry in range(H):
                for rx in range(W):
                    if data[ry] & (1 << (W - 1 - rx)):
                        for sy in range(scale):
                            for sx in range(scale):
                                p = ((oy + ry * scale + sy) * w + (x + rx * scale + sx)) * 3
                                pix[p:p + 3] = b"\x14\x20\x38"
            x += cw

    for i, s in enumerate(samples):
        draw_text(s, 10, 20 + i * (ch + 6))
    png_write(path, w, h, pix)
    print("wrote %s" % path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    if "--png" in sys.argv:
        render_preview(sys.argv[sys.argv.index("--png") + 1])
        return
    emit_header(os.path.join(root, "source", "font5x7.h"))
    render_preview(os.path.join(root, "font_preview.png"))


if __name__ == "__main__":
    main()
