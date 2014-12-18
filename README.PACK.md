RPM Package
===============================================================================

To build an RPM run:
```
git archive --format tar ${VERSION} | gzip -9 > ${HOME}/rpmbuild/SOURCES/${VERSION}.tar.gz
cp rpm/tarantool.ini ${HOME}/rpmbuild/SOURCES/
rpmbuild -bb rpm/php-tarantool.spec
```
When the build ends you'll find an RPM in `${HOME}/rpmbuild/(x86_64|i386)/`
Current `${VERSION}` is `0.0.4`

DEBIAN Package
===============================================================================
To build a DEB package, run `fakeroot debian/rules binary` in the root.

PECL Package
===============================================================================
You must have PEAR/PECL Installed in your system, and then `pear package` in the root
