#!/bin/sh
#
# Record traces
#
# A risu helper script to batch process a bunch of binaries and record their outputs
#
set -e

if test -z "$RISU"; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
    RISU=${script_dir}/risu
fi

for f in $@; do
    echo "Running risu against $f"
    t="$f.trace"
    ${RISU} --master $f -t $t
    echo "Checking trace file OK"
    ${RISU} $f -t $t
done
