#!/bin/bash

cd `dirname $0`

if test ! -f "hub/Dockerfile"; then
    echo -e "ERROR: Something wrong"
    exit 1
fi;

if test ! -d "hub/bin"; then
    echo -e "ERROR: Missing bin dir, did you compile?"
    exit
fi

if test -z "$TAG"; then
    echo -e "NOTICE: \$TAG not set, using 'local' as tag for docker images\n\n"
    TAG="local"
fi

docker build --tag=flowee/hub:$TAG hub
docker build --tag=flowee/pos-cli:$TAG pos-cli
