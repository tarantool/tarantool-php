#ifndef    PHP_TARANTOOL_H
#define    PHP_TARANTOOL_H

#include <php.h>
#include <php_ini.h>
#include <zend_API.h>
#include <php_network.h>
#include <zend_compile.h>
#include <zend_exceptions.h>

#include <ext/standard/info.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if PHP_VERSION_ID >= 70000
#  include <ext/standard/php_smart_string.h>
#else
#  include <ext/standard/php_smart_str.h>
   typedef smart_str smart_string;
#  define smart_string_alloc4(...) smart_str_alloc4(__VA_ARGS__)
#  define smart_string_free_ex(...) smart_str_free_ex(__VA_ARGS__)
#endif

extern zend_module_entry tarantool_module_entry;
#define phpext_tarantool_ptr &tarantool_module_entry

#define PHP_TARANTOOL_VERSION "0.1.0"
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

typedef struct tarantool_schema tarantool_schema;

#define SSTR_BEG(str) (str->c)
#define SSTR_END(str) (str->c + str->a)
#define SSTR_AWA(str) (str->a)
#define SSTR_LEN(str) (str->len)
#define SSTR_POS(str) (str->c + str->len)
#define SSTR_DIF(str, end) (end - str->c)

PHP_MINIT_FUNCTION(tarantool);
PHP_RINIT_FUNCTION(tarantool);
PHP_MSHUTDOWN_FUNCTION(tarantool);
PHP_MINFO_FUNCTION(tarantool);

PHP_METHOD(Tarantool, __construct);
PHP_METHOD(Tarantool, connect);
PHP_METHOD(Tarantool, reconnect);
PHP_METHOD(Tarantool, close);
PHP_METHOD(Tarantool, authenticate);
PHP_METHOD(Tarantool, ping);
PHP_METHOD(Tarantool, select);
PHP_METHOD(Tarantool, insert);
PHP_METHOD(Tarantool, replace);
PHP_METHOD(Tarantool, call);
PHP_METHOD(Tarantool, eval);
PHP_METHOD(Tarantool, delete);
PHP_METHOD(Tarantool, update);
PHP_METHOD(Tarantool, upsert);
PHP_METHOD(Tarantool, flush_schema);

ZEND_BEGIN_MODULE_GLOBALS(tarantool)
	zend_bool persistent;
	long sync_counter;
	long retry_count;
	double retry_sleep;
	double timeout;
	double request_timeout;
ZEND_END_MODULE_GLOBALS(tarantool)

ZEND_EXTERN_MODULE_GLOBALS(tarantool);

typedef struct tarantool_object {
	zend_object   zo;

	struct tarantool_connection  {
		char             *host;
		int               port;
		char             *login;
		char             *passwd;
		php_stream       *stream;
		tarantool_schema *schema;
		smart_string     *value;
		struct tp        *tps;
		char             *greeting;
		char             *salt;
		/* Only for persistent connections */
		char             *orig_login;
		char             *suffix;
		int               suffix_len;
		char             *persistent_id;
	}            *obj;

	zend_bool     is_persistent;
} tarantool_object;

typedef struct tarantool_connection tarantool_connection;

PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_ce(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_exception(void);
PHP_TARANTOOL_API zend_class_entry *php_tarantool_get_exception_base(int root TSRMLS_DC);

#ifdef ZTS
#  define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#  define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#define THROW_EXC(...) zend_throw_exception_ex(					\
	zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC, __VA_ARGS__)

#endif  /* PHP_TARANTOOL_H */
