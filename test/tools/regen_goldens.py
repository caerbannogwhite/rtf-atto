"""Rewrites the golden JSON files of the fixtures.

Usage: python test/tools/regen_goldens.py <rtf2json> <fixtures_dir>

For every <name>.rtf in the directory, the script runs rtf2json in --json and
--tables mode and writes <name>.json and <name>.tables.json with the exact
bytes the command printed. Before writing, it checks that each output is
canonical: equal to Python's json.dumps(..., ensure_ascii=False, indent=2,
sort_keys=True) plus one newline. A file that is not RTF makes the command
exit with code 1 and print the one-line error object {"error": "not_rtf"};
that line is checked as it is and becomes the golden. The script prints one line per golden file and stops at the first failure.
"""

import json
import os
import subprocess
import sys

# What rtf2json prints, with exit code 1, for a file that is not RTF.
NOT_RTF = '{"error": "not_rtf"}\n'


def first_difference(a, b):
    """Returns the first line where the two texts differ, from each text."""
    left, right = a.split("\n"), b.split("\n")
    for i in range(max(len(left), len(right))):
        x = left[i] if i < len(left) else ""
        y = right[i] if i < len(right) else ""
        if x != y:
            return "line %d\n  output:    %s\n  canonical: %s" % (i + 1, x, y)
    return "no line differs"


def run(tool, mode, path):
    result = subprocess.run([tool, mode, path], capture_output=True)
    if result.returncode not in (0, 1):
        sys.exit("rtf2json failed with code %d on %s: %s"
                 % (result.returncode, path, result.stderr.decode("utf-8", "replace")))
    return result.returncode, result.stdout


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: regen_goldens.py <rtf2json> <fixtures_dir>")
    # An absolute path, because Windows does not start a relative path written with "/".
    tool, directory = os.path.abspath(sys.argv[1]), sys.argv[2]
    names = sorted(n for n in os.listdir(directory) if n.endswith(".rtf"))
    for name in names:
        path = os.path.join(directory, name)
        base = name[:-len(".rtf")]
        for mode, suffix in (("--json", ".json"), ("--tables", ".tables.json")):
            golden = base + suffix
            code, out = run(tool, mode, path)
            text = out.decode("utf-8")
            if code == 1:
                canonical = NOT_RTF
            else:
                canonical = json.dumps(json.loads(text), ensure_ascii=False, indent=2, sort_keys=True) + "\n"
            if text != canonical:
                sys.exit("not canonical: %s\n%s" % (golden, first_difference(text, canonical)))
            with open(os.path.join(directory, golden), "wb") as f:
                f.write(out)
            print("canonical: %s" % golden)


if __name__ == "__main__":
    main()
