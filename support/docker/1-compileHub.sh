#!/bin/bash
cd `dirname $0`

if test -z "$SOURCES_HUB" -o ! -d "$SOURCES_HUB" -o ! -d "$SOURCES_HUB/hub"; then
    echo -e "ERROR: Please export SOURCES_HUB to the absolute path of the hub repo\n"
    exit
fi


cid=`docker run -d -ti flowee/buildenv:static /bin/bash`

TARGET="hub/bin"
TARGET2="pos-cli/bin"

rm -rf $TARGET
rm -rf $TARGET2
docker container exec $cid mkdir -p /builds/tomzander
docker cp "$SOURCES_HUB" $cid:/builds/tomzander/thehub
docker cp hub/startHub $cid:/builds/tomzander/
docker container exec $cid build_static
docker container exec --workdir /builds/tomzander/startHub $cid mkdir build
docker container exec --workdir /builds/tomzander/startHub/build $cid cmake ..
docker container exec --workdir /builds/tomzander/startHub/build $cid make
docker cp $cid:/builds/tomzander/thehub/bin $TARGET/../
docker cp $cid:/builds/tomzander/startHub/build/startHub $TARGET
docker container stop $cid
docker container rm $cid

rm -f $TARGET/txVulcano
rm -f $TARGET/indexer-cli

for i in $TARGET/*; do
    strip $i
done

mkdir -p $TARGET2
mv $TARGET/pos $TARGET2
