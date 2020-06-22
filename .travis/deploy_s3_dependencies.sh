#!/bin/sh

# Make shell strictier.
#
# - Exit with a failure on a first failed command.
# - Exit with a failure on an attempt to use an unset variable.
# - Print each executed commmand.
set -eux

# Prevent procmail package from asking configuration parameters
# interactively.
# See https://github.com/packpack/packpack/issues/7
export DEBIAN_FRONTEND=noninteractive
SUDO="sudo -E"

${SUDO} apt-get update > /dev/null

${SUDO} apt-get install -y pcregrep
${SUDO} apt-get install -y procmail  # for lockfile tool
${SUDO} apt-get install -y awscli
${SUDO} apt-get install -y reprepro
${SUDO} apt-get install -y createrepo
