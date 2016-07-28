#include "php_tarantool.h"

#include "tarantool_internal.h"

#include "tarantool_exception.h"

zend_class_entry *TarantoolException_ptr;
zend_class_entry *TarantoolIOException_ptr;
zend_class_entry *TarantoolClientError_ptr;
zend_class_entry *TarantoolParsingException_ptr;

#if HAVE_SPL
/* Pointer to SPL Runtime Exception */
static zend_class_entry *SPLRE_ptr = NULL;
static const char SPLRE_name[]     = "runtimeexception";
static size_t SPLRE_name_len       = sizeof(SPLRE_name);
#endif

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_exception(void)
{
	return TarantoolException_ptr;
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_ioexception(void)
{
	return TarantoolIOException_ptr;
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_clienterror(void)
{
	return TarantoolClientError_ptr;
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_parsingexception(void)
{
	return TarantoolParsingException_ptr;
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_exception_base(int root) {
	TSRMLS_FETCH();
#if HAVE_SPL
	if (!root) {
		if (SPLRE_ptr == NULL) {
			zval *pce_z = NULL;
			zend_string *key = zend_string_init(SPLRE_name,
							    SPLRE_name_len, 0);
			if ((pce_z = zend_hash_find(CG(class_table),
						    key)) != NULL)
				SPLRE_ptr = Z_CE_P(pce_z);
			zend_string_release(key);
		}
		if (SPLRE_ptr != NULL)
			return SPLRE_ptr;
	}
#endif
	return zend_ce_exception;
}

zend_object *tarantool_throw_exception_vbase(zend_class_entry *ce,
					     uint32_t code, const char *fmt,
					     va_list arg) {
	char *message;
	zend_object *obj;

	vspprintf(&message, 0, fmt, arg);
	va_end(arg);
	obj = zend_throw_exception(ce, (const char *)message, code TSRMLS_DC);
	efree(message);
	return obj;
}

zend_object *tarantool_throw_exception_base(zend_class_entry *ce, uint32_t code,
					    const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);

	return tarantool_throw_exception_vbase(ce, code, fmt, arg);
}

zend_object *tarantool_throw_exception(const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);

	return tarantool_throw_exception_vbase(TarantoolException_ptr, 0, fmt,
					       arg);
}

zend_object *tarantool_throw_ioexception(const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);

	return tarantool_throw_exception_vbase(TarantoolIOException_ptr, 0, fmt,
					       arg);
}

zend_object *tarantool_throw_clienterror(uint32_t code, const char *err,
					 size_t errlen) {
	return tarantool_throw_exception_base(TarantoolClientError_ptr, code,
					      "%.*s", errlen, err);
}

zend_object *tarantool_throw_parsingexception(const char *component) {
	return tarantool_throw_exception_base(TarantoolParsingException_ptr, 0,
					      "Failed to parse %s", component);
}

ZEND_MINIT_FUNCTION(tarantool_exception) {
	/* Init class entries */
	zend_class_entry tarantool_xc_class;
	zend_class_entry tarantool_io_xc_class;
	zend_class_entry tarantool_client_er_class;
	zend_class_entry tarantool_parsing_xc_class;

	TNT_INIT_CLASS_ENTRY(tarantool_xc_class, "TarantoolException",
			     "Tarantool\\Exception", NULL);
	TarantoolException_ptr = zend_register_internal_class_ex(
			&tarantool_xc_class,
			php_tarantool_get_exception_base(0)
	);
	TNT_INIT_CLASS_ENTRY(tarantool_io_xc_class, "TarantoolIOException",
			     "Tarantool\\Exception\\IOException", NULL);
	TarantoolIOException_ptr = zend_register_internal_class_ex(
			&tarantool_io_xc_class,
			TarantoolException_ptr
	);
	TNT_INIT_CLASS_ENTRY(tarantool_client_er_class, "TarantoolClientError",
			     "Tarantool\\Exception\\ClientError", NULL);
	TarantoolClientError_ptr = zend_register_internal_class_ex(
			&tarantool_client_er_class,
			TarantoolException_ptr
	);
	TNT_INIT_CLASS_ENTRY(tarantool_parsing_xc_class,
			     "TarantoolParsingException",
			     "Tarantool\\Exception\\ParsingException",
			     NULL);
	TarantoolParsingException_ptr = zend_register_internal_class_ex(
			&tarantool_parsing_xc_class,
			TarantoolException_ptr
	);

	return SUCCESS;
}
