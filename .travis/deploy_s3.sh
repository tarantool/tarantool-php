#!/bin/sh

# Deploy to S3 based repositories
# -------------------------------
#
# `deploy_s3.sh` is equivalent to `deploy_s3.sh staging`.
#
# `deploy_s3.sh staging` requires the following environment
# variables:
#
# - OS
# - DIST
# - DEPLOY_STAGING_S3_ENDPOINT_URL="https://..."
# - DEPLOY_STAGING_S3_LIVE_DIR="s3://my_bucket/foo/bar/live"
# - DEPLOY_STAGING_S3_RELEASE_DIR="s3://my_bucket/foo/bar/release"
# - DEPLOY_STAGING_S3_ACCESS_KEY_ID
# - DEPLOY_STAGING_S3_SECRET_ACCESS_KEY
# - DEPLOY_STAGING_S3_GPG_KEY_FILE_KEY (32 bytes in hex)
# - DEPLOY_STAGING_S3_GPG_KEY_FILE_IV  (16 bytes in hex)
#
# `deploy_s3.sh production` requires the following environment
# variables:
#
# - OS
# - DIST
# - DEPLOY_PRODUCTION_S3_ENDPOINT_URL="https://..."
# - DEPLOY_PRODUCTION_S3_LIVE_DIR="s3://my_bucket/foo/bar/live"
# - DEPLOY_PRODUCTION_S3_RELEASE_DIR="s3://my_bucket/foo/bar/release"
# - DEPLOY_PRODUCTION_S3_ACCESS_KEY_ID
# - DEPLOY_PRODUCTION_S3_SECRET_ACCESS_KEY
# - DEPLOY_PRODUCTION_S3_GPG_KEY_FILE_KEY (32 bytes in hex)
# - DEPLOY_PRODUCTION_S3_GPG_KEY_FILE_IV  (16 bytes in hex)
#
# If one of those variables is not set or empty, then deployment
# will be skipped.
#
# The following optional variable will be used to group related
# packages into one directory within a Debian / Ubuntu repository.
# If not set or empty, the variable will be guessed like it is
# done in packpack:
#
# - PRODUCT
#
# **Production** repository directory and credentials will be used
# to fetch a repository list (1.10, 2.1, ...) to deploy into them.
# The production list will be used as for the production
# configuration as well as for the staging one (!). The following
# optional variables should be set to fetch the list, otherwise
# the default one will be used (1.10, 2.1, 2.2, 2.3, 2.4, 2.5):
#
# - DEPLOY_PRODUCTION_S3_ENDPOINT_URL
# - DEPLOY_PRODUCTION_S3_ACCESS_KEY_ID
# - DEPLOY_PRODUCTION_S3_SECRET_ACCESS_KEY
# - DEPLOY_PRODUCTION_S3_ENDPOINT_URL
# - DEPLOY_PRODUCTION_S3_LIVE_DIR

# Make shell strictier.
#
# - Exit with a failure on a first failed command.
# - Exit with a failure on an attempt to use an unset variable.
# - Print each executed commmand.
#
# Note: The script expects that Travis-CI will filter sensitive
# information (such as a token): 'Display value in build log'
# toogle should be OFF for to keep a value secure.
set -eux

configuration=${1:-staging}

# Choose URLs, directories, keys and so.
if [ ${configuration} = staging ]; then
    DEPLOY_S3_ENDPOINT_URL="${DEPLOY_STAGING_S3_ENDPOINT_URL:-}"
    DEPLOY_S3_LIVE_DIR="${DEPLOY_STAGING_S3_LIVE_DIR:-}"
    DEPLOY_S3_RELEASE_DIR="${DEPLOY_STAGING_S3_RELEASE_DIR:-}"
    DEPLOY_S3_ACCESS_KEY_ID="${DEPLOY_STAGING_S3_ACCESS_KEY_ID:-}"
    DEPLOY_S3_SECRET_ACCESS_KEY="${DEPLOY_STAGING_S3_SECRET_ACCESS_KEY:-}"
    DEPLOY_S3_GPG_KEY_FILE_KEY="${DEPLOY_STAGING_S3_GPG_KEY_FILE_KEY:-}"
    DEPLOY_S3_GPG_KEY_FILE_IV="${DEPLOY_STAGING_S3_GPG_KEY_FILE_IV:-}"
