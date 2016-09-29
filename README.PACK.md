RPM package
===============================================================================

To build an RPM package, run:
```
git archive --format tar ${VERSION} | gzip -9 > ${HOME}/rpmbuild/SOURCES/${VERSION}.tar.gz
cp rpm/tarantool.ini ${HOME}/rpmbuild/SOURCES/
rpmbuild -bb rpm/php-tarantool.spec
```

When the build is completed, you'll find an RPM package in `${HOME}/rpmbuild/(x86_64|i386)/`.
The current `${VERSION}` is `0.0.5`.

DEBIAN package
===============================================================================

To build a DEB package, run `fakeroot debian/rules binary` in the root.

PECL package
===============================================================================

You must have PEAR/PECL installed in your system.

To build a PECL package, run `pear package` in the root.