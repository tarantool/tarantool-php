#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

if type dnf; then
    PM_SYNC='dnf makecache'
    PM_INSTALL_NAME='dnf -qy install'
    PM_INSTALL_FILE='dnf -qy install'
    PKG_GLOB='php-tarantool-*.x86_64.rpm'
elif type yum; then
    PM_SYNC='yum makecache'
    PM_INSTALL_NAME='yum -qy install'
    PM_INSTALL_FILE='yum -qy install'
    PKG_GLOB='php-tarantool-*.x86_64.rpm'
elif type apt-get; then
    PM_SYNC='apt-get update'
    PM_INSTALL_NAME='apt-get -qy install'
    PM_INSTALL_FILE='dpkg -i'
    PKG_GLOB='php7.?-tarantool*_amd64.deb'
    # Prevent packages like tzdata from asking configuration
    # parameters interactively.
    # See https://github.com/packpack/packpack/issues/7
    export DEBIAN_FRONTEND=noninteractive
else
    echo "No suitable package manager found"
    exit 1
fi

# Update available packages list.
${PM_SYNC}

# Install php interpreter.
${PM_INSTALL_NAME} php

# Install php-tarantool package.
${PM_INSTALL_FILE} build/${PKG_GLOB}

# A kind of very minimaliztic test. Verify that Tarantool instance
# can be created. It means that we at least install the dynamic
# library and php configuration file into right places.
php -r "new Tarantool('127.0.0.1', 3301);"
