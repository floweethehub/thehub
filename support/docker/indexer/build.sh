cd /builds/thehub
mkdir build
cd build
cmake ..
make -j`nproc` indexer indexer-cli

cd /builds/startIndexer
mkdir build
cd build
cmake ..
make -j`nproc`
