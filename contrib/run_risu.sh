#!/bin/bash
#
# Run risu against a set of binaries + trace files
#
# Copyright (c) 2017 Linaro Limited
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
#
# Contributors:
#     Alex Bennée <alex.bennee@linaro.org> - initial implementation
#
# Usage:
#   (optional) export QEMU=/path/to/qemu
#   (optional) export QEMU=/path/to/qemu -cpu type
#   (optional) export RISU=/path/to/risu
#   ./run_risu.sh  ./testcases.aarch64/*.bin

passed=()
failed=()
missing=()

if test -z "$RISU"; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
    RISU=${script_dir}/risu
fi

if test -z "$QEMU"; then
    echo "Running against system QEMU with no flags"
fi

for f in $@; do
    t="$f.trace"
    CMD="${QEMU} ${RISU} $f -t $t"
    echo "Running: ${CMD}"
    if [ -e $t ]; then
        ${CMD}
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
else
    echo "No Failures ;-)"
fi

exit ${#failed[@]}
