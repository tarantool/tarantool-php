#ifndef   PHP_TARANTOOL_H
#define   PHP_TARANTOOL_H

typedef unsigned long ulong;
typedef unsigned int  uint;
typedef enum {
	false = 0,
	true  = 1,
} bool;


#include <sys/socket.h>
#include <netinet/tcp.h>

#include <php.h>
#include <php_ini.h>
#include <php_network.h>
#include <zend_API.h>
#include <zend_compile.h>
#include <zend_exceptions.h>
#include <zend_modules.h>

#include <ext/standard/php_smart_str.h>
#include <ext/standard/sha1.h>
#include <ext/standard/base64.h>
#include <ext/standard/info.h>

#include "php_tarantool.h"

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

#include "php_tp_const.h"
#include "php_msgpack.h"
#include "php_tp.h"

typedef struct tarantool_object {
	zend_object zo;
	char *host;
	int port;
	php_stream *stream;
	smart_str *value;
	bool auth;
	char *greeting;
	char *salt;
} tarantool_object;

zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

zend_module_entry tarantool_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"tarantool",
	tarantool_module_functions,
	PHP_MINIT(tarantool),
	PHP_MSHUTDOWN(tarantool),
	NULL,
	NULL,
	PHP_MINFO(tarantool),
#if ZEND_MODULE_API_NO >= 20010901
	"1.0",
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif

static bool tarantool_stream_open(tarantool_object *obj TSRMLS_DC) {
	char *dest_addr = NULL;
	size_t dest_addr_len = spprintf(&dest_addr, 0, "tcp://%s:%d", obj->host, obj->port);
	int options = ENFORCE_SAFE_MODE | REPORT_ERRORS;
	int flags = STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT;
	struct timeval timeout = {
		.tv_sec = TARANTOOL_TIMEOUT_SEC,
		.tv_usec = TARANTOOL_TIMEOUT_USEC,
	};
	char *errstr = NULL;
	int errcode = 0;

	php_stream *stream = php_stream_xport_create(dest_addr, dest_addr_len,
						     options, flags,
						     NULL, &timeout, NULL,
						     &errstr, &errcode);
	efree(dest_addr);
	if (errcode || !stream) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
					0 TSRMLS_CC, "failed to connect. code %d: %s",
					errcode, errstr);
		goto process_error;
	}
	if (errstr) efree(errstr);
	int socketd = ((php_netstream_data_t*)stream->abstract)->socket;
	flags = 1;
	int result = setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags, sizeof(int));
	if (result) {
		char errbuf[64];
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
					0 TSRMLS_CC, "failed to connect. code %d: %s",
					errcode, errstr);
		goto process_error;
	}
	obj->stream = stream;
	return true;

process_error:	
	if (errstr) efree(errstr);
	if (stream) php_stream_close(stream);
}

static bool tarantool_stream_send(tarantool_object *obj) {
	if (php_stream_write(obj->stream,
			obj->value->c,
			obj->value->len)) {
		return false;
	}
	php_stream_flush(obj->stream);
	obj->value->len = 0;
}

/*
 * Legacy rtsisyk code:
 * php_stream_read made right
 * See https://bugs.launchpad.net/tarantool/+bug/1182474
 */
static size_t tarantool_stream_read(tarantool_object *obj, char *buf, size_t size) {
	size_t total_size = 0;
	while (total_size < size) {
		size_t read_size = php_stream_read(obj->stream, obj->value->c + total_size, size - total_size);
		assert(read_size <= size - total_size);
		if (read_size == 0)
			return total_size;
		total_size += read_size;
	}
}

static void tarantool_stream_close(tarantool_object *obj) {
	if (obj->stream) php_stream_close(obj->stream);
}

static void tarantool_free(tarantool_object *obj) {
	if (!obj) return;
	tarantool_stream_close(obj);
	smart_str_free(obj->value);
	efree(obj);
}

