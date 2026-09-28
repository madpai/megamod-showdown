#!/bin/bash
# The X8 gameplay/late-join route against the distinct X9 visual package.
set -euo pipefail
cd "$(dirname "$0")/.."
NIGHT_SHIFT_VARIANT=x9 scripts/test_x8.sh
