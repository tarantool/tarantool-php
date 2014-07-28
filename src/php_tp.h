#ifndef PHP_TP_H
#define PHP_TP_H

#include <ext/standard/php_smart_str.h>

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen);
size_t php_tp_sizeof_ping(uint32_t sync);

void php_tp_encode_auth(smart_str *str, uint32_t sync, char * const username, size_t username_len, char * const scramble);
void php_tp_encode_ping(smart_str *str, uint32_t sync);

#endif /* PHP_TP_H */
