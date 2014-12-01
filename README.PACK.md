RPM Package
===============================================================================

To build RPM you need to execute:
```
git archive --format tar ${VERSION} | gzip -9 > ${HOME}/rpmbuild/SOURCES/${VERSION}.tar.gz
cp rpm/tarantool.ini ${HOME}/rpmbuild/SOURCES/
rpmbuild -bb rpm/php-tarantool.spec
```
When build will be ended you'll find you RPM in the `${HOME}/rpmbuild/(x86_64|i386)/`
Current `${VERSION}` is `0.0.4`

DEBIAN Package
===============================================================================
To build simple DEB you need to execute `fakeroot debian/rules binary` in the root, or

PECL Package
===============================================================================
You must have PEAR/PECL Installed in your system, and then `pear package` in the root
