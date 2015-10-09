PHP driver for Tarantool 1.6
============================

PECL PHP driver for Tarantool [![Build Status](https://travis-ci.org/tarantool/tarantool-php.png?branch=master)](https://travis-ci.org/tarantool/tarantool-php)

If you're looking for 1.5 version, check out branch 'stable'.


## Build

To build Tarantool PHP extenstion PHP-devel package is required. The
package should contain phpize utility.

```sh
$ phpize
$ ./configure
$ make
$ make install
```

## Test

To run tests Taranool server and PHP/PECL package are requred.

```sh
$ ./test-run.py
```

It'll automaticly find and start Tarantool and, then, run `phpunit.phar` based tests.
If Tarantool doesn't defined in `PATH` variable, you may define it in `TARANTOOL_BOX_PATH` enviroment variable.

```sh
$ TARANTOOL_BOX_PATH=/path/to/tarantool/bin/tarantool ./test-run.py
```


## Installing from PEAR

Tarantool-PHP Have it's own [PEAR repository](https://tarantool.github.io/tarantool-php).
You may install it from PEAR with just a few commands:

```
pecl channel-discover tarantool.github.io/tarantool-php/pecl
pecl install Tarantool-PHP/Tarantool-beta
```


## Building RPM/DEB/PECL Packages

For building packages - please, read `README.PACK.md`


## IDE autocompletion

Stubs can be found at [tarantool/tarantool-php-stubs](https://github.com/tarantool/tarantool-php-stubs).
Place it into project library path in your IDE.

