#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

TARANTOOL_VERSION=${TARANTOOL_VERSION:-1.10}

if [ "$(id -u)" = "0" ]; then
    SUDO=""
else
    SUDO="sudo"
fi

${SUDO} apt-get update > /dev/null

if [ -z "$(which gpg)" ]; then
    ${SUDO} apt-get -qy install gnupg2 > /dev/null
fi

if [ -z "$(which lsb_release)" ]; then
    ${SUDO} apt-get -qy install lsb-release > /dev/null
fi

${SUDO} apt-get -qy install apt-transport-https > /dev/null
${SUDO} apt-get -qy install python-yaml > /dev/null

OS="$(lsb_release --id --short | tr '[:upper:]' '[:lower:]')"  # debian or ubuntu
DIST="$(lsb_release --codename --short)"                   # stretch, xenial, ...

# Setup tarantool repository.
curl https://download.tarantool.org/tarantool/${TARANTOOL_VERSION}/gpgkey | \
    ${SUDO} apt-key add -
${SUDO} rm -f /etc/apt/sources.list.d/*tarantool*.list
${SUDO} tee /etc/apt/sources.list.d/tarantool_${TARANTOOL_VERSION}.list <<- EOF
deb https://download.tarantool.org/tarantool/${TARANTOOL_VERSION}/${OS}/ ${DIST} main
EOF
${SUDO} apt-get update > /dev/null

# Install tarantool executable and headers.
#
# Explicit version is necessary to install a lower tarantool
# version from our repository when a system have a higher version
# in default repositories. Say, Debian Stretch have tarantool-1.7
# that prevents tarantool-1.6 from being installed from our repo
# if we would not hold the version.
${SUDO} apt-get -qy install "tarantool=${TARANTOOL_VERSION}*" \
    "tarantool-dev=${TARANTOOL_VERSION}*" > /dev/null
tarantool --version

# Ensure we got the requested tarantool version.
TARANTOOL_VERSION_PATTERN='^Tarantool \([0-9]\+\.[0-9]\+\)\..*$'
ACTUAL_TARANTOOL_VERSION="$(tarantool --version | head -n 1 | \
    sed "s/${TARANTOOL_VERSION_PATTERN}/\\1/")"
if [ "${ACTUAL_TARANTOOL_VERSION}" != "${TARANTOOL_VERSION}" ]; then
    echo "Requested tarantool-${TARANTOOL_VERSION}, got" \
        "tarantool-${ACTUAL_TARANTOOL_VERSION}"
    exit 1
fi

phpize && ./configure
make
make install
/usr/bin/python test-run.py