elif [ ${configuration} = production ]; then
    DEPLOY_S3_ENDPOINT_URL="${DEPLOY_PRODUCTION_S3_ENDPOINT_URL:-}"
    DEPLOY_S3_LIVE_DIR="${DEPLOY_PRODUCTION_S3_LIVE_DIR:-}"
    DEPLOY_S3_RELEASE_DIR="${DEPLOY_PRODUCTION_S3_RELEASE_DIR:-}"
    DEPLOY_S3_ACCESS_KEY_ID="${DEPLOY_PRODUCTION_S3_ACCESS_KEY_ID:-}"
    DEPLOY_S3_SECRET_ACCESS_KEY="${DEPLOY_PRODUCTION_S3_SECRET_ACCESS_KEY:-}"
    DEPLOY_S3_GPG_KEY_FILE_KEY="${DEPLOY_PRODUCTION_S3_GPG_KEY_FILE_KEY:-}"
    DEPLOY_S3_GPG_KEY_FILE_IV="${DEPLOY_PRODUCTION_S3_GPG_KEY_FILE_IV:-}"
else
    echo "Unknown configuration: ${configuration}"
    exit 1
fi

# Skip deployment if some variables are not set or empty.
if [ -z "${OS:-}" ] || [ -z "${DIST:-}" ]       || \
        [ -z "${DEPLOY_S3_ENDPOINT_URL}" ]      || \
        [ -z "${DEPLOY_S3_LIVE_DIR}" ]          || \
        [ -z "${DEPLOY_S3_RELEASE_DIR}" ]       || \
        [ -z "${DEPLOY_S3_ACCESS_KEY_ID}" ]     || \
        [ -z "${DEPLOY_S3_SECRET_ACCESS_KEY}" ] || \
        [ -z "${DEPLOY_S3_GPG_KEY_FILE_KEY}" ]  || \
        [ -z "${DEPLOY_S3_GPG_KEY_FILE_IV}" ]; then
    echo "Skip deployment: some of necessary environment"
    echo "variables are not set or empty"
    exit 0
fi

# Download the tool to deploy to an S3 based repository.
ref=f84cb1aae3144f5677feacf6be31bd4f15e91c2d
base_url="https://raw.githubusercontent.com/tarantool/tarantool/${ref}"
curl -Ssfo update_repo.sh "${base_url}/tools/update_repo.sh"
chmod a+x update_repo.sh

# FIXME: Upstream the patches.
patch -p1 -i .travis/gh-5112-update-repo-sh-use-right-gpg-key.patch
patch -p1 -i .travis/gh-5113-update-repo-sh-add-fedora-25-26.patch
patch -p1 -i .travis/gh-5114-update-repo-sh-fix-unbound-var-access.patch

# Decrypt a GPG key.
gpg_key_file=".travis/deploy_${configuration}_s3_gpg_private_key.asc"
openssl aes-256-cbc -K "${DEPLOY_S3_GPG_KEY_FILE_KEY}"           \
    -iv "${DEPLOY_S3_GPG_KEY_FILE_IV}" -in "${gpg_key_file}.enc" \
    -out "${gpg_key_file}" -d

# Import GPG key for signing repository files.
gpg --import --batch "${gpg_key_file}"

# Extract GPG key id for signing repository files.
#
# This way works for both GnuPG 1 and GnuPG 2. The alternative
# would be using '--import-options show-only', but it is available
# only in GnuPG 2. See https://unix.stackexchange.com/a/468889
mkdir -m 0700 temp-gpg-home
gpg --homedir temp-gpg-home --import --batch "${gpg_key_file}"
export GPG_SIGN_KEY="$(gpg --homedir temp-gpg-home --list-secret-keys \
    --with-colons | grep ^sec: | cut -d: -f5)"
rm -rf temp-gpg-home

# Use SHA256 hashing algorithm for files signing.
#
# `apt-get update` gives a warning when InRelease file signature
# is calculated with SHA1. We should configure GnuPG (which is
# used by reprepro, which is used by update_repo.sh) to sign using
# SHA265.
#
# https://askubuntu.com/a/819868
mkdir -p ~/.gnupg
echo 'digest-algo sha256' >> ~/.gnupg/gpg.conf

# Setup arguments that are common for all repositories
# (1.10, 2.1, ...).
update_repo_args="--os=${OS} --distribution=${DIST}"

# ${PRODUCT} value may affect location of *.deb, *.rpm and related
# files relative to a base repository URL. We can provide it or
# miss: the script will generate correct repository metainfo
# anyway.
#
# However providing meaningful value for this option enables
# grouping of related set of packages into a subdirectory named as
# ${PRODUCT} (only for Deb repositories at moment of writing
# this).
#
# It is enabled here for consistency with locations of other Deb
# packages in our repositories, but in fact it is the internal
# detail, which does not lead to any change in the user
# experience.

