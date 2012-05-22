tarantool-php
=============

PECL PHP driver for Tarantool/Box

Build

To build Tarantool PHP extenstion PHP-devel package is requred. The
package should contain phpize utility.

$ phpize
$ ./configure
$ make
# make install

Test

To run tests Taranool/Box server and PHP/pecl package are requred.

$ cd test
$ ./run-tests

If Tarantool/Box doesn't define in PATH variable, you may define it in
TARANTOOL_BOX_BIN enviroment variable.

$ TARANTOOL_BOX_BIN=/path/to/tarantool/bin/tarantool_box ./run-tests
