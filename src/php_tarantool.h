#ifndef    PHP_TARANTOOL_H
#define    PHP_TARANTOOL_H


extern zend_module_entry tarantool_module_entry;
#define phpext_tarantool_ptr &tarantool_module_entry

#ifdef PHP_WIN32
#  define PHP_TARANTOOL_API __declspec(__dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define PHP_TARANTOOL_API __attribute__ ((visibility("default")))
#else
#  define PHP_TARANTOOL_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#define PHP_TARANTOOL_VERSION "0.1"
#define PHP_TARANTOOL_EXTNAME "tarantool"

ZEND_BEGIN_MODULE_GLOBALS(tarantool)
long sync_counter;
/*	        ...               */
ZEND_END_MODULE_GLOBALS(tarantool)

#ifdef ZTS
#  define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#  define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#endif  /* PHP_TARANTOOL_H */
