#!/bin/bash
cd `dirname $0`

if test -z "$SOURCES_HUB" -o ! -d "$SOURCES_HUB" -o ! -d "$SOURCES_HUB/indexer"; then
    echo -e "ERROR: Please export SOURCES_HUB to point to the dir of the hub\n"
    exit
fi;
if test -z "$SOURCES_BITCORE" -o ! -d "$SOURCES_BITCORE" -o ! -f "$SOURCES_BITCORE/BitCoreProxy.cpp"; then
    echo -e "ERROR: Please export SOURCES_BITCORE to point to the dir of the bitcore-proxy\n"
    exit
fi;

cid=`docker run -d -ti flowee/buildenv:debian /bin/bash`
TARGET="bitcore/bin"

rm -rf $TARGET
mkdir -p $TARGET
docker container exec $cid mkdir -p /builds/
docker cp "$SOURCES_HUB" $cid:/builds/thehub
docker cp "$SOURCES_BITCORE" $cid:/builds/bitcore
docker cp bitcore/startBitcoreProxy $cid:/builds/
docker cp bitcore/build.sh $cid:/builds/
docker container exec $cid env /builds/build.sh
docker cp $cid:/usr/bin/bitcore-proxy $TARGET/
docker cp $cid:/usr/bin/bitcore-proxy-tester $TARGET/
docker cp $cid:/builds/startBitcoreProxy/build/startBitcoreProxy $TARGET/
docker container stop $cid
docker container rm $cid

