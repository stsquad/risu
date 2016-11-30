#!/bin/bash
#
# Run risu against a set of binaries + trace files
#
#
#set -e

passed=()
failed=()
missing=()

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
risu_path=${script_dir}/risu

for f in $@; do
    t="$f.trace"
    echo "Running $f against $t"
    if [ -e $t ]; then
        ${QEMU} ${risu_path} $f -t $t
        if [ $? == 0 ]; then
            passed=( "${passed[@]}" $f )
        else
            failed=( "${failed[@]}" $f )
        fi
    else
        missing=( "${missing[@]}" $f )
    fi
done

if test ${#missing[@]} -gt 0; then
    echo "Tests missing ${#missing[@]} trace files:"
    for m in "${missing[@]}"; do
        echo "$m"
    done
fi

if test ${#passed[@]} -gt 0; then
    echo "Passed ${#passed[@]} tests:"
    for p in "${passed[@]}"; do
        echo "$p"
    done
fi

if test ${#failed[@]} -gt 0; then
    echo "Failed ${#failed[@]} tests:"
    for f in "${failed[@]}"; do
        echo "$f"
    done
fi

exit ${#failed[@]}
