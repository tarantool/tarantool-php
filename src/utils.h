#ifndef   PHP_TNT_UTILS_H
#define   PHP_TNT_UTILS_H

const char *tutils_op_to_string(zval *obj);
void tutils_hexdump_base (FILE *ostream, char *desc, const char *addr, size_t len);
void tutils_hexdump (char *desc, const char *addr, size_t len);
void tutils_hexdump_zs (char *desc, zend_string *val);
void tutils_hexdump_ss (char *desc, smart_string *val);

#endif /* PHP_TNT_UTILS_H */
