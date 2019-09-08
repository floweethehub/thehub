#!/bin/bash

cd `dirname $0`

if test ! -f "hub/Dockerfile"; then
    echo -e "ERROR: Something wrong"
    exit
fi;

if test ! -d "bitcore/bin"; then
    echo -e "ERROR: Missing bin dir, did you compile?"
    exit
fi

if test -z "$TAG"; then
    echo -e "NOTICE: \$TAG not set, using 'local' as tag for docker images\n\n"
    TAG="local"
fi
set -x

docker build --tag=flowee/bitcore-proxy:$TAG bitcore
