#!/usr/bin/env bash

echo "Building oneCCL ..."
cd /tmp
rm -rf oneCCL
git config --global http.proxy http://child-prc.intel.com:913
git clone https://github.com/oneapi-src/oneCCL
cd oneCCL
git checkout beta08
mkdir -p build && cd build
cmake ..
make -j 2 install

#
# Setup building environments manually:
#
# export ONEAPI_ROOT=/opt/intel/oneapi
# source /opt/intel/oneapi/dal/latest/env/vars.sh
# source /opt/intel/oneapi/tbb/latest/env/vars.sh
# source /tmp/oneCCL/build/_install/env/setvars.sh
#
