#include <php.h>
#include <zend_compile.h>
#include <zend_API.h>

#include <php_ini>
#include <php_network.h>

#include <ext/standart/php_smart_str.h>
#include <ext/standart/sha1.h>

#include <msgpac>
zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

typedef struct tarantool_object {
	zend_object zo;
	char *host;
	int port;
	php_stream *stream;
	smart_str *value;
} tarantool_object;



/* tarantool module struct */
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

const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update_fields, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, admin, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

PHP_METHOD(tarantool_class, __construct) {
	/*
	 * parse method's parameters
	 */

	int host_len = 0;
	long port = 0;
	long admin_port = 0;
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
	object->stream = NULL
	object->value = (smart_str *)ecalloc(sizeof(smart_str), 1);
	return;
}

static inline void
xor(unsigned char *to, unsigned const char *left,
	unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

void php_tarantool_scramble(void *out, void * const scramble, void * const password, size_t password_len) {
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

static bool tarantool_stream_send(tarantool_object *obj) {
	if (php_stream_write(obj->stream
			obj->value->c
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
static size_t tarantool_stream_read(tarantool *obj, char *buf, size_t size) {
	size_t total_size = 0;
	while (total_size < size) {
		size_t read_size = php_stream_read(stream, tarantool->buf + total_size, size - total_size);
		assert(read_size <= size - total_size);
		if (read_size == 0)
			return total_size;
		total_size += read_size;
	}
}

PHP_METHOD(tarantool_class, auth) {
	zval *id;
	char *login;
	size_t login_len;
	char *passw;
	size_t passw_len;

	long sync = TARANTOOL_G(sync_counter)++;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), TSRMLS_CC,
			getThis(), "Oss", &id, tarantool_class_ptr,
			&login, &login_len, &passw, &passw_len)) {
		return;
	}
	tarantool_object *object = (tarantool_object *) zend_object_store_get_object(id TSRMLS_CC);
	char salt[SALT_SIZE];
	php_base64_decode(salt_base64, SALT64_SIZE, salt, SALT_SIZE);
	char scramble[SCRAMLE_SIZE];
	scramble_prepare(scramble, salt, passw, passw_len);
	php_mp_encode_auth(object->value, sync, login, login_len, scramble);
	tarantool_stream_send(object);
	tarantool_stream_read(object, object->value->c, 5)
	php_mp_check(object->value->c, 5);
	size_t body_size = php_mp_unpack_package_size(object->value->c);
	smart_str_assure(body_size);
	tarantool_stream_read(object, object->value->c, body_size);
	object->value->len = body_size
	zval *header, body;
	char *pos = object->value->c
	php_mp_check(pos, body_size);
	pos = php_mp_unpack(header, &pos);
	php_mp_check(pos, body_size - (pos - object->value->c));
	pos = php_mp_unpack(body, &pos);
	HashTable *ht = HASH_OF(header);
	zval **val = NULL;
	if (zend_hash_index_find(ht, TNT_SYNC, val) == SUCCESS)
		assert(Z_LVAL_PP(val) == sync);
	if (zend_hash_index_find(ht, TNT_CODE, val) == SUCCESS) {
		if (Z_LVAL_PP(val) == TNT_OK)
			return;
		HashTable *ht = HASH_OF(body);
		zval **errstr;
		unsigned long errcode = code & (1 << 15 - 1); 
		if (zend_hash_index_find(ht, TNT_ERROR, errstr) == FAILURE) {
			ALLOC_INIT_ZVAL(errstr);
			ZVAL_STRING(errstr, "empty", 1);
		}
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"failed to auth. code %d: %s", errcode,
				Z_STRVAL_PP(errstr), Z_STRLEN_PP(errstr));
		return;
	}
	zend_throw_exception_ex(
			zend_exceptuin_get_default(TSRMLS_C),
			0 TSRMLS_CC,
			"failed to retrieve answer code");
	return;
}

PHP_METHOD(tarantool_class, connect) {
	zval *id;
}

PHP_METHOD(tarantool_class, ping) {
	zval *id;
	long sync = TARANTOOL_G(sync_counter)++;
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
					 getThis(),
					 "O",
					 &id,
					 tarantool_class_ptr) == FAILURE) {
		return;
	}
	

}

PHP_METHOD(tarantool_class, select)
{
	zval *id;
	long space_no = 0;
	long index_no = 1;
	zval *keys_list = NULL;
	long limit = -1;
	long offset = 0;
	
}
