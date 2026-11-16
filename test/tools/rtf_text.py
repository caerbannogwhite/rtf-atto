"""Prints the plain text of an RTF file, read straight from the RTF stream.

Usage: python test/tools/rtf_text.py <file.rtf>

This is an independent check of the C++ reader's text projection. It works
on bytes, keeps a stack of group states, skips the destinations that hold no
document text, and maps control words to characters. The text goes to
stdout as UTF-8. The script exits 0 on any input. The tokenizer checks
every index, the group depth is capped, and a missing closing brace closes
at the end, so no input can raise an exception.
"""

import sys

DEPTH_CAP = 1024

# Groups whose first word is one of these hold no text (written without "\*").
SKIP_PLAIN = {
    "fonttbl", "colortbl", "stylesheet", "info", "pict", "nonshppict",
    "shprslt", "nonesttables", "mmathPr", "userprops",
}
# Groups that end with a line break when they emitted text after the last one.
BLOCKS = {"header", "footer", "headerf", "footerf", "headerl", "headerr",
          "footerl", "footerr", "footnote", "shptxt", "result"}

SYMBOLS = {  # control symbols and words that map to one string
    "~": " ", "_": "‑", "-": "­", "{": "{", "}": "}", "\\": "\\",
    "emdash": "—", "endash": "–", "bullet": "•",
    "lquote": "‘", "rquote": "’", "ldblquote": "“",
    "rdblquote": "”", "emspace": " ", "enspace": " ",
    "qmspace": " ", "zwj": "‍", "zwnj": "‌",
    "tab": "\t", "cell": "\t", "nestcell": "\t", "row": "\n", "nestrow": "\n",
    "par": "\n", "line": "\n", "sect": "\n", "page": "\f", "column": "\n",
}
CHARSET_PAGE = {128: 932, 129: 949, 130: 1361, 134: 936, 136: 950, 161: 1253,
                162: 1254, 163: 1258, 177: 1255, 178: 1256, 186: 1257,
                204: 1251, 222: 874, 238: 1250, 254: 437, 255: 850, 77: 10000}
CODECS = {p: "cp%d" % p for p in list(range(1250, 1259)) + [874, 437, 850, 852, 866]}
CODECS[10000] = "mac_roman"
DOC_PAGES = {"ansi": 1252, "mac": 10000, "pc": 437, "pca": 850}


class State:
    """The state of one open group."""
    __slots__ = ("font", "uc", "skip", "quiet", "name", "ft", "block", "start", "pending", "kids")

    def __init__(self):
        self.font = None    # current font number, None means the default font
        self.uc = 1         # \uc count
        self.skip = False   # nothing from this group is output
        self.quiet = False  # the group's own text is dropped, children may emit
        self.name = ""      # "shpinst" or "upr" for the groups that need it
        self.ft = False     # inside \fonttbl, reading font definitions
        self.block = False  # emit a line break when the group closes
        self.start = 0      # length of the output when the group opened
        self.pending = 1    # 1: the destination word is next, 2: after "\*"
        self.kids = 0       # child groups seen (for \upr)

    def child(self, start):
        s = State()
        s.font, s.uc, s.skip, s.quiet, s.ft = self.font, self.uc, self.skip, self.quiet, self.ft
        s.start = start
        return s


def is_letter(c):
    return 65 <= c <= 90 or 97 <= c <= 122


def is_digit(c):
    return 48 <= c <= 57


def hex_value(c):
    if is_digit(c):
        return c - 48
    if 65 <= c <= 70:
        return c - 55
    if 97 <= c <= 102:
        return c - 87
    return -1


