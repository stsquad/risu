#!/bin/bash
#
# Generate all patterns in a given RISU file
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
#   ./contrib/generate_all.sh <arch.risu> <target directory> -- risugen args

set -e

USAGE="Usage: `basename $0` [-h] [-n x] <risufile> <target dir> -- [risugen args]"
SPLIT=4
RISUGEN=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)/risugen

# Parse command line options.
while getopts hn: OPT; do
    case "$OPT" in
        h)
            echo $USAGE
            exit 0
            ;;
        n)
            SPLIT=$OPTARG
            ;;
        \?)
            # getopts issues an error message
            echo $USAGE >&2
            exit 1
            ;;
    esac
done

# Remove the switches we parsed above.
shift `expr $OPTIND - 1`

# Parse up to and including any --
RISUGEN_ARGS=""
while [ $# -ne 0 ] && [ -z "$RISUGEN_ARGS" ]; do

    if [ -f $1 ]; then
        RISU_FILE=$1;
    elif [ -d $1 ]; then
        TARGET_DIR=$1;
    elif [ "$1" = "--" ]; then
        RISUGEN_ARGS=$1
    elif [ ! -e $1 ]; then
        TARGET_DIR=$1
    fi

    shift
done
# anything left is for RISUGEN
RISUGEN_ARGS=$@

if test -z "${RISUGEN}" || test ! -x "${RISUGEN}";  then
    echo "Couldn't find risugen (${RISUGEN})"
    exit 1
fi

if [ -z "$RISU_FILE" ]; then
    echo "Need to set a .risu file for patterns"
    exit 1
fi

if [ -z "${TARGET_DIR}" ]; then
    echo "Need to set a TARGET_DIR"
    exit 1
fi


mkdir -p ${TARGET_DIR}

ALL_INSNS=$(cat ${RISU_FILE} | ag "^\w" | cut -f 1 -d " " | sort)
COUNT=$(cat ${RISU_FILE=} | ag "^\w" | cut -f 1 -d " " | wc -l)
set -- $ALL_INSNS

GROUP=$((COUNT / ${SPLIT}))

while test $# -gt 0 ; do
    INSN_PATTERNS=""
    I_FILE="${TARGET_DIR}/insn_"
    for i in `seq 1 ${SPLIT}`; do
        I=$1
        if test -n "${I}"; then
            shift
            INSN_PATTERNS="${INSN_PATTERNS} --pattern ${I}"
            I_FILE="${I_FILE}${I}_"
        fi
    done
    I_FILE="${I_FILE}_INC.risu.bin"
    CMD="${RISUGEN} ${RISUGEN_ARGS} ${INSN_PATTERNS} ${RISU_FILE} ${I_FILE}"
    echo "Running: $CMD"
    $CMD
done
