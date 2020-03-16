#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

TARANTOOL_VERSION=${TARANTOOL_VERSION:-1.10}

curl http://download.tarantool.org/tarantool/${TARANTOOL_VERSION}/gpgkey | \
    sudo apt-key add -
release=`lsb_release -c -s`

# append two lines to a list of source repositories
sudo rm -f /etc/apt/sources.list.d/*tarantool*.list
sudo tee /etc/apt/sources.list.d/tarantool_${TARANTOOL_VERSION}.list <<- EOF
deb http://download.tarantool.org/tarantool/${TARANTOOL_VERSION}/ubuntu/ $release main
EOF

# install
sudo apt-get update > /dev/null
sudo apt-get -qy install tarantool tarantool-dev

tarantool --version
phpize && ./configure
make
make install
sudo pip install PyYAML
/usr/bin/python test-run.py
