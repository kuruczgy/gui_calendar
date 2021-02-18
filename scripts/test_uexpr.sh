#!/bin/sh
set -e
uexpr_exe="$1"

for f in test/uexpr/input*.txt; do
	out_file=test/uexpr/out"${f##*/input}"
	exp="$(cat "$out_file")"
	act="$("$uexpr_exe" "$f")"
	if [ "$exp" != "$act" ]; then
		printf '%s failed!\n' "$f"
		printf 'expected: %s\n' "$exp"
		printf 'actual: %s\n' "$act"
		exit 1
	fi
done
