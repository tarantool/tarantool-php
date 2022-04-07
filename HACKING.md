# Hacking

A set of recipes, which may ease developer's work on the connector.

## Verify all configurations locally

```shell
$ ./test.all.sh
```

The script runs the whole test suite for the connector on all supported
tarantool and php versions.

The script runs docker containers based on the `php:<version>-cli` images for
each configuration. Within container the `test.sh` script is invoked. It
installs tarantool and php, builds the driver and runs tests.

Note: APT repositories updating takes significant time and it is performed for
each configuration.

## Verify all distributions locally

```shell
$ ./test.pkg.all.sh
```

The script builds a RPM / Deb package using [packpack][packpack], installs it
inside a docker container based on an appropriate image and verify that the
main class of the extension (Tarantool) is available from the PHP interpreter.
    
Note: The `packpack` script should be available in `PATH` directories.

## Release a new version

1. Write release notes. Look at `git show 0.3.3` for example.
2. Update `package.xml` and `src/php_tarantool.h` (see `git show 0.3.3` for
   example).
3. Open a pull request, get it merged. Checkout the fresh master.
4. Add **annotated** tag (`git tag -a --cleanup verbatim X.Y.Z`), place the
   release notes into the annotation. Push the tag (`git push --tags`).
5. Go to the releases page on GitHub and publish a new release. Place the
   release notes into the description.

## Build a package against ppa:ondrej/php (Ubuntu)

For ones who needs custom builds for php-7.3 or php-7.4 from `ppa:ondrej/php`
repository for Ubuntu Bionic.

Apply the following patch:

<details>
<summary>For php-7.3</summary>

```diff
diff --git a/debian/prebuild.sh b/debian/prebuild.sh
index c17d14c..b33595f 100755
--- a/debian/prebuild.sh
+++ b/debian/prebuild.sh
@@ -11,6 +11,12 @@ SUDO="sudo -E"
 ${SUDO} apt-get update > /dev/null
 ${SUDO} apt-get -y install php-all-dev

+# Build against php7.3 on Ubuntu Bionic.
+${SUDO} apt-get install software-properties-common
+${SUDO} add-apt-repository -y ppa:ondrej/php
+${SUDO} apt-get update > /dev/null
+${SUDO} apt-get install -y php7.3-dev
+
 phpversion=$(php-config --version | sed 's/^\([0-9]\+\.[0-9]\).*/\1/')

 cd /build/php-tarantool-*
diff --git a/test.pkg.all.sh b/test.pkg.all.sh
index fd74685..388cc62 100755
--- a/test.pkg.all.sh
+++ b/test.pkg.all.sh
@@ -3,27 +3,9 @@
 set -exu  # Strict shell (w/o -o pipefail)

 distros="
-    el:8
-    fedora:25
-    fedora:26
-    fedora:27
-    fedora:28
-    fedora:29
-    fedora:30
-    fedora:31
-    debian:stretch
-    debian:buster
-    ubuntu:xenial
     ubuntu:bionic
-    ubuntu:disco
-    ubuntu:eoan
 "

-if ! type packpack; then
-    echo "Unable to find packpack"
-    exit 1
-fi
-
 for distro in $distros; do
     export OS="${distro%%:*}"
     export DIST="${distro#*:}"
@@ -31,9 +13,6 @@ for distro in $distros; do
         export OS=centos
     fi

-    rm -rf build
-    packpack
-
     docker run \
         --volume "$(realpath .):/tarantool-php" \
         --workdir /tarantool-php                \
diff --git a/test.pkg.sh b/test.pkg.sh
index 3c1c271..e80d587 100755
--- a/test.pkg.sh
+++ b/test.pkg.sh
@@ -29,8 +29,14 @@ fi
 # Update available packages list.
 ${PM_SYNC}

+# Verify against php7.3 on Ubuntu Bionic.
+apt-get -y install software-properties-common
+add-apt-repository -y ppa:ondrej/php
+apt-get update > /dev/null
+
 # Install php interpreter.
-${PM_INSTALL_NAME} php
+apt-get install -y php7.3
+php --version

 # Install php-tarantool package.
 ${PM_INSTALL_FILE} build/${PKG_GLOB}
```
</details>

