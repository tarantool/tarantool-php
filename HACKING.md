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
