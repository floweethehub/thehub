#!/bin/bash
cd `dirname $0`

if test -z "$SOURCES_HUB" -o ! -d "$SOURCES_HUB" -o ! -d "$SOURCES_HUB/indexer"; then
    echo -e "ERROR: Please export SOURCES_HUB to the absolute path of the hub repo\n"
    exit 1
fi;

cid=`docker run -d -ti flowee/buildenv:debian /bin/bash`

rm -rf bin

docker container exec $cid mkdir -p /builds/
docker cp "$SOURCES_HUB" $cid:/builds/thehub
docker cp hub/startHub $cid:/builds/
docker cp indexer/startIndexer $cid:/builds/
docker cp bitcore/startBitcoreProxy $cid:/builds/
docker cp rest-service0.2/startRestv02 $cid:/builds/
docker cp .build.sh $cid:/builds/build.sh
docker container exec $cid env /builds/build.sh
docker cp $cid:/usr/local/bin .

docker cp $cid:/builds/startHub/build/startHub bin/
docker cp $cid:/builds/startIndexer/build/startIndexer bin/
docker cp $cid:/builds/startRestv02/build/startRestv02 bin/
docker cp $cid:/builds/startBitcoreProxy/build/startBitcoreProxy bin/

docker container stop $cid
docker container rm $cid
