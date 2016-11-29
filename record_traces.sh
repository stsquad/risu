#!/bin/sh
#
# Record traces
#
# A risu helper script to batch process a bunch of binaries and record their outputs
#
set -e


for f in $@; do
    echo "Running risu against $f"
    t="$f.trace"
    ./risu --master $f -t $t
    echo "Checking trace file OK"
    ./risu $f -t $t
done
