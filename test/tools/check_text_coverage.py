"""Checks the C++ reader's text projection against the independent extractor.

Usage: python test/tools/check_text_coverage.py <rtf2json> <fixtures_dir>

For every *.rtf file in the directory, in sorted order, except those whose
name starts with "malformed_", the script runs "rtf2json --text" and
rtf_text.py, normalises both outputs and compares them. It prints one line
per file. It exits 1 when any file differs, else 0.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import compare_text  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: check_text_coverage.py <rtf2json> <fixtures_dir>")
    tool, directory = os.path.abspath(sys.argv[1]), sys.argv[2]
    names = sorted(n for n in os.listdir(directory)
                   if n.endswith(".rtf") and not n.startswith("malformed_"))
    failed = False
    for name in names:
        path = os.path.join(directory, name)
        cpp = subprocess.run([tool, "--text", path], capture_output=True)
        py = subprocess.run([sys.executable, os.path.join(HERE, "rtf_text.py"), path],
                            capture_output=True)
        if cpp.returncode != 0 or py.returncode != 0:
            print("differ %s\n  rtf2json exit %d, rtf_text.py exit %d\n  %s"
                  % (name, cpp.returncode, py.returncode,
                     py.stderr.decode("utf-8", "replace").strip()))
            failed = True
            continue
        diff = compare_text.first_difference(compare_text.normalise(cpp.stdout),
                                             compare_text.normalise(py.stdout))
        if diff is None:
            print("same %s" % name)
        else:
            print("differ %s\n  %s" % (name, diff.replace("\n", "\n  ")))
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