def extract(data):
    """Returns the text projection of the RTF bytes in data."""
    out = []
    stack = [State()]
    stack[0].pending = 0
    overflow = 0        # groups opened beyond the depth cap, not pushed
    doc_page, deff = 1252, 0
    charsets, cpgs = {}, {}
    def_font = [None]   # font number being defined in \fonttbl
    high = [None]       # a high surrogate waiting for its low half
    n, i = len(data), 0

    def emit(text):
        top = stack[-1]
        if not top.skip and not top.quiet:
            out.extend(text)

    def flush_high():
        if high[0] is not None:
            high[0] = None
            emit("�")

    def decode(b):
        if b < 0x80:
            return chr(b)
        font = stack[-1].font
        if font is None:
            font = deff
        if font in cpgs:
            page = cpgs[font]
        else:
            cs = charsets.get(font, 0)
            page = 2 if cs == 2 else CHARSET_PAGE.get(cs, doc_page)
        if page == 2:
            return chr(0xF000 + b)
        if page not in CODECS:
            return "�"
        try:
            return bytes([b]).decode(CODECS[page])
        except UnicodeDecodeError:
            return "�"

    def unicode_escape(value):
        if value < 0:
            value += 65536
        if value < 0 or value > 0xFFFF:
            flush_high()
            emit("�")
        elif 0xD800 <= value <= 0xDBFF:
            flush_high()
            high[0] = value
        elif 0xDC00 <= value <= 0xDFFF:
            if high[0] is not None:
                emit(chr(0x10000 + ((high[0] - 0xD800) << 10) + (value - 0xDC00)))
                high[0] = None
            else:
                emit("�")
        else:
            flush_high()
            emit(chr(value))

    def close_group():
        flush_high()
        top = stack.pop()
        if top.block and not top.skip:
            seg = out[top.start:]
            if seg and seg[-1] != "\n":
                out.append("\n")

    def decide(word, top):
        """Sets the destination of group top from its first word."""
        parent = stack[-2] if len(stack) > 1 else None
        if top.pending == 2:  # written "\*\word"
            if word == "shpinst":
                top.name, top.quiet = "shpinst", True
            elif word == "ud":
                top.quiet = False
            # nesttableprops is read: it holds only control words, and the nestrow
            # that ends a nested row stands inside it.
            elif word not in ("shppict", "nesttableprops"):
                top.skip = True
        elif parent is not None and parent.name == "shpinst" and word != "shptxt":
            top.skip = True
        elif word in SKIP_PLAIN:
            top.skip = True
            top.ft = word == "fonttbl"
        elif word == "upr":
            top.name, top.quiet = "upr", True
        elif word == "ud":
            top.quiet = False
        elif word in BLOCKS:
            top.block, top.quiet = True, False

    while i < n:
        c = data[i]
        top = stack[-1]
        if c == 0x7B:  # {
            i += 1
            flush_high()
            if overflow or len(stack) >= DEPTH_CAP:
                overflow += 1
                continue
            top.pending = 0
            top.kids += 1
            kid = top.child(len(out))
            if top.name == "upr" and top.kids == 1:
                kid.skip = True
            stack.append(kid)
            continue
        if c == 0x7D:  # }
            i += 1
            if overflow:
                overflow -= 1
            elif len(stack) > 1:
                close_group()
            else:
                flush_high()
            continue
        if c != 0x5C:  # a text byte
            i += 1
            if c in (0x0D, 0x0A):
                continue
            top.pending = 0
            flush_high()
            emit("\t" if c == 9 else decode(c))
            continue
        i += 1  # a backslash
        if i >= n:
            break
        c = data[i]
        if is_letter(c):
            j = i
            while j < n and is_letter(data[j]):
                j += 1
            word = data[i:j].decode("ascii")
            neg = j + 1 < n and data[j] == 0x2D and is_digit(data[j + 1])
            k = j + 1 if neg else j
            while k < n and is_digit(data[k]):
                k += 1
            # Every digit is consumed, but only the first 10 are converted, then the
            # value is clamped to 32 bits. int() raises on a run of over 4300 digits.
            if k > j + (1 if neg else 0):
                digits = data[j + (1 if neg else 0):k][:10]
                param = max(-2**31, min(2**31 - 1, (-1 if neg else 1) * int(digits)))
            else:
                param = None
            if param is None:
                k = j
            i = k + 1 if k < n and data[k] == 0x20 else k
            if top.pending and overflow == 0:
                decide(word, top)
                top.pending = 0
            if word == "bin":
                i = min(n, i + max(0, param or 0))
            elif top.ft:
                if def_font[0] is not None and param is not None:
                    if word == "fcharset":
                        charsets[def_font[0]] = param
                    elif word == "cpg":
                        cpgs[def_font[0]] = param
                if word == "f" and param is not None:
                    def_font[0] = param
            elif word == "f" and param is not None:
                top.font = param
            elif word == "deff" and param is not None:
                deff = param
            elif word == "uc" and param is not None:
                top.uc = max(0, param)
            elif word in DOC_PAGES:
                doc_page = DOC_PAGES[word]
            elif word == "ansicpg" and param is not None:
                doc_page = param
            elif word == "u" and param is not None:
                unicode_escape(param)
                i = skip_fallback(data, i, top.uc)
            elif word in SYMBOLS:
                flush_high()
                emit(SYMBOLS[word])
            continue
        # A control symbol.
        i += 1
        sym = chr(c) if c < 0x80 else ""
        if sym == "*":
            if top.pending == 1:
                top.pending = 2
            continue
        top.pending = 0
        if sym == "'":
            digits = []
            while len(digits) < 2 and i < n and hex_value(data[i]) >= 0:
                digits.append(hex_value(data[i]))
                i += 1
            if len(digits) == 2:
                flush_high()
                emit(decode(digits[0] * 16 + digits[1]))
        elif c in (0x0D, 0x0A):
            # A backslash followed by CR or LF is a paragraph break.
            emit("\n")
        elif sym in SYMBOLS:
            flush_high()
            emit(SYMBOLS[sym])
    while len(stack) > 1:
        close_group()
    flush_high()
    return "".join(out)


def skip_fallback(data, i, count):
    """Skips count fallback units from index i and returns the new index.

    A unit is one text byte or one \\'hh. The skip stops at a brace and at any
    control word or symbol other than \\'hh. CR and LF are ignored.
    """
    n = len(data)
    while count > 0 and i < n:
        c = data[i]
        if c in (0x0D, 0x0A):
            i += 1
        elif c in (0x7B, 0x7D):
            break
        elif c == 0x5C:
            if i + 1 < n and data[i + 1] == 0x27:
                i = min(n, i + 4)
                count -= 1
            else:
                break
        else:
            i += 1
            count -= 1
    return i


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: rtf_text.py <file.rtf>\n")
        return 2
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    sys.stdout.buffer.write(extract(data).encode("utf-8", errors="replace"))
    sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
