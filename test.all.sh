#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

php_version_list="7.0 7.1 7.2 7.3"
tarantool_version_list="1.6 1.7 1.9 1.10 2.1 2.2 2.3 2.4"

# gh-151: disable tarantool-2.2 due to lack of support the new
# _index format.
#
# Disable tarantool-1.6 and 1.7 on php 7.1-7.4, because
# php-7.[1-4]-cli docker images are based on Debian Buster, but we
# have no tarantool-1.[6-7] packages for this distribution.
exceptions="
    php-7.0-tarantool-2.2
    php-7.1-tarantool-1.6
    php-7.1-tarantool-1.7
    php-7.1-tarantool-2.2
    php-7.2-tarantool-1.6
    php-7.2-tarantool-1.7
    php-7.2-tarantool-2.2
    php-7.3-tarantool-1.6
    php-7.3-tarantool-1.7
    php-7.3-tarantool-2.2
    php-7.4-tarantool-1.6
    php-7.4-tarantool-1.7
    php-7.4-tarantool-2.2
"

for php_version in $php_version_list; do
    for tarantool_version in $tarantool_version_list; do
        echo "======================================"
        echo "PHP_VERSION=${php_version} TARANTOOL_VERSION=${tarantool_version}"
        echo "======================================"

        # Skip configurations from exceptions list.
        conf="php-${php_version}-tarantool-${tarantool_version}"
        if [ -z "${exceptions##*${conf}*}" ]; then
            echo "*** Skip the configuration ***"
            continue
        fi

        docker run                                       \
            --volume $(realpath .):/tarantool-php        \
            --workdir /tarantool-php                     \
            --env TARANTOOL_VERSION=${tarantool_version} \
            --rm                                         \
            php:${php_version}-cli                       \
            sh -c 'make clean; ./test.sh'
    done
done
