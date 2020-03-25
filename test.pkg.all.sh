#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

distros="
    el:8
    fedora:25
    fedora:26
    fedora:27
    fedora:28
    fedora:29
    fedora:30
    fedora:31
    debian:stretch
    debian:buster
    ubuntu:xenial
    ubuntu:bionic
    ubuntu:disco
    ubuntu:eoan
"

if ! type packpack; then
    echo "Unable to find packpack"
    exit 1
fi

for distro in $distros; do
    export OS="${distro%%:*}"
    export DIST="${distro#*:}"
    if [ "${OS}" = "el" ]; then
        export OS=centos
    fi

    rm -rf build
    packpack

    docker run \
        --volume "$(realpath .):/tarantool-php" \
        --workdir /tarantool-php                \
        --rm                                    \
        "${OS}:${DIST}"                         \
        ./test.pkg.sh
done