static zend_object_value tarantool_create(zend_class_entry *entry TSRMLS_DC) {
	zend_object_value new_value;

	tarantool_object *obj = (tarantool_object *)ecalloc(sizeof(tarantool_object), 1);
	zend_object_std_init(&obj->zo, entry TSRMLS_CC);
	new_value.handle = zend_objects_store_put(obj,
			(zend_objects_store_dtor_t )zend_objects_destroy_object,
			(zend_objects_free_object_storage_t )tarantool_free,
			NULL TSRMLS_CC);
	new_value.handlers = zend_get_std_object_handlers();
	return new_value;
}


const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
/*	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update_fields, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, admin, NULL, ZEND_ACC_PUBLIC)*/
	{NULL, NULL, NULL}
};

zend_class_entry *tarantool_class_ptr;

PHP_MINIT_FUNCTION(tarantool) {
	zend_class_entry tarantool_class;
	INIT_CLASS_ENTRY(tarantool_class, "Tarantool", tarantool_class_methods);
	tarantool_class.create_object = tarantool_create;
	tarantool_class_ptr = zend_register_internal_class(&tarantool_class TSRMLS_CC);
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(tarantool) {
	return SUCCESS;
}

PHP_MINFO_FUNCTION(tarantool) {
	php_info_print_table_start();
	php_info_print_table_header(2, "Tarantool support", "enabled");
	php_info_print_table_row(2, "Extension version", PHP_TARANTOOL_VERSION);
	php_info_print_table_end();
}

PHP_METHOD(tarantool_class, __construct) {
	/*
	 * parse method's parameters
	 */

	zval *id = NULL;
	char *host = NULL;
	int host_len = 0;
	long port = 0;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
					 getThis(),
					 "O|sl",
					 &id,
					 tarantool_class_ptr,
					 &host,
					 &host_len,
					 &port) == FAILURE) {
		return;
	}

	/*
	 * validate parameters
	 */

	if (host == NULL)
		host = "localhost";
	if (port < 0 || port >= 65536) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C), 0 TSRMLS_CC,
								"invalid primary port value: %li", port);
		return;
	} else if (port == 0) {
		port = 33013;
	}

	/* initialzie object structure */
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(id TSRMLS_CC);
	object->host = estrdup(host);
	object->port = port;
	object->stream = NULL;
	object->value = (smart_str *)ecalloc(sizeof(smart_str), 1);
	object->auth = false;
	object->greeting = emalloc(sizeof(char)*64);
	object->salt = NULL;
	smart_str_ensure(object->value, 128);
	return;
}

PHP_METHOD(tarantool_class, connect) {
	zval *id;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
					 getThis(),
					 "O|b",
					 &id,
					 tarantool_class_ptr) == FAILURE) {
		return;
	}
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(id TSRMLS_CC);
	
	tarantool_stream_open(object);
	tarantool_stream_read(object, object->greeting, 128);
	object->salt = object->greeting + 64;
	RETURN_TRUE;
}

static inline void
xor(unsigned char *to, unsigned const char *left,
	unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

static inline void
scramble_prepare(void *out, void * const salt, void * const password, size_t password_len) {
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	PHP_SHA1_CTX ctx;
	
	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, password, password_len);
	PHP_SHA1Final(hash1, &ctx);

	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, hash1, SCRAMBLE_SIZE);
	PHP_SHA1Final(hash2, &ctx);

	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	PHP_SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	PHP_SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

