dnl config.m4 for extension tarantool
PHP_ARG_WITH(tarantool, for tarantool support,
[  --with-tarantool	Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
    PHP_ADD_INCLUDE([$ext_builddir/src/third_party])
    PHP_NEW_EXTENSION(tarantool,        \
        src/tarantool.c                 \
        src/tarantool_msgpack.c         \
        src/tarantool_manager.c         \
        src/tarantool_schema.c          \
        src/tarantool_proto.c           \
        src/tarantool_tp.c              \
        src/third_party/msgpuck.c       \
        src/third_party/PMurHash.c      \
        , $ext_shared)
fi
