#!/bin/sh

# Deploy to packagecloud repositories
# -----------------------------------
#
# `deploy_packagecloud.sh` is equivalent to
# `deploy_packagecloud.sh staging`.
#
# `deploy_packagecloud.sh staging` requires the following
# environment variables:
#
# - OS
# - DIST
# - DEPLOY_STAGING_PACKAGECLOUD_USER
# - DEPLOY_STAGING_PACKAGECLOUD_TOKEN
#
# `deploy_packagecloud.sh production` requires the following
# environment variables:
#
# - OS
# - DIST
# - DEPLOY_PRODUCTION_PACKAGECLOUD_USER
# - DEPLOY_PRODUCTION_PACKAGECLOUD_TOKEN
#
# If one of those variables is not set or empty, then deployment
# will be skipped.

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

# Choose credentials.
if [ ${configuration} = staging ]; then
    DEPLOY_PACKAGECLOUD_USER="${DEPLOY_STAGING_PACKAGECLOUD_USER:-}"
    DEPLOY_PACKAGECLOUD_TOKEN="${DEPLOY_STAGING_PACKAGECLOUD_TOKEN:-}"
elif [ ${configuration} = production ]; then
    DEPLOY_PACKAGECLOUD_USER="${DEPLOY_PRODUCTION_PACKAGECLOUD_USER:-}"
    DEPLOY_PACKAGECLOUD_TOKEN="${DEPLOY_PRODUCTION_PACKAGECLOUD_TOKEN:-}"
else
    echo "Unknown configuration: ${configuration}"
    exit 1
fi

# Skip deployment if some variables are not set or empty.
if [ -z "${OS:-}" ] || [ -z "${DIST:-}" ]    || \
        [ -z "${DEPLOY_PACKAGECLOUD_USER}" ] || \
        [ -z "${DEPLOY_PACKAGECLOUD_TOKEN}" ]; then
    echo "Skip deployment: some of necessary environment"
    echo "variables are not set or empty"
    exit 0
fi

# Verify that packpack is cloned into the current directory.
packagecloud_tool=./packpack/tools/packagecloud
if [ ! -f "${packagecloud_tool}" ]; then
    echo "Could not find ${packagecloud_tool}"
    exit 1
fi

# Staging repository: keep older packages in case of a
# version clash.
#
# It would be better to replace old ones, but there is no
# such option in the packagecloud tool we use. It may be
# important if we'll have some manual or automatic testing
# upward a staging repository. But at least CI will not fail
# because a package is already exists.
push_args=""
if [ "${configuration}" = staging ]; then
    push_args="${push_args} --ignore-duplicates"
fi

# Setup environment variables for the packagecloud tool.
export PACKAGECLOUD_TOKEN="${DEPLOY_PACKAGECLOUD_TOKEN}"

# FIXME: It would be good to upstream the repos_list command.
(cd packpack/tools &&
    patch -p1 -i ../../.travis/packagecloud-list-repos.patch)

# Fetch list of repositories from packagecloud.io.
#
# Set default repositories if the attempt to fetch them fails.
#
# We have tarantool repositories on packagecloud.io up to
# 2_4. The next ones present only in the S3 based storage.
repositories=""
for i in $(seq 1 5); do
    repositories="$(${packagecloud_tool} list_repos || true)"
    [ -n "${repositories}" ] && break
done
[ -z "${repositories}" ] && repositories="1_6 1_7 1_9 1_10 2x 2_2 2_3 2_4"

for repo in ${repositories}; do
    # FIXME: Enable *.ddeb when packagecloud.io will support it.
    for file in build/*.rpm build/*.deb build/*.dsc; do
        extension=${file##*.}

        # Skip non-matched globs: say, build/*.rpm on Debian.
        basename="$(basename "${file}" ".${extension}")"
        [ "${basename}" = "*" ] && continue

        # Push all source files listed in .dsc file together with
        # the file.
        #
        # FIXME: It seems logical to move this logic to the
        # packagecloud tool we use.
        files="${file}"
        if [ "${extension}" = "dsc" ]; then
             parse_dsc_file='{
                 if ($0 == "Files:") {
                     FILES_SECTION = 1;
                 } else if (FILES_SECTION != 0) {
                     print "build/"$3;
                 }
             }'
             files="${files} $(awk "${parse_dsc_file}" "${file}")"
        fi

        user=${DEPLOY_PACKAGECLOUD_USER}

        # Retry failed attempts to upload a package.
        #
        # packagecloud.io sometimes replieds with 502 Bad Gateway
        # for attempts to push, so retrying is important here.
        #
        # FIXME: This way we don't differentiate network errors
        # and all other ones. It would be much better to retry
        # from inside the packagecloud tool (requests library
        # supports it).
        for i in $(seq 1 5); do
            # FIXME: The tool fetches distributions.json each
            # time. It can cache the data somewhere and reuse
            # during some time period until expiration.
            ${packagecloud_tool} push ${push_args} ${user}/${repo} \
                ${extension} ${OS} ${DIST} ${files} && break
        done
    done
done
