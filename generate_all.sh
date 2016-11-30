#!/bin/sh
#
# Generate All Patterns
#
# Work through a given .risu file and generate full coverage
#

BASE_SHELL_PARAMS="./risugen"

risufile=$1
divisor=$2
insns=$3
dir=$4

if test -z "$risufile"; then
    echo "Need to specify a risu defintiion file"
    exit 1
fi

if test -z "$divisor"; then
    divisor=4
fi

if test -n "$insns"; then
    BASE_SHELL_PARAMS="${BASE_SHELL_PARAMS} --numinsns $insns"
fi

if test -n "dir"; then
    mkdir -p $dir
else
    dir="./"
fi

ALL_INSNS=$(cat $risufile | ag "^\w" | cut -f 1 -d " " | sort)
COUNT=$(cat $risufile | ag "^\w" | cut -f 1 -d " " | wc -l)
set -- $ALL_INSNS

GROUP=$((COUNT / $divisor))

while test $# -gt 0 ; do
    INSN_PATTERNS=""
    I_FILE="$dir/insn_"
    for i in `seq 1 $divisor`; do
        I=$1
        if test -n "${I}"; then
            shift
            INSN_PATTERNS="${INSN_PATTERNS} --pattern ${I}"
            I_FILE="${I_FILE}${I}_"
        fi
    done
    I_FILE="${I_FILE}_INC.risu.bin"
    CMD="$BASE_SHELL_PARAMS ${INSN_PATTERNS} $risufile ${I_FILE}"
    echo "Running: $CMD"
    $CMD
done
