cd /builds/thehub
mkdir build
cd build
cmake ..
make univalue leveldb
cd libs
make -j`nproc` install

cd /builds/bitcore
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j`nproc` install

cd /builds/startBitcoreProxy
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j`nproc`

