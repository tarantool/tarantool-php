dnl config.m4 for extension tarantool
PHP_ARG_WITH(tarantool, for tarantool support,
[  --with-tarantool	Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
   PHP_ADD_INCLUDE([$ext_builddir/src])
   PHP_NEW_EXTENSION(tarantool, src/tarantool.c, $ext_shared)
fi
