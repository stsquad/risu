#!/bin/bash
#
set -x

if [ -z $PPA ]; then
    echo "No PPA Specified"
else
    sudo add-apt-repository -y ${PPA}
fi

if [ ! -z "$PKGS" ]; then
    sudo apt-get clean
    sudo apt-get update -qq
    sudo apt-get install -y ${PKGS}
fi    

if [ ! -z $EXTERNAL_TOOLCHAIN ]; then
    SAVE_PWD=$PWD
    mkdir ext-toolchain
    cd ext-toolchain
    wget -O - ${EXTERNAL_TOOLCHAIN} | tar --strip-components=1 -xJ
    cd -
fi
