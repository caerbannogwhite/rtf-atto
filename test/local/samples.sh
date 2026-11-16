#!/usr/bin/env bash
# Checks the private sample files of the owner against a stored baseline.
#
# Usage: RTF_SAMPLES_DIR=<dir> [RTF2JSON=<path>] [PYTHON=<command>] test/local/samples.sh
#
# For every .rtf and .RTF file in the directory the script runs
# "rtf2json --summary" and compares the result with the block for that file
# in <dir>/expected.txt. A block starts with the "file:" line. Blocks are
# separated by one empty line. The script then compares "rtf2json --text"
# with test/tools/rtf_text.py for every file that is RTF.
# expected.txt lives next to the samples. It is never committed.
# The script prints summaries and diffs. It never prints the text of a file.

root=$(cd "$(dirname "$0")/../.." && pwd)

dir=${RTF_SAMPLES_DIR:-}
if [ -z "$dir" ] || [ ! -d "$dir" ]; then
  echo "samples.sh: set RTF_SAMPLES_DIR to the directory with the samples" >&2
  exit 2
fi

if [ -z "${RTF2JSON:-}" ]; then
  for candidate in build/Release/rtf2json.exe build/rtf2json build/rtf2json.exe \
      build-gcc/rtf2json.exe build-gcc/rtf2json; do
    if [ -f "$root/$candidate" ]; then
      RTF2JSON=$root/$candidate
      break
    fi
  done
fi
if [ -z "${RTF2JSON:-}" ] || [ ! -f "$RTF2JSON" ]; then
  echo "samples.sh: rtf2json not found; build it or set RTF2JSON" >&2
  exit 2
fi

if [ -z "${PYTHON:-}" ]; then
  if command -v python3 >/dev/null 2>&1; then PYTHON=python3; else PYTHON=python; fi
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

expected=$dir/expected.txt
had_expected=1
[ -f "$expected" ] || had_expected=0

# Lists the sample files, one per line, sorted without regard to case.
list_files() {
  for f in "$dir"/*; do
    [ -f "$f" ] || continue
    case "$f" in
      *.[rR][tT][fF]) printf '%s\n' "$f" ;;
    esac
  done | sort -f
}

# Prints the block of one file from expected.txt.
stored_block() {
  awk -v name="$1" '
    $0 == "file: " name { on = 1; print; next }
    on && $0 == "" { exit }
    on { print }
  ' "$expected"
}

count=0
new=0
failed=0
list_files > "$tmp/files"

while IFS= read -r file; do
  count=$((count + 1))
  name=$(basename "$file")
  "$RTF2JSON" --summary "$file" > "$tmp/summary" 2> "$tmp/err"
  code=$?
  if [ "$code" -gt 1 ]; then
    echo "FAIL $name: rtf2json exited with $code"
    failed=$((failed + 1))
    continue
  fi

  if [ "$had_expected" -eq 0 ]; then
    if [ -s "$expected" ]; then echo >> "$expected"; fi
    cat "$tmp/summary" >> "$expected"
  else
    stored_block "$name" > "$tmp/stored"
    if [ ! -s "$tmp/stored" ]; then
      if [ -s "$expected" ]; then echo >> "$expected"; fi
      cat "$tmp/summary" >> "$expected"
      echo "new $name"
      new=$((new + 1))
    elif ! diff -u "$tmp/stored" "$tmp/summary" > "$tmp/diff"; then
      echo "FAIL $name: summary differs from expected.txt"
      cat "$tmp/diff"
      failed=$((failed + 1))
    fi
  fi

  if grep -q '^error: not_rtf' "$tmp/summary"; then
    continue
  fi

  "$RTF2JSON" --text "$file" > "$tmp/a.txt" 2> /dev/null
  "$PYTHON" "$root/test/tools/rtf_text.py" "$file" > "$tmp/b.txt" 2> /dev/null
  if result=$("$PYTHON" "$root/test/tools/compare_text.py" "$tmp/a.txt" "$tmp/b.txt"); then
    echo "text $name: same"
  else
    echo "text $name: differ"
    echo "$result"
    failed=$((failed + 1))
  fi
done < "$tmp/files"

if [ "$had_expected" -eq 0 ]; then
  echo "wrote expected.txt; review it against Word"
fi
echo "$count files, $new new, $failed failed"
[ "$failed" -eq 0 ]
