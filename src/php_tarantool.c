#include <sys/socket.h>
#include <netinet/tcp.h>

#include <php.h>
#include <php_ini.h>
#include <php_network.h>
#include <zend_API.h>
#include <zend_compile.h>
#include <zend_exceptions.h>

#include <ext/standard/info.h>
#include <ext/standard/php_smart_str.h>
#include <ext/standard/base64.h>
#include <ext/standard/sha1.h>

#include "php_tarantool.h"

#include "php_tp_const.h"
#include "php_msgpack.h"
#include "php_tp.h"

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct tarantool_object {
	zend_object zo;
	char *host;
	int port;
	php_stream *stream;
	smart_str *value;
	char auth;
	char *greeting;
	char *salt;
} tarantool_object;

zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
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

static int tarantool_stream_open(tarantool_object *obj TSRMLS_DC) {
	char *dest_addr = NULL;
	size_t dest_addr_len = spprintf(&dest_addr, 0, "tcp://%s:%d",
					obj->host, obj->port);
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
	int socketd = ((php_netstream_data_t* )stream->abstract)->socket;
	flags = 1;
	int result = setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY,
			        (char *) &flags, sizeof(int));
	if (result) {
		char errbuf[64];
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
					0 TSRMLS_CC, "failed to connect. code %d: %s",
					errcode, errstr);
		goto process_error;
	}
	obj->stream = stream;
	return 0;

process_error:
	if (errstr) efree(errstr);
	if (stream) php_stream_close(stream);
}

#include <stdio.h>

static int
tarantool_stream_send(tarantool_object *obj TSRMLS_DC) {
	int i = 0;
	TSRMLS_FETCH();

	printf("Sent:");
	for (i = 0; i < SSTR_LEN(obj->value); ++i) {
		printf("\\x%02x", (unsigned char)SSTR_BEG(obj->value)[i]);
	} printf("\n");

	if (php_stream_write(obj->stream,
			SSTR_BEG(obj->value),
			SSTR_LEN(obj->value)) != SSTR_LEN(obj->value)) {
		return FAILURE;
	}
	if (php_stream_flush(obj->stream)) {
		return FAILURE;
	}
	SSTR_LEN(obj->value) = 0;
	smart_str_nullify(obj->value);
	return SUCCESS;
}

/*
 * Legacy rtsisyk code:
 * php_stream_read made right
 * See https://bugs.launchpad.net/tarantool/+bug/1182474
 */
static size_t tarantool_stream_read(tarantool_object *obj,
				    char *buf, size_t size TSRMLS_DC) {
	size_t total_size = 0;
	size_t read_size = 0;
	int i = 0;
	TSRMLS_FETCH();

	while (total_size < size) {
		size_t read_size = php_stream_read(obj->stream,
				buf + total_size,
				size - total_size);
		assert(read_size + total_size <= size);
		if (read_size == 0)
			break;
		total_size += read_size;
	}
	return total_size;
}

static void tarantool_stream_close(tarantool_object *obj TSRMLS_DC) {
	if (obj->stream) php_stream_close(obj->stream);
}

