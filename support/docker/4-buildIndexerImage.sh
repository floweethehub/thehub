#!/bin/bash
cd `dirname $0`

if test ! -f "indexer/Dockerfile"; then
    echo -e "ERROR: Something wrong"
    exit 1
fi;

if test ! -d "indexer/bin"; then
    echo -e "ERROR: Missing bin dir, did you compile?"
    exit 1
fi

if test -z "$TAG"; then
    echo -e "NOTICE: \$TAG not set, using 'local' as tag for docker images\n\n"
    TAG="local"
fi

docker build --tag=flowee/indexer:$TAG indexer