PHP_METHOD(tarantool_class, authenticate) {
	zval *id;
	char *login;
	size_t login_len;
	char *passw;
	size_t passw_len;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
			getThis(), "Oss", &id, tarantool_class_ptr,
			&login, &login_len, &passw, &passw_len)) {
		return;
	}
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(id TSRMLS_CC);
	
	char *salt; size_t salt_len;
	salt = php_base64_decode(object->salt, SALT64_SIZE, (int *)&salt_len);
	assert(salt); /* TODO: REWRITE CHECK */
	char scramble[SCRAMBLE_SIZE];
	scramble_prepare(scramble, salt, passw, passw_len);
	
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_auth(object->value, sync, login, login_len, scramble);
	tarantool_stream_send(object);
	tarantool_stream_read(object, object->value->c, 5);
	php_mp_check(object->value->c, 5);
	size_t body_size = php_mp_unpack_package_size(object->value->c);
	smart_str_ensure(object->value, body_size);
	tarantool_stream_read(object, object->value->c, body_size);
	object->value->len = body_size;
	
	zval *header, *body;
	char *pos = object->value->c;
	php_mp_check(pos, body_size);
	pos = php_mp_unpack(&header, &pos);
	php_mp_check(pos, body_size - (pos - object->value->c));
	pos = php_mp_unpack(&body, &pos);

	HashTable *hash = HASH_OF(header);	
	zval **val = NULL;
	if (zend_hash_index_find(hash, TNT_SYNC, (void **)val) == SUCCESS)
		assert(Z_LVAL_PP(val) == sync);
	
	if (zend_hash_index_find(hash, TNT_CODE, (void **)val) == SUCCESS) {
		if (Z_LVAL_PP(val) == TNT_OK)
			return;
		HashTable *hash = HASH_OF(body);
		zval **errstr;
		unsigned long errcode = Z_LVAL_PP(val) & (1 << 15 - 1);
		if (zend_hash_index_find(hash, TNT_ERROR, (void **)errstr) == FAILURE) {
			ALLOC_INIT_ZVAL(*errstr);
			ZVAL_STRING(*errstr, "empty", 1);
		}
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"failed to auth. code %d: %s", errcode,
				Z_STRVAL_PP(errstr), Z_STRLEN_PP(errstr));
		return;
	}
	zend_throw_exception_ex(
			zend_exception_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"failed to retrieve answer code");
	return;
}

PHP_METHOD(tarantool_class, ping) {
	zval *id;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
					 getThis(),
					 "O",
					 &id,
					 tarantool_class_ptr) == FAILURE) {
		return;
	}
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(id TSRMLS_CC);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(object->value, sync);
	tarantool_stream_send(object);
	tarantool_stream_read(object, object->value->c, 5);
	php_mp_check(object->value->c, 5);
	size_t body_size = php_mp_unpack_package_size(object->value->c);
	smart_str_ensure(object->value, body_size);
	tarantool_stream_read(object, object->value->c, body_size);
	object->value->len = body_size;
	
	zval *header, *body;
	char *pos = object->value->c;
	php_mp_check(pos, body_size);
	pos = php_mp_unpack(&header, &pos);
	php_mp_check(pos, body_size - (pos - object->value->c));
	pos = php_mp_unpack(&body, &pos);

	HashTable *hash = HASH_OF(header);
	zval **val = NULL;
	if (zend_hash_index_find(hash, TNT_SYNC, (void **)val) == SUCCESS)
		assert(Z_LVAL_PP(val) == sync);
	
	if (zend_hash_index_find(hash, TNT_CODE, (void **)val) == SUCCESS) {
		if (Z_LVAL_PP(val) == TNT_OK)
			return;
		HashTable *hash = HASH_OF(body);
		zval **errstr;
		unsigned long errcode = Z_LVAL_PP(val) & (1 << 15 - 1);
		if (zend_hash_index_find(hash, TNT_ERROR, (void **)errstr) == FAILURE) {
			ALLOC_INIT_ZVAL(*errstr);
			ZVAL_STRING(*errstr, "empty", 1);
		}
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"failed to auth. code %d: %s", errcode,
				Z_STRVAL_PP(errstr), Z_STRLEN_PP(errstr));
		return;
	}
	zend_throw_exception_ex(
			zend_exception_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"failed to retrieve answer code");
	return;

}
/*
 * PHP_METHOD(tarantool_class, select)
 * {
 * 	zval *id;
 * 	long space_no = 0;
 *	long index_no = 1;
 *	zval *keys_list = NULL;
 *	long limit = -1;
 *	long offset = 0;
 *	
 * }
 */

#endif /* PHP_TARANTOOL_H*/
