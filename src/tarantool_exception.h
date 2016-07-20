#ifndef   PHP_TNT_EXCEPTION_H
#define   PHP_TNT_EXCEPTION_H

extern zend_class_entry *TarantoolException_ptr;
extern zend_class_entry *TarantoolIOException_ptr;
extern zend_class_entry *TarantoolClientError_ptr;
extern zend_class_entry *TarantoolParsingException_ptr;

PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_exception(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_ioexception(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_clienterror(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_parsingexception(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_exception_base(int root TSRMLS_DC);

zend_object *tarantool_throw_exception(const char *fmt, ...);
zend_object *tarantool_throw_ioexception(const char *fmt, ...);
zend_object *tarantool_throw_clienterror(uint32_t code, const char *err,
					 size_t errlen);
zend_object *tarantool_throw_parsingexception(const char *component);

ZEND_MINIT_FUNCTION(tarantool_exception);

#define THROW_EXC(...) tarantool_throw_exception(__VA_ARGS__)

#endif /* PHP_TNT_EXCEPTION_H */