<details>
<summary>For php-7.4</summary>

```diff
diff --git a/debian/prebuild.sh b/debian/prebuild.sh
index c17d14c..b33595f 100755
--- a/debian/prebuild.sh
+++ b/debian/prebuild.sh
@@ -11,6 +11,12 @@ SUDO="sudo -E"
 ${SUDO} apt-get update > /dev/null
 ${SUDO} apt-get -y install php-all-dev

+# Build against php7.4 on Ubuntu Bionic.
+${SUDO} apt-get install software-properties-common
+${SUDO} add-apt-repository -y ppa:ondrej/php
+${SUDO} apt-get update > /dev/null
+${SUDO} apt-get install -y php7.4-dev
+
 phpversion=$(php-config --version | sed 's/^\([0-9]\+\.[0-9]\).*/\1/')

 cd /build/php-tarantool-*
diff --git a/test.pkg.all.sh b/test.pkg.all.sh
index fd74685..388cc62 100755
--- a/test.pkg.all.sh
+++ b/test.pkg.all.sh
@@ -3,27 +3,9 @@
 set -exu  # Strict shell (w/o -o pipefail)

 distros="
-    el:8
-    fedora:25
-    fedora:26
-    fedora:27
-    fedora:28
-    fedora:29
-    fedora:30
-    fedora:31
-    debian:stretch
-    debian:buster
-    ubuntu:xenial
     ubuntu:bionic
-    ubuntu:disco
-    ubuntu:eoan
 "

-if ! type packpack; then
-    echo "Unable to find packpack"
-    exit 1
-fi
-
 for distro in $distros; do
     export OS="${distro%%:*}"
     export DIST="${distro#*:}"
@@ -31,9 +13,6 @@ for distro in $distros; do
         export OS=centos
     fi

-    rm -rf build
-    packpack
-
     docker run \
         --volume "$(realpath .):/tarantool-php" \
         --workdir /tarantool-php                \
diff --git a/test.pkg.sh b/test.pkg.sh
index 3c1c271..e80d587 100755
--- a/test.pkg.sh
+++ b/test.pkg.sh
@@ -29,8 +29,14 @@ fi
 # Update available packages list.
 ${PM_SYNC}

+# Verify against php7.4 on Ubuntu Bionic.
+apt-get -y install software-properties-common
+add-apt-repository -y ppa:ondrej/php
+apt-get update > /dev/null
+
 # Install php interpreter.
-${PM_INSTALL_NAME} php
+apt-get install -y php7.4
+php --version

 # Install php-tarantool package.
 ${PM_INSTALL_FILE} build/${PKG_GLOB}
```
</details>

Steps:

```sh
$ OS=ubuntu DIST=bionic /path/to/packpack/packpack # build
$ ./test.pkg.all.sh # verify
```

A package is in `build/` directory, named like
`php7.4-tarantool_0.3.0.40-1_amd64.deb`.

## Packaging (obsolete)

Those packaging recipes are likely outdated. Kept to revisit them later.

### RPM package

To build an RPM run:

```
git archive --format tar ${VERSION} | gzip -9 > ${HOME}/rpmbuild/SOURCES/${VERSION}.tar.gz
cp rpm/tarantool.ini ${HOME}/rpmbuild/SOURCES/
rpmbuild -bb rpm/php-tarantool.spec
```

When the build ends you'll find an RPM in `${HOME}/rpmbuild/(x86_64|i386)/`.
Current `${VERSION}` is `0.0.5`.

### DEBIAN package

To build a DEB package, run `fakeroot debian/rules binary` in the root.

### PECL package

You must have PEAR/PECL Installed in your system, and then `pear package` in
the root.

[packpack]: https://github.com/packpack/packpack