# Guess PRODUCT value in the similar way as packpack does.
#
# Guess from Debian package
[ -z "${PRODUCT:-}" ] && PRODUCT="$(grep Source: debian/control \
    2>/dev/null | awk '{ print $2; }' || true)"
# Guess from Debian package (control file template)
[ -z "${PRODUCT:-}" ] && PRODUCT="$(grep Source: debian/control.in \
    2>/dev/null | awk '{ print $2; }' || true)"
# Guess from RPM package
[ -z "${PRODUCT:-}" ] && PRODUCT="$(grep Name: rpm/*.spec \
    2>/dev/null | awk '{ print $2; }' || true)"
# Guess from git repository name
[ -z "${PRODUCT:-}" ] && PRODUCT="$(git config --get remote.origin.url | \
    sed -e 's/.*\///' -e 's/.git$//' || true)"
# Guess from directory name
[ -z "${PRODUCT:-}" ] && PRODUCT="$(basename "$(pwd)" || true)"

# Add --product option if there is ${PRODUCT} value. Otherwise it
# will fall back to default 'tarantool'.
if [ -n "${PRODUCT:-}" ]; then
    update_repo_args="${update_repo_args} --product=${PRODUCT}"
fi

# Staging repository: rewrite a package if there is a previous one
# of the same version.
#
# Note: It differs from a logic in deploy_packagecloud.sh.
if [ "${configuration}" = staging ]; then
    update_repo_args="${update_repo_args} --force"
fi

# Fetch actual list of repositories (1.10, 2.1, ...).
#
# Always use production repositories list: even for staging
# deployments. The reason is that it is always actual, while
# we cannot guarantee it for staging repositories.
#
# Note: It differs from a logic in deploy_packagecloud.sh.
repositories=""
if [ -n "${DEPLOY_PRODUCTION_S3_ACCESS_KEY_ID:-}" ] && \
        [ -n "${DEPLOY_PRODUCTION_S3_SECRET_ACCESS_KEY:-}" ] && \
        [ -n "${DEPLOY_PRODUCTION_S3_ENDPOINT_URL:-}" ] && \
        [ -n "${DEPLOY_PRODUCTION_S3_LIVE_DIR:-}" ]; then
    export AWS_ACCESS_KEY_ID="${DEPLOY_PRODUCTION_S3_ACCESS_KEY_ID}"
    export AWS_SECRET_ACCESS_KEY="${DEPLOY_PRODUCTION_S3_SECRET_ACCESS_KEY}"

    url="${DEPLOY_PRODUCTION_S3_ENDPOINT_URL}"
    # Single slash at end matters.
    dir="${DEPLOY_PRODUCTION_S3_LIVE_DIR%%/}/"

    repositories="$(aws --endpoint-url "${url}" s3 ls "${dir}" | \
        grep PRE | awk '{ print $2; }' | sed 's@/$@@' || true)"
fi
[ -z "${repositories}" ] && repositories="1.10 2.1 2.2 2.3 2.4 2.5"

# Setup environment variables for the update_repo.sh tool.
export AWS_S3_ENDPOINT_URL="${DEPLOY_S3_ENDPOINT_URL}"
export AWS_ACCESS_KEY_ID="${DEPLOY_S3_ACCESS_KEY_ID}"
export AWS_SECRET_ACCESS_KEY="${DEPLOY_S3_SECRET_ACCESS_KEY}"

# Deploy to S3 based repositories.
for repo in ${repositories}; do
    # Note: The update_repo.sh tool automatically find
    # *.{rpm,deb,dsc} within a passed directory, so we just
    # pass the directory name: 'build'.

    # FIXME: Machine-local locking that is used in the
    # update_repo.sh tool is insufficient when we deploy from a
    # just created virtual machine.

    # Deploy to live repository (per-push).
    bucket="${DEPLOY_S3_LIVE_DIR}/${repo}"
    ./update_repo.sh ${update_repo_args} --bucket="${bucket}" build

    # Deploy to release repository (tagged commits).
    if [ -n "${TRAVIS_TAG:-}" ]; then
        bucket="${DEPLOY_S3_RELEASE_DIR}/${repo}"
        ./update_repo.sh ${update_repo_args} --bucket="${bucket}" build
    fi
done
