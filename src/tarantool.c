#include <sys/socket.h>
#include <netinet/tcp.h>

#include <sys/time.h>
#include <time.h>

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

#include "tarantool_msgpack.h"
#include "tarantool_proto.h"
#include "tarantool_manager.h"

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define TARANTOOL_PARSE_PARAMS(ID, FORMAT, ...) zval *ID;          \
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,\
				getThis(), "O" FORMAT,             \
				&ID, tarantool_class_ptr,          \
				__VA_ARGS__) == FAILURE)           \
		RETURN_FALSE;

#define TARANTOOL_FETCH_OBJECT(NAME, ID)                           \
	tarantool_object *NAME = (tarantool_object *)              \
			zend_object_store_get_object(ID TSRMLS_CC)

#define TARANTOOL_CONNECT_ON_DEMAND(CON, ID)                                   \
	if (!CON->stream)                                                      \
		if (__tarantool_connect(CON, ID TSRMLS_CC) == FAILURE)         \
			RETURN_FALSE;                                          \
	if (CON->stream && php_stream_eof(CON->stream) != 0)                   \
		if (__tarantool_reconnect(CON, ID TSRMLS_CC) == FAILURE)       \
			RETURN_FALSE;                                          \

#define THROW_EXC(...) zend_throw_exception_ex( \
	zend_exception_get_default(TSRMLS_C),   \
	0 TSRMLS_CC, __VA_ARGS__)

#define TARANTOOL_RETURN_DATA(HT, HEAD, BODY)                      \
	HashTable *ht_ ## HT = HASH_OF(HT);                        \
	zval **answer = NULL;                                      \
	if (zend_hash_index_find(ht_ ## HT, TNT_DATA,              \
			(void **)&answer) == FAILURE || !answer) { \
		THROW_EXC("No field DATA in body");                \
		zval_ptr_dtor(&HEAD);                              \
		zval_ptr_dtor(&BODY);                              \
		RETURN_FALSE;                                      \
	}                                                          \
	RETVAL_ZVAL(*answer, 1, 0);                                \
	zval_ptr_dtor(&HEAD);                                      \
	zval_ptr_dtor(&BODY);                                      \
	return;

