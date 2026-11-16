"""Compares two text files after normalising them.

Usage: python test/tools/compare_text.py [--quiet] <a.txt> <b.txt>

Each file is read as UTF-8 bytes. Each is split on "\n", every line is
stripped of leading and trailing whitespace, and empty lines are dropped.
The script prints "same" and exits 0 when the two lists are equal. Otherwise
it prints the first difference and exits 1.
With --quiet it prints only "differ at line N" (N counts from 1) or
"extra lines in a|b after line N", never the lines themselves.
"""

import sys


def normalise(data):
    """Returns the list of stripped, non-empty lines of the bytes in data."""
    text = data.decode("utf-8", errors="replace")
    lines = [line.strip() for line in text.split("\n")]
    return [line for line in lines if line]


def first_difference(a, b):
    """Returns a description of the first difference of two line lists, or None."""
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return "line %d\n  a: %r\n  b: %r" % (i, a[i], b[i])
    if len(a) > len(b):
        return "a has extra lines from line %d: %r" % (len(b), a[len(b)])
    if len(b) > len(a):
        return "b has extra lines from line %d: %r" % (len(a), b[len(a)])
    return None


def quiet_difference(a, b):
    """Like first_difference, but names only the position, never the text."""
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return "differ at line %d" % (i + 1)
    if len(a) > len(b):
        return "extra lines in a after line %d" % len(b)
    if len(b) > len(a):
        return "extra lines in b after line %d" % len(a)
    return None


def main():
    args = sys.argv[1:]
    quiet = "--quiet" in args
    args = [x for x in args if x != "--quiet"]
    if len(args) != 2:
        sys.exit("usage: compare_text.py [--quiet] <a.txt> <b.txt>")
    with open(args[0], "rb") as f:
        a = normalise(f.read())
    with open(args[1], "rb") as f:
        b = normalise(f.read())
    diff = quiet_difference(a, b) if quiet else first_difference(a, b)
    if diff is None:
        print("same")
        return 0
    print(diff)
    return 1


if __name__ == "__main__":
    sys.exit(main())
