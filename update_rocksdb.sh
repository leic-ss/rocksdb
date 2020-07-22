#!/bin/bash

set -x

work_home=`cd $(dirname $0); pwd`
rocksdb_home=${work_home}

deps_prefix=${rocksdb_home}/rocksdb_bin
DEPS=/apps/mdsenv/deps

rm -rf $DEPS/include/rocksdb
rm -rf $DEPS/lib64/librocksdb.* $DEPS/lib64/cmake

cp -rf ${deps_prefix}/include/rocksdb $DEPS/include
cp -af ${deps_prefix}/lib64/* $DEPS/lib64

