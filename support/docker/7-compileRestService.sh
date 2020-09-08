#!/bin/bash
cd `dirname $0`

if test -z "$SOURCES_HUB" -o ! -d "$SOURCES_HUB" -o ! -d "$SOURCES_HUB/indexer"; then
    echo -e "ERROR: Please export SOURCES_HUB to point to the dir of the hub\n"
    exit 1
fi;

cid=`docker run -d -ti flowee/buildenv:debian /bin/bash`
TARGET="rest-service0.2/bin"

rm -rf $TARGET
mkdir -p $TARGET
docker container exec $cid mkdir -p /builds/
docker cp "$SOURCES_HUB" $cid:/builds/thehub
docker cp rest-service0.2/startRestv02 $cid:/builds/
docker cp rest-service0.2/build.sh $cid:/builds/
docker container exec $cid env /builds/build.sh
docker cp $cid:/usr/bin/rest-service $TARGET/
docker cp $cid:/usr/bin/rest-service-tester $TARGET/
docker cp $cid:/builds/startRestv02/build/startRestv02 $TARGET/
docker container stop $cid
docker container rm $cid