#define RLCI(NAME)                                                 \
	REGISTER_LONG_CONSTANT("TARANTOOL_ITER_" # NAME,           \
		ITERATOR_ ## NAME, CONST_CS | CONST_PERSISTENT)

void schema_flush(tarantool_object *obj TSRMLS_DC);

zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flushSchema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, eval, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

zend_module_entry tarantool_module_entry = {
	STANDARD_MODULE_HEADER,
	"tarantool",
	tarantool_module_functions,
	PHP_MINIT(tarantool),
	PHP_MSHUTDOWN(tarantool),
	PHP_RINIT(tarantool),
	NULL,
	PHP_MINFO(tarantool),
	"1.0",
	STANDARD_MODULE_PROPERTIES
};

PHP_INI_BEGIN()
	PHP_INI_ENTRY("tarantool.timeout"     , "10.0", PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("tarantool.retry_count" , "1"   , PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("tarantool.retry_sleep" , "0.1" , PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("tarantool.persistent"  , "1"   , PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("tarantool.deauthorize" , "0"   , PHP_INI_ALL, NULL)
	PHP_INI_ENTRY("tarantool.con_per_host", "5"   , PHP_INI_ALL, NULL)
PHP_INI_END()

#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif

static int tarantool_stream_open(tarantool_object *obj, int count TSRMLS_DC) {
	char *dest_addr = NULL;
	size_t dest_addr_len = spprintf(&dest_addr, 0, "tcp://%s:%d",
					obj->host, obj->port);
	int options = ENFORCE_SAFE_MODE | REPORT_ERRORS;
	if (TARANTOOL_G(persistent)) {
		options |= STREAM_OPEN_PERSISTENT;
	}
	int flags = STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT;
	long time = floor(INI_FLT("tarantool.timeout"));
	struct timeval timeout = {
		.tv_sec = time,
		.tv_usec = (INI_FLT("tarantool.timeout") - time) * pow(10, 6),
	};
	char *errstr = NULL;
	int errcode = 0;

	php_stream *stream = php_stream_xport_create(dest_addr, dest_addr_len,
						     options, flags,
						     obj->persistent_id, &timeout,
						     NULL, &errstr, &errcode);
	efree(dest_addr);
	if (errcode || !stream) {
		if (!count)
			THROW_EXC("Failed to connect. Code %d: %s", errcode, errstr);
		goto error;
	}
	int socketd = ((php_netstream_data_t* )stream->abstract)->socket;
	flags = 1;
	if (setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags, sizeof(int))) {
		char errbuf[128];
		if (!count)
			THROW_EXC("Failed to connect. Setsockopt error %s",
					strerror_r(errno, errbuf, sizeof(errbuf)));
		goto error;
	}
	obj->stream = stream;
	return SUCCESS;
error:
	if (count)
		php_error(E_NOTICE, "Connection failed. %d attempts left", count);
	if (errstr) efree(errstr);
	if (stream) php_stream_pclose(stream);
	return FAILURE;
}

#include <stdio.h>

static int
tarantool_stream_send(tarantool_object *obj TSRMLS_DC) {
	int i = 0;

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
	if (obj->stream) php_stream_pclose(obj->stream);
	obj->stream = NULL;
}

int __tarantool_connect(tarantool_object *obj, zval *id TSRMLS_DC) {
	int status = SUCCESS;
	long count = TARANTOOL_G(retry_count);
	long sec = TARANTOOL_G(retry_sleep);
	struct timespec sleep_time = {
		.tv_sec = sec,
		.tv_nsec = (TARANTOOL_G(retry_sleep) - sec) * pow(10, 9)
	};
	if (TARANTOOL_G(persistent) && pool_manager_find_apply(TARANTOOL_G(manager), obj) == 0) {
		php_stream_from_persistent_id(obj->persistent_id, &obj->stream TSRMLS_CC);
		if (obj->stream == NULL)
			goto retry;
		if (TARANTOOL_G(deauthorize))
			goto authorize;
		return status;
	}
	ALLOC_INIT_ZVAL(obj->schema_hash);
	array_init(obj->schema_hash);
retry:
	while (1) {
		if (TARANTOOL_G(persistent)) {
			if (obj->persistent_id) pefree(obj->persistent_id, 1);
			obj->persistent_id = tarantool_stream_persistentid(obj);
		}
		if (tarantool_stream_open(obj, count TSRMLS_CC) == SUCCESS) {
			if (tarantool_stream_read(obj, obj->greeting,
						GREETING_SIZE TSRMLS_CC) == GREETING_SIZE) {
				obj->salt = obj->greeting + SALT_PREFIX_SIZE;
				break;
			}
			if (count < 0)
				THROW_EXC("Can't read Greeting from server");
		}
		--count;
		if (count < 0)
			return FAILURE;
		nanosleep(&sleep_time, NULL);
	}
authorize:
	if (obj->login != NULL && obj->passwd != NULL)
		__tarantool_authenticate(obj);
	return status;
}

int __tarantool_reconnect(tarantool_object *obj, zval *id TSRMLS_DC) {
	tarantool_stream_close(obj TSRMLS_CC);
	return __tarantool_connect(obj, id TSRMLS_CC);
}

static void tarantool_free(tarantool_object *obj TSRMLS_DC) {
	if (!obj) return;
	if (TARANTOOL_G(deauthorize) && obj->stream) {
		pefree(obj->login, 1);
		obj->login  = pestrdup("guest", 1);
		obj->passwd = NULL;
		__tarantool_authenticate(obj);
	}
	if (TARANTOOL_G(persistent)) {
		pool_manager_push_assure(TARANTOOL_G(manager), obj);
	}
	if (obj->host)     pefree(obj->host, 1);
	if (obj->login)    pefree(obj->login, 1);
	if (obj->passwd)   efree(obj->passwd);
	if (!TARANTOOL_G(persistent)) {
		if (obj->greeting) pefree(obj->greeting, 1);
		tarantool_stream_close(obj TSRMLS_CC);
		//schema_flush(obj TSRMLS_CC);
		if (obj->schema_hash) zval_ptr_dtor(&obj->schema_hash);
	}
	smart_str_free(obj->value);
	pefree(obj->value, 1);
	pefree(obj, 1);
}

static zend_object_value tarantool_create(zend_class_entry *entry TSRMLS_DC) {
	zend_object_value new_value;

	tarantool_object *obj = (tarantool_object *)pecalloc(sizeof(tarantool_object), 1, 1);
	zend_object_std_init(&obj->zo, entry TSRMLS_CC);
	new_value.handle = zend_objects_store_put(obj,
			(zend_objects_store_dtor_t )zend_objects_destroy_object,
			(zend_objects_free_object_storage_t )tarantool_free,
			NULL TSRMLS_CC);
	new_value.handlers = zend_get_std_object_handlers();
	return new_value;
}

static int64_t tarantool_step_recv(
		tarantool_object *obj,
		unsigned long sync,
		zval **header,
		zval **body TSRMLS_DC) {
	char pack_len[5] = {0, 0, 0, 0, 0};
	*header = NULL;
	*body = NULL;
	if (tarantool_stream_read(obj, pack_len, 5 TSRMLS_CC) != 5) {
		THROW_EXC("Can't read query from server");
		goto error;
	}
	if (php_mp_check(pack_len, 5)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_str_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_POS(obj->value),
				body_size TSRMLS_CC) != body_size) {
		THROW_EXC("Can't read query from server");
		goto error;
	}
	SSTR_LEN(obj->value) += body_size;

	char *pos = SSTR_BEG(obj->value);
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	if (php_mp_unpack(header, &pos) == FAILURE) {
		goto error;
	}
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	if (php_mp_unpack(body, &pos) == FAILURE) {
		goto error;
	}

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
		long errcode = Z_LVAL_PP(val) & ((1 << 15) - 1 );
		if (zend_hash_index_find(hash, TNT_ERROR, (void **)&errstr) == FAILURE) {
			ALLOC_INIT_ZVAL(*errstr);
			ZVAL_STRING(*errstr, "empty", 1);
		}
		THROW_EXC("Query error %d: %s", errcode, Z_STRVAL_PP(errstr),
				Z_STRLEN_PP(errstr));
		goto error;
	}
	THROW_EXC("Failed to retrieve answer code");
error:
	if (*header) zval_ptr_dtor(header);
	if (*body) zval_ptr_dtor(body);
	SSTR_LEN(obj->value) = 0;
	smart_str_nullify(obj->value);
	return FAILURE;
}


const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flushSchema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

/* ####################### HELPERS ####################### */

static void
xor(unsigned char *to, unsigned const char *left,
	unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

void
scramble_prepare(void *out, const void *salt,
		 const void *password, int password_len)
{
	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	PHP_SHA1_CTX ctx;

	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, (const unsigned char *)password, password_len);
	PHP_SHA1Final(hash1, &ctx);

	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, (const unsigned char *)hash1, SCRAMBLE_SIZE);
	PHP_SHA1Final(hash2, &ctx);

	PHP_SHA1Init(&ctx);
	PHP_SHA1Update(&ctx, (const unsigned char *)salt,  SCRAMBLE_SIZE);
	PHP_SHA1Update(&ctx, (const unsigned char *)hash2, SCRAMBLE_SIZE);
	PHP_SHA1Final((unsigned char *)out, &ctx);

	xor((unsigned char *)out, (const unsigned char *)hash1, (const unsigned char *)out, SCRAMBLE_SIZE);
}

zval *pack_key(zval *args, char select) {
	if (args && Z_TYPE_P(args) == IS_ARRAY)
		return args;
	zval *arr = NULL;
	ALLOC_INIT_ZVAL(arr);
	if (select && (!args || Z_TYPE_P(args) == IS_NULL)) {
		array_init_size(arr, 0);
		return arr;
	}
	array_init_size(arr, 1);
	add_next_index_zval(arr, args);
	return arr;
}

zval *tarantool_update_verify_op(zval *op, long position TSRMLS_DC) {
	if (Z_TYPE_P(op) != IS_ARRAY || !php_mp_is_hash(op)) {
		THROW_EXC("Op must be MAP at pos %d", position);
		return NULL;
	}
	HashTable *ht = HASH_OF(op);
	size_t n = zend_hash_num_elements(ht);
	zval *arr; ALLOC_INIT_ZVAL(arr); array_init_size(arr, n);
	zval **opstr, **oppos;
	if (zend_hash_find(ht, "op", 3, (void **)&opstr) == FAILURE ||
			!opstr || Z_TYPE_PP(opstr) != IS_STRING ||
			Z_STRLEN_PP(opstr) != 1) {
		THROW_EXC("Field OP must be provided and must be STRING with "
				"length=1 at position %d", position);
		return NULL;
	}
	if (zend_hash_find(ht, "field", 6, (void **)&oppos) == FAILURE ||
			!oppos || Z_TYPE_PP(oppos) != IS_LONG) {
		THROW_EXC("Field FIELD must be provided and must be LONG at "
				"position %d", position);
		return NULL;

	}
	zval **oparg, **splice_len, **splice_val;
	switch(Z_STRVAL_PP(opstr)[0]) {
	case ':':
		if (n != 5) {
			THROW_EXC("Five fields must be provided for splice"
					" at position %d", position);
			return NULL;
		}
		if (zend_hash_find(ht, "offset", 7,
				(void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			THROW_EXC("Field OFFSET must be provided and must be LONG for "
					"splice at position %d", position);
			return NULL;
		}
		if (zend_hash_find(ht, "length", 7, (void **)&splice_len) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_len) != IS_LONG) {
			THROW_EXC("Field LENGTH must be provided and must be LONG for "
					"splice at position %d", position);
			return NULL;
		}
		if (zend_hash_find(ht, "list", 5, (void **)&splice_val) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_val) != IS_STRING) {
			THROW_EXC("Field LIST must be provided and must be STRING for "
					"splice at position %d", position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		add_next_index_long(arr, Z_LVAL_PP(oparg));
		add_next_index_long(arr, Z_LVAL_PP(splice_len));
		add_next_index_stringl(arr, Z_STRVAL_PP(splice_val),
				Z_STRLEN_PP(splice_val), 1);
		break;
	case '+':
	case '-':
	case '&':
	case '|':
	case '^':
	case '#':
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
					"position %d", Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			THROW_EXC("Field ARG must be provided and must be LONG for "
					"'%s' at position %d", Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		add_next_index_long(arr, Z_LVAL_PP(oparg));
		break;
	case '=':
	case '!':
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
					"position %d", Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || !PHP_MP_SERIALIZABLE_PP(oparg)) {
			THROW_EXC("Field ARG must be provided and must be SERIALIZABLE for "
					"'%s' at position %d", Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		SEPARATE_ZVAL_TO_MAKE_IS_REF(oparg);
		add_next_index_zval(arr, *oparg);
		break;
	default:
		THROW_EXC("Unknown operation '%s' at position %d",
				Z_STRVAL_PP(opstr), position);
		return NULL;

	}
	return arr;
}

zval *tarantool_update_verify_args(zval *args TSRMLS_DC) {
	if (Z_TYPE_P(args) != IS_ARRAY || php_mp_is_hash(args)) {
		THROW_EXC("Provided value for update OPS must be Array");
		return NULL;
	}
	HashTable *ht = HASH_OF(args);
	size_t n = zend_hash_num_elements(ht);

	zval **op, *arr;
	ALLOC_INIT_ZVAL(arr); array_init_size(arr, n);
	size_t key_index = 0;
	for(; key_index < n; ++key_index) {
		int status = zend_hash_index_find(ht, key_index,
				                  (void **)&op);
		if (status != SUCCESS || !op) {
			THROW_EXC("Internal Array Error");
			goto cleanup;
		}
		zval *op_arr = tarantool_update_verify_op(*op, key_index
				TSRMLS_CC);
		if (!op_arr)
			goto cleanup;
		if (add_next_index_zval(arr, op_arr) == FAILURE) {
			THROW_EXC("Internal Array Error");
			goto cleanup;
		}
	}
	return arr;
cleanup:
	//zval_ptr_dtor(&arr);
	return NULL;
}

void schema_flush(tarantool_object *obj TSRMLS_DC) {
	HashTable *ht = HASH_OF(obj->schema_hash);
	size_t n = zend_hash_num_elements(ht);
	assert(n % 2 == 0);
	ulong *keys = calloc(sizeof(ulong), n/2 + 1);

	char *key;
	uint key_len;
	int key_type;
	ulong key_index;
	zval **data;
	HashPosition pos;

	ssize_t cnt = 0;
	zend_hash_internal_pointer_reset_ex(ht, &pos);
	for (;; zend_hash_move_forward_ex(ht, &pos)) {
		key_type = zend_hash_get_current_key_ex(
			ht, &key, &key_len, &key_index, 0, &pos);
		if (key_type == HASH_KEY_NON_EXISTANT)
			break;
		int status = zend_hash_get_current_data_ex(ht,
				(void *)&data, &pos);
		if (status != SUCCESS || !data) {
			continue;
		}
		switch (key_type) {
		case HASH_KEY_IS_LONG:
			keys[cnt++];
			break;
		case HASH_KEY_IS_STRING:
			continue;
		default:
			/* TODO: THROW EXCEPTION */
			break;
		}

	}
	for (;cnt >= 0;--cnt)
		zend_hash_index_del(ht, keys[cnt]);
	free(keys);
	zend_hash_clean(ht);
}

int schema_add_space(tarantool_object *obj, uint32_t space_no,
		char *space_name, size_t space_len TSRMLS_DC) {
	zval *s_hash, *i_hash;
	ALLOC_INIT_ZVAL(s_hash); array_init(s_hash);
	ALLOC_INIT_ZVAL(i_hash); array_init(i_hash);
	add_next_index_long(s_hash, space_no);
	add_next_index_stringl(s_hash, space_name, space_len, 1);
	add_next_index_zval(s_hash, i_hash);

	add_index_zval(obj->schema_hash, space_no, s_hash);
	Z_ADDREF_P(s_hash);
	add_assoc_zval_ex(obj->schema_hash, space_name, space_len, s_hash);
}

int schema_add_index(tarantool_object *obj, uint32_t space_no,
		uint32_t index_no, char *index_name,
		size_t index_len TSRMLS_DC) {
	zval **s_hash, **i_hash;
	if (zend_hash_index_find(HASH_OF(obj->schema_hash), space_no,
				 (void **)&s_hash) == FAILURE || !s_hash) {
		return FAILURE;
	}
	if (zend_hash_index_find(HASH_OF(*s_hash), 2,
				 (void **)&i_hash) == FAILURE || !i_hash) {
		return FAILURE;
	}
	add_assoc_long_ex(*i_hash, index_name, index_len, index_no);
}

uint32_t schema_get_space(tarantool_object *obj,
		char *space_name, size_t space_len TSRMLS_DC) {
	HashTable *ht = HASH_OF(obj->schema_hash);
	zval **stuple, **sid;
	if (zend_hash_find(ht, space_name, space_len,
			   (void **)&stuple) == FAILURE || !stuple)
		return FAILURE;
	if (zend_hash_index_find(HASH_OF(*stuple), 0,
				 (void **)&sid) == FAILURE || !sid)
		return FAILURE;
	return Z_LVAL_PP(sid);
}

uint32_t schema_get_index(tarantool_object *obj, uint32_t space_no,
		char *index_name, size_t index_len TSRMLS_DC) {
	HashTable *ht = HASH_OF(obj->schema_hash);
	zval **stuple, **ituple, **iid;
	if (zend_hash_index_find(ht, space_no,
			   (void **)&stuple) == FAILURE || !stuple)
		return FAILURE;
	if (zend_hash_index_find(HASH_OF(*stuple), 2,
				(void **)&ituple) == FAILURE || !ituple)
		return FAILURE;
	if (zend_hash_find(HASH_OF(*ituple), index_name, index_len,
				(void **)&iid) == FAILURE || !iid)
		return FAILURE;
	return Z_LVAL_PP(iid);
}

int get_spaceno_by_name(tarantool_object *obj, zval *id, zval *name TSRMLS_DC) {
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Space ID must be String or Long");
		return FAILURE;
	}
	int32_t space_no = schema_get_space(obj, Z_STRVAL_P(name),
			Z_STRLEN_P(name) TSRMLS_CC);
	if (space_no != FAILURE)
		return space_no;

	zval *fname, *spaceno, *indexno, *key;
	ALLOC_INIT_ZVAL(key); array_init_size(key, 1);
	ALLOC_INIT_ZVAL(fname); ZVAL_STRING(fname, "select", 1);
	ALLOC_INIT_ZVAL(spaceno); ZVAL_LONG(spaceno, SPACE_SPACE);
	ALLOC_INIT_ZVAL(indexno); ZVAL_LONG(indexno, INDEX_SPACE_NAME);
	zval *retval; ALLOC_INIT_ZVAL(retval);
	add_next_index_zval(key, name);
	zval *args[] = {id, spaceno, key, indexno};
	call_user_function(NULL, &obj, fname, retval, 4, args TSRMLS_CC);
	if (!retval || (Z_TYPE_P(retval) == IS_BOOL && !Z_BVAL_P(retval)) ||
		       (Z_TYPE_P(retval) == IS_NULL)) {
		space_no = FAILURE;
		goto cleanup;
	}
	zval **tuple, **field;
	if (zend_hash_index_find(HASH_OF(retval), 0,
				(void **)&tuple) == FAILURE) {
		THROW_EXC("No space '%s' defined", Z_STRVAL_P(name));
		goto cleanup;
	}
	if (zend_hash_index_find(HASH_OF(*tuple), 0,
				(void **)&field) == FAILURE ||
			!field || Z_TYPE_PP(field) != IS_LONG) {
		THROW_EXC("Bad data came from server");
		space_no = FAILURE;
		goto cleanup;
	}
	if (schema_add_space(obj, Z_LVAL_PP(field), Z_STRVAL_P(name),
				Z_STRLEN_P(name) TSRMLS_CC) == FAILURE) {
		THROW_EXC("Internal Error");
		space_no = FAILURE;
		goto cleanup;
	}
	space_no = Z_LVAL_PP(field);
cleanup:
	Z_ADDREF_P(name);
	zval_ptr_dtor(&key);
	Z_DELREF_P(name);
	zval_ptr_dtor(&fname);
	zval_ptr_dtor(&spaceno);
	zval_ptr_dtor(&indexno);
	zval_ptr_dtor(&retval);
	return space_no;
}

int get_indexno_by_name(tarantool_object *obj, zval *id,
		int space_no, zval *name TSRMLS_DC) {
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Index ID must be String or Long");
		return FAILURE;
	}
	int32_t index_no = schema_get_index(obj, space_no,
			Z_STRVAL_P(name), Z_STRLEN_P(name) TSRMLS_CC);
	if (index_no != FAILURE)
		return index_no;

	zval *fname, *spaceno, *indexno, *key;
	ALLOC_INIT_ZVAL(key); array_init_size(key, 2);
	ALLOC_INIT_ZVAL(fname); ZVAL_STRING(fname, "select", 1);
	ALLOC_INIT_ZVAL(spaceno); ZVAL_LONG(spaceno, SPACE_INDEX);
	ALLOC_INIT_ZVAL(indexno); ZVAL_LONG(indexno, INDEX_INDEX_NAME);
	zval *retval; ALLOC_INIT_ZVAL(retval);
	add_next_index_long(key, space_no);
	add_next_index_zval(key, name);
	zval *args[] = {id, spaceno, key, indexno};
	call_user_function(NULL, &obj, fname, retval, 4, args TSRMLS_CC);
	if (!retval || (Z_TYPE_P(retval) == IS_BOOL && !Z_BVAL_P(retval)) ||
		       (Z_TYPE_P(retval) == IS_NULL)) {
		space_no = FAILURE;
		goto cleanup;
	}
	zval **tuple, **field;
	if (zend_hash_index_find(HASH_OF(retval), 0, (void **)&tuple) == FAILURE) {
		THROW_EXC("No index '%s' defined", Z_STRVAL_P(name));
		space_no = FAILURE;
		goto cleanup;
	}
	if (zend_hash_index_find(HASH_OF(*tuple), 1, (void **)&field) == FAILURE ||
			!tuple || Z_TYPE_PP(field) != IS_LONG) {
		THROW_EXC("Bad data came from server");
		space_no = FAILURE;
		goto cleanup;
	}
	if (schema_add_index(obj, space_no, Z_LVAL_PP(field), Z_STRVAL_P(name),
				Z_STRLEN_P(name) TSRMLS_CC) == FAILURE) {
		THROW_EXC("Internal Error");
		space_no = FAILURE;
		goto cleanup;
	}
	index_no = Z_LVAL_PP(field);
cleanup:
	Z_ADDREF_P(name);
	zval_ptr_dtor(&key);
	Z_DELREF_P(name);
	zval_ptr_dtor(&fname);
	zval_ptr_dtor(&retval);
	zval_ptr_dtor(&spaceno);
	zval_ptr_dtor(&indexno);
	return index_no;
}

/* ####################### METHODS ####################### */

zend_class_entry *tarantool_class_ptr;

PHP_RINIT_FUNCTION(tarantool) {
	return SUCCESS;
}

static void
php_tarantool_init_globals(zend_tarantool_globals *tarantool_globals) {
}

PHP_MINIT_FUNCTION(tarantool) {
	REGISTER_INI_ENTRIES();
	ZEND_INIT_MODULE_GLOBALS(tarantool, php_tarantool_init_globals, NULL);
	RLCI(EQ);
	RLCI(REQ);
	RLCI(ALL);
	RLCI(LT);
	RLCI(LE);
	RLCI(GE);
	RLCI(GT);
	RLCI(BITSET_ALL_SET);
	RLCI(BITSET_ANY_SET);
	RLCI(BITSET_ALL_NOT_SET);
	RLCI(OVERLAPS);
	RLCI(NEIGHBOR);
	TARANTOOL_G(sync_counter) = 0;
	TARANTOOL_G(retry_count)  = INI_INT("tarantool.retry_count");
	TARANTOOL_G(retry_sleep)  = INI_FLT("tarantool.retry_sleep");
	zend_bool persistent  = INI_BOOL("tarantool.persistent");
	TARANTOOL_G(persistent) = persistent;
	zend_bool deauthorize = INI_BOOL("tarantool.deauthorize");
	TARANTOOL_G(deauthorize) = deauthorize;
	int con_per_host = INI_INT("tarantool.con_per_host");
	TARANTOOL_G(manager) = pool_manager_create(persistent, con_per_host, deauthorize);
	zend_class_entry tarantool_class;
	INIT_CLASS_ENTRY(tarantool_class, "Tarantool", tarantool_class_methods);
	tarantool_class.create_object = tarantool_create;
	tarantool_class_ptr = zend_register_internal_class(&tarantool_class TSRMLS_CC);
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(tarantool) {
	//pool_manager_free(TARANTOOL_G(manager));
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MINFO_FUNCTION(tarantool) {
	php_info_print_table_start();
	php_info_print_table_header(2, "Tarantool support", "enabled");
	php_info_print_table_row(2, "Extension version", PHP_TARANTOOL_VERSION);
	php_info_print_table_end();
	DISPLAY_INI_ENTRIES();
}

PHP_METHOD(tarantool_class, __construct) {
	char *host = NULL; int host_len = 0;
	long port = 0;

	TARANTOOL_PARSE_PARAMS(id, "|sl", &host, &host_len, &port);
	TARANTOOL_FETCH_OBJECT(obj, id);

	/*
	 * validate parameters
	 */

	if (host == NULL) host = "localhost";
	if (port < 0 || port >= 65536) {
		THROW_EXC("Invalid primary port value: %li", port);
		RETURN_FALSE;
	}
	if (port == 0) port = 3301;

	/* initialzie object structure */
	obj->host = pestrdup(host, 1);
	obj->port = port;
	obj->value = (smart_str *)pecalloc(sizeof(smart_str), 1, 1);
	obj->auth = 0;
	obj->greeting = (char *)pecalloc(sizeof(char), GREETING_SIZE, 1);
	obj->salt = NULL;
	obj->login = NULL;
	obj->passwd = NULL;
	obj->persistent_id = NULL;
	obj->schema_hash = NULL;
	smart_str_ensure(obj->value, GREETING_SIZE);
	return;
}

PHP_METHOD(tarantool_class, connect) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	if (__tarantool_connect(obj, id TSRMLS_CC) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

int __tarantool_authenticate(tarantool_object *obj) {
	char *salt = php_base64_decode(obj->salt, SALT64_SIZE, NULL);
	char scramble[SCRAMBLE_SIZE];
	zval *header = NULL, *body = NULL;
	TSRMLS_FETCH();
	long sync = TARANTOOL_G(sync_counter)++;

	size_t passwd_len = (obj->passwd ? strlen(obj->passwd) : 0);
	scramble_prepare(scramble, salt, obj->passwd, passwd_len);
	efree(salt);
	php_tp_encode_auth(obj->value, sync, obj->login,
			strlen(obj->login), scramble);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		return 1;

	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		return 1;

	zval_ptr_dtor(&header);
	zval_ptr_dtor(&body);
	return 0;
}

PHP_METHOD(tarantool_class, authenticate) {
	char *login; int login_len;
	char *passwd; int passwd_len;

	TARANTOOL_PARSE_PARAMS(id, "ss", &login, &login_len,
			&passwd, &passwd_len);
	TARANTOOL_FETCH_OBJECT(obj, id);
	obj->login = pestrdup(login, 1);
	obj->passwd = estrdup(passwd);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	if (__tarantool_authenticate(obj))
		RETURN_FALSE;
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, flushSchema) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	schema_flush(obj TSRMLS_CC);
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, flush_schema) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	schema_flush(obj TSRMLS_CC);
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, close) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	if (!TARANTOOL_G(persistent)) {
		tarantool_stream_close(obj TSRMLS_CC);
		obj->stream = NULL;
		//schema_flush(obj TSRMLS_CC);
		if (obj->schema_hash != NULL) zval_ptr_dtor(&obj->schema_hash);
		obj->schema_hash = NULL;
	}
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, ping) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(obj->value, sync);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval_ptr_dtor(&header);
	zval_ptr_dtor(&body);
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, select) {
	zval *space = NULL, *index = NULL;
	zval *key = NULL, *key_new = NULL;
	long limit = -1, offset = 0, iterator = 0;

	TARANTOOL_PARSE_PARAMS(id, "z|zzlll", &space, &key,
			&index, &limit, &offset, &iterator);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index TSRMLS_CC);
		if (index_no == FAILURE) RETURN_FALSE;
	}
	key_new = pack_key(key, 1);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_select(obj->value, sync, space_no, index_no,
			limit, offset, iterator, key_new);
	if (key != key_new) {
		if (key) Z_ADDREF_P(key);
		zval_ptr_dtor(&key_new);
		if (key) Z_DELREF_P(key);
	}
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header = NULL, *body = NULL;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, insert) {
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_INSERT);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, replace) {
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_REPLACE);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, delete) {
	zval *space = NULL, *key = NULL;
	zval *key_new = NULL;

	TARANTOOL_PARSE_PARAMS(id, "zz", &space, &key);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);


	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE) RETURN_FALSE;

	key_new = pack_key(key, 0);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_delete(obj->value, sync, space_no, key);
	if (key != key_new) {
		if (key) Z_ADDREF_P(key);
		zval_ptr_dtor(&key_new);
		if (key) Z_DELREF_P(key);
	}
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, call) {
	char *proc; size_t proc_len;
	zval *tuple = NULL;

	TARANTOOL_PARSE_PARAMS(id, "s|z", &proc, &proc_len, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	zval *tuple_new = pack_key(tuple, 1);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_call(obj->value, sync, proc, proc_len, tuple_new);
	if (tuple_new != tuple) {
		if (tuple) Z_ADDREF_P(tuple);
		zval_ptr_dtor(&tuple_new);
		if (tuple) Z_DELREF_P(tuple);
	}
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, eval) {
	char *proc; size_t proc_len;
	zval *tuple = NULL;

	TARANTOOL_PARSE_PARAMS(id, "s|z", &proc, &proc_len, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	zval *tuple_new = pack_key(tuple, 1);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_eval(obj->value, sync, proc, proc_len, tuple_new);
	if (tuple_new != tuple) {
		if (tuple) Z_ADDREF_P(tuple);
		zval_ptr_dtor(&tuple_new);
		if (tuple) Z_DELREF_P(tuple);
	}
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, update) {
	zval *space = NULL, *key = NULL;
	zval *args = NULL, *key_new = NULL;

	TARANTOOL_PARSE_PARAMS(id, "zza", &space, &key, &args);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE)
		RETURN_FALSE;

	args = tarantool_update_verify_args(args TSRMLS_CC);
	if (!args) RETURN_FALSE;
	key_new = pack_key(key, 0);
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_update(obj->value, sync, space_no, key_new, args);
	if (key != key_new) {
		if (key) Z_ADDREF_P(key);
		zval_ptr_dtor(&key_new);
		if (key) Z_DELREF_P(key);
	}
	//zval_ptr_dtor(&args);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}