static void tarantool_free(tarantool_object *obj TSRMLS_DC) {
	if (!obj) return;
	tarantool_stream_close(obj TSRMLS_CC);
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

static inline void
xor(unsigned char *to, unsigned const char *left,
    unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

static int64_t tarantool_step_recv(
		tarantool_object *obj,
		unsigned long sync,
		zval **header,
		zval **body TSRMLS_DC) {
	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5 TSRMLS_CC) != 5)
		goto error_read;
	if (php_mp_check(pack_len, 5))
		goto error_check;
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_str_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_POS(obj->value),
				body_size TSRMLS_CC) != body_size)
		goto error_read;
	SSTR_LEN(obj->value) += body_size;

	char *pos = SSTR_BEG(obj->value);
	if (php_mp_check(pos, body_size))
		goto error_check;
	php_mp_unpack(header, &pos);
	if (php_mp_check(pos, body_size))
		goto error_check;
	php_mp_unpack(body, &pos);

	HashTable *hash = HASH_OF(*header);
	zval **val = NULL;

	if (zend_hash_index_find(hash, TNT_SYNC, (void **)&val) == SUCCESS)
		assert(Z_LVAL_PP(val) == sync);
	val = NULL;
	if (zend_hash_index_find(hash, TNT_CODE, (void **)&val) == SUCCESS) {
		if (Z_LVAL_PP(val) == TNT_OK) {
			SSTR_LEN(obj->value) = 0;
			smart_str_nullify(obj->value);
			return SUCCESS;
		}
		HashTable *hash = HASH_OF(*body);
		zval **errstr = NULL;
		long errcode = Z_LVAL_PP(val) & (1 << 15 - 1);
		if (zend_hash_index_find(hash, TNT_ERROR, (void **)&errstr) == FAILURE) {
			errstr = (zval **)ecalloc(sizeof(zval **), 1);
			ALLOC_INIT_ZVAL(*errstr);
			ZVAL_STRING(*errstr, "empty", 1);
		}
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"Query Error %d: %s", errcode,
				Z_STRVAL_PP(errstr), Z_STRLEN_PP(errstr));
		goto error;
	}
	zend_throw_exception_ex(
			zend_exception_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"failed to retrieve answer code");
	goto error;
error_read:
	zend_throw_exception_ex(
			zend_exception_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"can't read query from server");
	goto error;
error_check:
	zend_throw_exception_ex(
			zend_exception_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"failed verifying msgpack");
error:
	SSTR_LEN(obj->value) = 0;
	smart_str_nullify(obj->value);
	return FAILURE;
}


const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
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
	tarantool_object *obj = (tarantool_object *) \
				    zend_object_store_get_object(id TSRMLS_CC);
	obj->host = estrdup(host);
	obj->port = port;
	obj->stream = NULL;
	obj->value = (smart_str *)ecalloc(sizeof(smart_str), 1);
	obj->auth = 0;
	obj->greeting = (char *)ecalloc(sizeof(char), 64);
	obj->salt = NULL;
	smart_str_ensure(obj->value, 128);
	smart_str_0(obj->value);
	return;
}

PHP_METHOD(tarantool_class, connect) {
	zval *id;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
					 getThis(),
					 "O",
					 &id,
					 tarantool_class_ptr) == FAILURE) {
		return;
	}
	tarantool_object *object = (tarantool_object *) \
				   zend_object_store_get_object(id TSRMLS_CC);
	tarantool_stream_open(object);
	tarantool_stream_read(object, object->greeting, 128);
	object->salt = object->greeting + 64;
	printf("GREETING:\n%s",     object->greeting);
	printf("SALT:    \n%.*s\n", SALT64_SIZE, object->salt);
	RETURN_TRUE;
}

static inline void
scramble_prepare(void *out, void * const salt,
		 void * const password, size_t password_len) {
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
	PHP_SHA1Update(&ctx, salt, 32);
	PHP_SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	PHP_SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

PHP_METHOD(tarantool_class, authenticate) {
	zval *id;
	char *login;
	int login_len;
	char *passwd;
	int passwd_len;

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
			getThis(), "Oss", &id, tarantool_class_ptr,
			&login, &login_len, &passwd, &passwd_len)) {
		return;
	}
	tarantool_object *obj = (tarantool_object *) \
				   zend_object_store_get_object(id TSRMLS_CC);

	char *salt; int salt_len;
	salt = php_base64_decode(obj->salt, SALT64_SIZE, (int *)&salt_len);
	printf("SALT_LEN: %d\n", salt_len);
	salt_len = SCRAMBLE_SIZE;
	assert(salt); /* TODO: REWRITE CHECK */
	char scramble[SCRAMBLE_SIZE] = {0};

	scramble_prepare(scramble, salt, passwd, passwd_len);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_auth(obj->value, sync, login, login_len, scramble);
	tarantool_stream_send(obj);

	zval *header = NULL, *body = NULL;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE) {
		return;
	}
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
	tarantool_object *obj = (tarantool_object *) \
				   zend_object_store_get_object(id TSRMLS_CC);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(obj->value, sync);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE) {
		return;
	}
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
