#!/bin/sh

set -exu  # Strict shell (w/o -o pipefail)

# Preserve DEBIAN_FRONTEND=noninteractive. It prevents packages
# like tzdata from asking configuration parameters interactively.
#
# See https://github.com/packpack/packpack/issues/7
SUDO="sudo -E"

${SUDO} apt-get update > /dev/null
${SUDO} apt-get -y install php-all-dev

phpversion=$(php-config --version | sed 's/^\([0-9]\+\.[0-9]\).*/\1/')

cd /build/php-tarantool-*
sed -e "s/\${phpversion}/${phpversion}/" debian/control.in > debian/control
rm debian/control.in

sed -e "s/\${phpversion}/${phpversion}/" debian/php-tarantool.postinst.in \
    > debian/php${phpversion}-tarantool.postinst
sed -e "s/\${phpversion}/${phpversion}/" debian/php-tarantool.postrm.in \
    > debian/php${phpversion}-tarantool.postrm
