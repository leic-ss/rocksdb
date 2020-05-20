#!/bin/bash

set -x

work_home=`cd $(dirname $0); pwd`
rocksdb_home=${work_home}

deps_prefix=${rocksdb_home}/rocksdb_bin
cmake -H${rocksdb_home} -Bcmake-build -DCMAKE_INSTALL_PREFIX:PATH=${deps_prefix} \
      -DCMAKE_PREFIX_PATH=${deps_prefix} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
      -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DWITH_TOOLS=OFF -DCMAKE_EXE_LINKER_FLAGS=-lrt \
      -DCMAKE_SHARED_LINKER_FLAGS=-lrt -DUSE_RTTI=1

cmake --build cmake-build -- -j4
cmake --build cmake-build -- all install
