#!/bin/bash
cd `dirname $0`

if test -z "$SOURCES_HUB" -o ! -d "$SOURCES_HUB" -o ! -d "$SOURCES_HUB/indexer"; then
    echo -e "ERROR: Please export SOURCES_HUB to the absolute path of the hub repo\n"
    exit 1
fi;

cid=`docker run -d -ti flowee/buildenv:debian /bin/bash`
TARGET="indexer/bin"

rm -rf $TARGET
mkdir $TARGET
docker container exec $cid mkdir -p /builds/
docker cp "$SOURCES_HUB" $cid:/builds/thehub
docker cp indexer/startIndexer $cid:/builds/
docker cp indexer/build.sh $cid:/builds/
docker container exec $cid env /builds/build.sh
docker cp $cid:/builds/thehub/build/indexer/indexer $TARGET/
docker cp $cid:/builds/thehub/build/indexer/indexer-cli $TARGET/
docker cp $cid:/builds/startIndexer/build/startIndexer $TARGET/
docker container stop $cid
docker container rm $cid

for i in $TARGET/*; do
    strip $i
done
