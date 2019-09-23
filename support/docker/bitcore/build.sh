cd /builds/thehub
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make univalue leveldb
cd bitcore-proxy
make -j`nproc` install

cd /builds/startBitcoreProxy
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j`nproc`

