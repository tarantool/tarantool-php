#ifndef PHP_MSGPACK_H
#define PHP_MSGPACK_H

#include <ext/standard/php_smart_str.h>

size_t php_mp_check  (const char *str,  size_t str_size);
void   php_mp_pack   (smart_str  *buf,  zval   *val    );
char  *php_mp_unpack (zval      **oval, char   **str   );
void   php_mp_pack_package_size   (smart_str *str, size_t val);
size_t php_mp_unpack_package_size (char *buf);

#endif /* PHP_MSGPACK_H */
