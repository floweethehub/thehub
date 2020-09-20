cd /builds/thehub
mkdir build
cd build
cmake ..
make -j`nproc` install

cd /builds/startIndexer
mkdir build
cd build
cmake ..
make -j`nproc`

cd /builds/startHub
mkdir build
cd build
cmake ..
make -j`nproc`

cd /builds/startRestv02
mkdir build
cd build
cmake ..
make -j`nproc`

cd /builds/startBitcoreProxy
mkdir build
cd build
cmake ..
make -j`nproc`
