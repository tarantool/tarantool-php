#!/bin/bash
echo $TRAVIS_PHP_VERSION
if [ $PACK != 'none' ]; then
    echo 'Test skipped(packaging mode)'
    cd build/pack
    bash pack.sh $PACK $OS $DIST $TRAVIS_BRANCH $OUT_REPO $PROD $P_URI
else
    echo 'Preparing tests'
    curl http://tarantool.org/dist/public.key | sudo apt-key add -
    echo "deb http://tarantool.org/dist/master/ubuntu `lsb_release -c -s` main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
    sudo apt-get update
    sudo apt-get -y install tarantool python-yaml
    phpize && ./configure
    make
    make install
    ./test-run.py --flags
fi
