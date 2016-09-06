#ifndef PHP_TNT_MSGPACK_H
#define PHP_TNT_MSGPACK_H

#include "php_tarantool.h"

#define PHP_MP_SERIALIZABLE_P(v) (Z_TYPE_P(v) == IS_NULL   || \
				  Z_TYPE_P(v) == IS_LONG   || \
				  Z_TYPE_P(v) == IS_DOUBLE || \
				  Z_TYPE_P(v) == IS_FALSE  || \
				  Z_TYPE_P(v) == IS_TRUE   || \
				  Z_TYPE_P(v) == IS_ARRAY  || \
				  Z_TYPE_P(v) == IS_STRING || \
				  Z_TYPE_P(v) == IS_REFERENCE)

size_t  php_mp_check  (const char   *str,  size_t str_size);
void    php_mp_pack   (smart_string *buf,  zval   *val    );
ssize_t php_mp_unpack (zval         *oval, char   **str   );
size_t  php_mp_sizeof (zval *val);

void   php_mp_pack_package_size       (smart_string *str, size_t val);
void   php_mp_pack_package_size_basic (char *pos, size_t val);
size_t php_mp_unpack_package_size     (char *buf);

int php_mp_is_hash(zval *val);

void php_mp_pack_nil(smart_string *str);
void php_mp_pack_long_pos(smart_string *str, long val);
void php_mp_pack_long_neg(smart_string *str, long val);
void php_mp_pack_long(smart_string *str, long val);
void php_mp_pack_double(smart_string *str, double val);
void php_mp_pack_bool(smart_string *str, unsigned char val);
void php_mp_pack_string(smart_string *str, const char *c, size_t len);
void php_mp_pack_hash(smart_string *str, size_t len);
void php_mp_pack_array(smart_string *str, size_t len);
void php_mp_pack_array_recursively(smart_string *str, zval *val);
void php_mp_pack_hash_recursively(smart_string *str, zval *val);

size_t php_mp_sizeof_nil();
size_t php_mp_sizeof_long_pos(long val);
size_t php_mp_sizeof_long_neg(long val);
size_t php_mp_sizeof_long(long val);
size_t php_mp_sizeof_double(double val);
size_t php_mp_sizeof_bool(unsigned char val);
size_t php_mp_sizeof_string(size_t len);
size_t php_mp_sizeof_hash(size_t len);
size_t php_mp_sizeof_array(size_t len);
size_t php_mp_sizeof_array_recursively(zval *val);
size_t php_mp_sizeof_hash_recursively(zval *val);

int  smart_string_ensure(smart_string *str, size_t len);
void smart_string_nullify(smart_string *str);
#endif /* PHP_TNT_MSGPACK_H */
