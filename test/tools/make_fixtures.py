"""Writes the generated RTF fixtures.

Usage: python test/tools/make_fixtures.py <fixtures_dir>

The script writes word_text.rtf, pictures.rtf and the eight malformed_*.rtf
files. Three malformed files are derived from hand-written fixtures in the
same directory, so those must exist first. The output is the same bytes on
every run: there are no timestamps, and the PNG is built from fixed data.
"""

import os
import struct
import sys
import zlib


def png_1x1():
    """Returns a valid 1x1 red PNG built with struct and zlib."""

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)  # 1x1, 8 bits, RGB
    pixels = b"\x00\xff\x00\x00"  # filter byte, then one red pixel
    # Level 0 stores the data without compression, so every zlib build gives the same bytes.
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels, 0))
            + chunk(b"IEND", b""))


def hex_lines(data):
    """Returns the bytes as lowercase hex with a CRLF after every 128 hex characters."""
    text = data.hex().encode("ascii")
    lines = [text[i:i + 128] for i in range(0, len(text), 128)]
    return b"\r\n".join(lines)


def word_text():
    out = [
        rb"{\rtf1\ansi\ansicpg1252\uc1\deff0",
        rb"{\fonttbl{\f0\froman\fcharset0 Times New Roman;}{\f3\froman\fcharset2 Symbol;}"
        rb"{\f5\froman\fcharset204 Times New Roman Cyr;}}",
        rb"\pard Euro sign: \u8364?\par",
        rb"\pard Sigma with a fallback hex escape: \u963\'73\par",
        rb"\pard {\uc0\u20013 text right after the character}\par",
        rb"\pard {\uc2\u20013\'a4\'a4 after two fallback bytes}\par",
        rb"\pard Emoji as a surrogate pair: \u-10179\'3f\u-8699\'3f\par",
        rb"\pard Fallback that is a brace: {\u8364}after\par",
        rb"\pard Cyrillic escapes: {\f5 \'c0\'e1} and raw bytes: {\f5 " + b"\xc0\xe1" + rb"}\par",
        rb"\pard Symbol run: {\f3\'b7\'a5}\par",
        rb"\pard Raw bytes under the default font: " + b"\xe9\xfc" + rb"\par",
        rb"\pard Raw tab byte:" + b"\t" + rb"after\par",
        rb"\pard Special words: \bullet \emdash \endash \emspace \enspace \qmspace \lquote \rquote"
        rb" \ldblquote \rdblquote \zwj \zwnj \tab \line \~ \- \_ end\par",
        rb"\pard Escaped symbols: \{ \} \\ end\par",
        rb"\pard Binary outside a picture: \bin4 " + b"\x00\x01\x7b\x7d" + rb" end\par",
        rb"}",
    ]
    return b"\r\n".join(out) + b"\r\n"


def pictures():
    png = hex_lines(png_1x1())
    pict = rb"\pict\pngblip\picw1\pich1\picwgoal15\pichgoal15" + b"\r\n" + png
    out = [
        rb"{\rtf1\ansi\ansicpg1252\uc1\deff0{\fonttbl{\f0\froman\fcharset0 Times New Roman;}}",
        rb"\pard A hex picture: {" + pict + rb"}\par",
        rb"\pard A picture with a metafile fallback: {\*\shppict{" + pict + rb"}}{\nonshppict{\pict\wmetafile8 0102}}\par",
        rb"\pard An EMF stub: {\pict\emfblip\picw100\pich100 01000000}\par",
        rb"\pard A binary picture: {\pict\jpegblip\bin4 " + b"\xff\xd8\x7b\x7d" + rb"}\par",
        rb"\pard A shape with a picture: {\shp{\*\shpinst\shpleft0\shptop0\shpright300\shpbottom300\shpfhdr0\shpbxcolumn"
        rb"\shpbypara\shpwr3\shpwrk0\shpfblwtxt0\shpz0\shplid1025{\sp{\sn shapeType}{\sv 75}}{\sp{\sn fFlipH}{\sv 0}}"
        rb"{\sp{\sn pib}{\sv {" + pict + rb"}}}}{\shprslt {\pict\wmetafile8 0102}}}\par",
        rb"\pard A text box: {\shp{\*\shpinst\shpleft0\shptop0\shpright3000\shpbottom1000\shplid1026"
        rb"{\sp{\sn shapeType}{\sv 202}}{\shptxt \pard Text inside a shape\par}}{\shprslt {Text inside a shape}}}\par",
        rb"\pard An object: {\object\objemb\objw100\objh100{\*\objclass Excel.Sheet.12}{\*\objdata 01050000020000000b000000}"
        rb"{\result {\pard Result text of the object\par}}}\par",
        rb"}",
    ]
    return b"\r\n".join(out) + b"\r\n"


def read(directory, name):
    with open(os.path.join(directory, name), "rb") as f:
        return f.read()


def malformed(directory):
    minimal = read(directory, "word_minimal.rtf")
    last = minimal.rindex(b"}")
    listing = read(directory, "sas_listing_1page.rtf")
    table = read(directory, "sas_table_3pages.rtf")
    cut = table[:1000]
    # The cut must fall inside a control word: a letter preceded by more letters and a backslash.
    word_start = cut.rindex(b"\\")
    if not (cut[word_start + 1:].isalpha() and len(cut) - word_start > 2):
        sys.exit("make_fixtures: byte 1000 of sas_table_3pages.rtf is not inside a control word")
    return {
        "malformed_missing_brace.rtf": minimal[:last] + minimal[last + 1:],
        "malformed_extra_braces.rtf": listing + b"}}}",
        "malformed_truncated_control.rtf": cut,
        "malformed_bin_past_end.rtf": rb"{\rtf1 {\pict\pngblip\bin999 ab}",
        "malformed_deep_nesting.rtf": rb"{\rtf1 " + b"{" * 2000 + b"x" + b"}" * 2000 + b"}",
        "malformed_u_before_brace.rtf": rb"{\rtf1 {\uc3\u8364}after\par}",
        "malformed_not_rtf.rtf": b"hello world",
        "malformed_empty.rtf": b"",
    }


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_fixtures.py <fixtures_dir>")
    directory = sys.argv[1]
    files = {"word_text.rtf": word_text(), "pictures.rtf": pictures()}
    files.update(malformed(directory))
    for name in sorted(files):
        with open(os.path.join(directory, name), "wb") as f:
            f.write(files[name])
        print("wrote %s (%d bytes)" % (name, len(files[name])))


if __name__ == "__main__":
    main()
