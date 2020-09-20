#!/bin/bash
cd `dirname $0`

if test ! -f "indexer/Dockerfile"; then
    echo -e "ERROR: Something wrong"
    exit 1
fi;

if test ! "bin/indexer"; then
    echo -e "ERROR: Missing bin dir, did you compile?"
    exit 1
fi

if test -z "$TAG"; then
    echo -e "NOTICE: \$TAG not set, using 'local' as tag for docker images\n\n"
    TAG="local"
fi

for target in hub pos-cli indexer bitcore rest-service0.2
do
    rm -rf $target/bin
    mkdir $target/bin
done

for file in bin/*
do
    strip "$file"
done

ln bin/hub bin/startHub bin/hub-cli hub/bin/
docker build --tag=flowee/hub:$TAG hub
ln bin/pos pos-cli/bin/
docker build --tag=flowee/pos-cli:$TAG pos-cli
ln bin/indexer bin/indexer-cli bin/startIndexer indexer/bin/
docker build --tag=flowee/indexer:$TAG indexer
ln bin/bitcore-proxy bin/startBitcoreProxy bitcore/bin
docker build --tag=flowee/bitcore-proxy:$TAG bitcore
ln bin/startRestv02 bin/rest-service bin/rest-service-tester rest-service0.2/bin
docker build --tag=flowee/rest-servicev02:$TAG rest-service0.2
