#!/bin/sh
# Rewrites the generated tables in docs/RESOURCES.md from this build's
# `megamod-resources --markdown` (test_resource fails while they differ).
#   scripts/regen_resource_tables.sh [BUILD]     BUILD defaults to build-host
set -e
cd "$(dirname "$0")/.."
build=${1:-build-host}
./"$build"/megamod-resources --markdown > "$build/resource_tables.md"
python3 - "$build/resource_tables.md" <<'PY'
import sys
begin = '<!-- megamod-resources --markdown: begin -->\n'
end = '<!-- megamod-resources --markdown: end -->'
doc = open('docs/RESOURCES.md').read()
a, b = doc.index(begin) + len(begin), doc.index(end)
open('docs/RESOURCES.md', 'w').write(doc[:a] + open(sys.argv[1]).read() + doc[b:])
PY
