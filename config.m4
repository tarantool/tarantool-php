dnl config.m4 for extension tarantool
PHP_ARG_WITH(tarantool, for tarantool support,
[  --with-tarantool	Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
   PHP_ADD_INCLUDE([$ext_builddir/src/third_party])
   PHP_NEW_EXTENSION(tarantool, src/php_tarantool.c src/php_msgpack.c src/php_tp.c src/third_party/msgpuck.c, $ext_shared)
fi
