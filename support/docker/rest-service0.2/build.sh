cd /builds/thehub
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
cd rest-service
make -j`nproc` install

cd /builds/startRestv02
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
make -j`nproc`

