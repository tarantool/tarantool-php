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
