#ifndef    PHP_TARANTOOL_H
#define    PHP_TARANTOOL_H

extern zend_module_entry tarantool_module_entry;
#define phpext_tarantool_ptr &tarantool_module_entry

#define PHP_TARANTOOL_VERSION "0.1"
#define PHP_TARANTOOL_EXTNAME "tarantool"

#ifdef PHP_WIN32
#  define PHP_TARANTOOL_API __declspec(__dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define PHP_TARANTOOL_API __attribute__ ((visibility("default")))
#else
#  define PHP_TARANTOOL_API
#endif

#define TARANTOOL_TIMEOUT_SEC 10
#define TARANTOOL_TIMEOUT_USEC 0

#ifdef ZTS
#include "TSRM.h"
#endif

#define SSTR_BEG(str) (str->c)
#define SSTR_END(str) (str->c + str->a)
#define SSTR_AWA(str) (str->a)
#define SSTR_LEN(str) (str->len)
#define SSTR_POS(str) (str->c + str->len)
#define SSTR_DIF(str, end) (end - str->c)

PHP_MINIT_FUNCTION(tarantool);
PHP_MSHUTDOWN_FUNCTION(tarantool);
PHP_MINFO_FUNCTION(tarantool);

PHP_METHOD(tarantool_class, __construct);
PHP_METHOD(tarantool_class, connect);
PHP_METHOD(tarantool_class, authenticate);
PHP_METHOD(tarantool_class, ping);
PHP_METHOD(tarantool_class, select);
PHP_METHOD(tarantool_class, insert);
PHP_METHOD(tarantool_class, replace);
PHP_METHOD(tarantool_class, call);
PHP_METHOD(tarantool_class, delete);
PHP_METHOD(tarantool_class, update);

ZEND_BEGIN_MODULE_GLOBALS(tarantool)
	long sync_counter;
ZEND_END_MODULE_GLOBALS(tarantool)

ZEND_EXTERN_MODULE_GLOBALS(tarantool)

#ifdef ZTS
#  define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#  define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#endif  /* PHP_TARANTOOL_H */
