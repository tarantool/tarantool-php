/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2008 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Alexandre Kalendarev akalend@mail.ru                         |
  | Copyright (c) 2011                                                   |
  +----------------------------------------------------------------------+
*/
#ifndef PHP_TARANTOOL_H
#define PHP_TARANTOOL_H

#ifdef ZTS
#include "TSRM.h"
#endif

/*============================================================================*
 * Constants
 *============================================================================*/

#define TARANTOOL_EXTENSION_VERSION "1.0"

/*----------------------------------------------------------------------------*
 * tbuf constants
 *----------------------------------------------------------------------------*/

enum {
	IO_BUF_CAPACITY_MIN    = 128,
	IO_BUF_CAPACITY_FACTOR = 2,
};

/*----------------------------------------------------------------------------*
 * Connections constants
 *----------------------------------------------------------------------------*/

enum {
	TARANTOOL_TIMEOUT_SEC  = 5,
	TARANTOOL_TIMEOUT_USEC = 0,
	TARANTOOL_DEFAULT_PORT = 3013,
};

#define TARANTOOL_DEFAULT_HOST "localhost"

/*----------------------------------------------------------------------------*
 * Commands constants
 *----------------------------------------------------------------------------*/

/* update fields operation codes */
enum tp_op_value {
	TP_ASSIGN = '=',
	TP_DELETE = '#',
	TP_INSERT = '!',
	TP_ADD    = '+',
	TP_SUB    = '-',
	TP_AND    = '&',
	TP_XOR    = '^',
	TP_OR     = '|',
	TP_SPLICE = ':',
};

/*============================================================================*
 * Interaface decalaration
 *============================================================================*/

/*----------------------------------------------------------------------------*
 * Tarantool module interface
 *----------------------------------------------------------------------------*/

/* initialize module function */
PHP_MINIT_FUNCTION(tarantool);

/* shutdown module function */
PHP_MSHUTDOWN_FUNCTION(tarantool);

/* show information about this module */
PHP_MINFO_FUNCTION(tarantool);


/*----------------------------------------------------------------------------*
 * Tarantool class interface
 *----------------------------------------------------------------------------*/

/* class constructor */
PHP_METHOD(tarantool_class, __construct);

/* do select operation */
PHP_METHOD(tarantool_class, select);

/* do insert operation */
PHP_METHOD(tarantool_class, insert);

/* do update fields operation */
PHP_METHOD(tarantool_class, update_fields);

/* do delete operation */
PHP_METHOD(tarantool_class, delete);

/* call lua funtion operation */
PHP_METHOD(tarantool_class, call);

/* do admin command */
PHP_METHOD(tarantool_class, admin);


#ifdef ZTS
#define TARANTOOL_G(v) TSRMG(tarantool_globals_id, zend_tarantool_globals *, v)
#else
#define TARANTOOL_G(v) (tarantool_globals.v)
#endif

#endif /* PHP_TARANTOOL_H */
