#!/bin/sh

for linter in scripts/lint/*; do
	printf 'linter: %s\n' "$linter"
	git ls-files -z --ignored -x '*.[ch]' -x 'meson.build' \
		| xargs -0 -n 1 sh -c "$linter \"\$1\" || ( echo 'file:' \"\$1\"; exit 255 )" sh
done
