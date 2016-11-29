#!/bin/sh
#
# A risu helper script to batch process a bunch of binaries and record their outputs
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
#   export RISU=/path/to/risu
#   ./record_traces.sh  ./testcases.aarch64/*.bin
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
