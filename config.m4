dnl config.m4 for extension tarantool
PHP_ARG_WITH(tarantool, for tarantool support,
[  --with-tarantool	Enable tarantool support])

if test "$PHP_TARANTOOL" != "no"; then
    PHP_NEW_EXTENSION(tarantool,   \
        src/tarantool.c            \
        src/tarantool_network.c    \
        src/tarantool_msgpack.c    \
        src/tarantool_manager.c    \
        src/tarantool_schema.c     \
        src/tarantool_proto.c      \
        src/tarantool_tp.c         \
        src/third_party/msgpuck.c  \
        src/third_party/sha1.c     \
        src/third_party/base64.c   \
        src/third_party/PMurHash.c \
        , $ext_shared)
    PHP_ADD_BUILD_DIR([$ext_builddir/src/])
    PHP_ADD_BUILD_DIR([$ext_builddir/src/third_party])
fi
