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

#include "tarantool_msgpack.h"
#include "tarantool_proto.h"

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define TARANTOOL_PARSE_PARAMS(ID, FORMAT, ...) zval *ID;\
		if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,\
					getThis(), "O" FORMAT,\
					&ID, tarantool_class_ptr,\
					__VA_ARGS__) == FAILURE) {\
			RETURN_FALSE;\
		}

#define TARANTOOL_FETCH_OBJECT(NAME, ID)\
		tarantool_object *NAME = (tarantool_object *)\
				zend_object_store_get_object(ID TSRMLS_CC)

#define TARANTOOL_CONNECT_ON_DEMAND(CON, ID)\
		if (!CON->stream && connect_on_demand(CON, ID) == FAILURE)\
			RETURN_FALSE;


#define THROW_EXC(...) zend_throw_exception_ex(\
		zend_exception_get_default(TSRMLS_C),\
		0 TSRMLS_CC, __VA_ARGS__)

#define TARANTOOL_RETURN_DATA(HT, HEAD, BODY)\
		HashTable *ht_ ## HT = HASH_OF(HT);\
		zval **answer = NULL;\
		if (zend_hash_index_find(ht_ ## HT, TNT_DATA,\
				(void **)&answer) == FAILURE || !answer) {\
			THROW_EXC("No field DATA in body");\
			RETURN_FALSE;\
		}\
		RETVAL_ZVAL(*answer, 1, 0);\
		zval_ptr_dtor(&HEAD);\
		zval_ptr_dtor(&BODY);\
		return;

typedef struct tarantool_object {
	zend_object zo;
	char *host;
	int port;
	php_stream *stream;
	smart_str *value;
	char auth;
	char *greeting;
	char *salt;
	zval *schema_hash;
} tarantool_object;

zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
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
						     &errstr, &errcode
						     TSRMLS_CC);
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
					0 TSRMLS_CC, "failed to connect: "
					"setsockopt error %s",
					strerror_r(errno,
						   errbuf,
						   sizeof(errbuf)));
		goto process_error;
	}
	obj->stream = stream;
	return SUCCESS;

process_error:
	if (errstr) efree(errstr);
	if (stream) php_stream_close(stream);
	return FAILURE;
}

#include <stdio.h>

static int
tarantool_stream_send(tarantool_object *obj TSRMLS_DC) {
	int i = 0;
	TSRMLS_FETCH();

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
	smart_str_free(obj->value); efree(obj->value);
	efree(obj->greeting); efree(obj->host);
	zval_ptr_dtor(&obj->schema_hash);
	efree(obj);
}

static zend_object_value tarantool_create(zend_class_entry *entry TSRMLS_DC) {
	zend_object_value new_value;
	TSRMLS_FETCH();

	tarantool_object *obj = (tarantool_object *)ecalloc(sizeof(tarantool_object), 1);
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
			ALLOC_INIT_ZVAL(*errstr);
			ZVAL_STRING(*errstr, "empty", 1);
		}
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"Query Error %d: %s", errcode,
				Z_STRVAL_PP(errstr), Z_STRLEN_PP(errstr));
		zval_ptr_dtor(errstr);
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
	if (header) zval_ptr_dtor(header);
	if (body) zval_ptr_dtor(body);
	SSTR_LEN(obj->value) = 0;
	smart_str_nullify(obj->value);
	return FAILURE;
}


const zend_function_entry tarantool_class_methods[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

/* ####################### HELPERS ####################### */

/* TODO: BEFORE EXCEPTION - CLEANUP BUFFER */
/* TODO: SPACE_NO/INDEX_NO TO SPACE/INDEX (SCHEMA) + "l" -> "z" */

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

zval *tarantool_update_verify_op(zval *op, long position) {
	if (Z_TYPE_P(op) != IS_ARRAY || !php_mp_is_hash(op)) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC, "Op must be Map at position %d",
				position);
		return NULL;
	}
	HashTable *ht = HASH_OF(op);
	size_t n = zend_hash_num_elements(ht);
	zval *arr; ALLOC_INIT_ZVAL(arr); array_init_size(arr, n);
	zval **opstr, **oppos;
	if (zend_hash_find(ht, "op", 3, (void **)&opstr) == FAILURE ||
			!opstr || Z_TYPE_PP(opstr) != IS_STRING ||
			Z_STRLEN_PP(opstr) != 1) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC, "Field OP must be provided"
				" and must be STRING with length=1 at position %d", position);
		return NULL;
	}
	if (zend_hash_find(ht, "field", 6, (void **)&oppos) == FAILURE ||
			!oppos || Z_TYPE_PP(oppos) != IS_LONG) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC, "Field FIELD must be provided"
				" and must be LONG at position %d", position);
		return NULL;

	}
	zval **oparg, **splice_len, **splice_val;
	switch(Z_STRVAL_PP(opstr)[0]) {
	case ':':
		if (zend_hash_find(ht, "offset", 7,
				(void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Field OFFSET must be provided"
						" and must be LONG for splice at position %d",
						position);
			return NULL;
		}
		if (zend_hash_find(ht, "length", 7, (void **)&splice_len) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_len) != IS_LONG) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Field LENGTH must be provided"
						" and must be LONG for splice at position %d",
						position);
			return NULL;
		}
		if (zend_hash_find(ht, "list", 5, (void **)&splice_val) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_val) != IS_STRING) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Field LIST must be provided"
						" and must be STRING for splice at position %d",
						position);
			return NULL;
		}
		if (n != 5) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Five fields must be provided for"
						" splice at position %d", position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		add_next_index_long(arr, Z_LVAL_PP(oparg));
		add_next_index_long(arr, Z_LVAL_PP(splice_len));
		add_next_index_stringl(arr, Z_STRVAL_PP(splice_val),
				Z_STRLEN_PP(splice_val), 0);
		break;
	case '+':
	case '-':
	case '&':
	case '^':
	case '#':
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Field ARG must be provided"
						" and must be LONG for '%s' at position %d",
						Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		if (n != 3) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Three fields must be provided "
						"for '%s' at position %d",
						Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		add_next_index_long(arr, Z_LVAL_PP(oparg));
		break;
	case '=':
	case '!':
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || !PHP_MP_SERIALIZABLE_PP(oparg)) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Field ARG must be provided"
						" and must be SERIALIZABLE for '%s' at position %d",
						Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		if (n != 3) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Three fields must be provided "
						"for '%s' at position %d",
						Z_STRVAL_PP(opstr), position);
			return NULL;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		add_next_index_zval(arr, *oparg);
		break;
	default:
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
					0 TSRMLS_CC, "Unknown operation '%s' at position %d",
					Z_STRVAL_PP(opstr), position);
		return NULL;

	}
	return arr;
}

zval *tarantool_update_verify_args(zval *args) {
	if (Z_TYPE_P(args) != IS_ARRAY || php_mp_is_hash(args)) {
		zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
					0 TSRMLS_CC, "Provided value for update ops must ");
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
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Internal HASH Error");
			return NULL;
		}
		zval *op_arr = tarantool_update_verify_op(*op, key_index);
		if (!op_arr)
			return NULL;
		if (add_next_index_zval(arr, op_arr) == FAILURE) {
			zend_throw_exception_ex(zend_exception_get_default(TSRMLS_C),
						0 TSRMLS_CC, "Internal HASH Error");
			return NULL;
		}
	}
	return arr;
}

char connect_on_demand(tarantool_object *obj, zval *id) {
	zval *fname; ALLOC_INIT_ZVAL(fname);
	ZVAL_STRING(fname, "connect", 1);
	zval *retval; ALLOC_INIT_ZVAL(retval);
	zval *args[] = {id};

	call_user_function(NULL, &obj, fname, retval, 1, args TSRMLS_CC);
	if (!retval || !Z_TYPE_P(retval) || !Z_BVAL_P(retval))
		return FAILURE;

	zval_ptr_dtor(&fname);
	zval_ptr_dtor(&retval);
	return SUCCESS;
}

void schema_flush(tarantool_object *obj) {
	zval_ptr_dtor(&obj->schema_hash);
	ALLOC_INIT_ZVAL(obj->schema_hash); array_init(obj->schema_hash);
}

int schema_add_space(tarantool_object *obj, uint32_t space_no,
		char *space_name, size_t space_len) {
	zval *s_hash, *i_hash;
	ALLOC_INIT_ZVAL(s_hash); array_init(s_hash);
	ALLOC_INIT_ZVAL(i_hash); array_init(i_hash);
	add_next_index_long(s_hash, space_no);
	add_next_index_stringl(s_hash, space_name, space_len, 1);
	add_next_index_zval(s_hash, i_hash);
	add_index_zval(obj->schema_hash, space_no, s_hash);
	add_assoc_zval_ex(obj->schema_hash, space_name, space_len, s_hash);
}

int schema_add_index(tarantool_object *obj, uint32_t space_no,
		uint32_t index_no, char *index_name, size_t index_len) {
	zval **s_hash, **i_hash;
	zend_hash_index_find(HASH_OF(obj->schema_hash),
			space_no, (void **)&s_hash);
	zend_hash_index_find(HASH_OF(*s_hash), 2, (void **)&i_hash);
	add_assoc_long_ex(*i_hash, index_name, index_len, index_no);
}

uint32_t schema_get_space(tarantool_object *obj,
		char *space_name, size_t space_len) {
	HashTable *ht = HASH_OF(obj->schema_hash);
	zval **stuple, **sid;
	if (zend_hash_find(ht, space_name, space_len,
			   (void **)&stuple) == FAILURE || !stuple)
		return FAILURE;
	if (zend_hash_index_find(ht, 0, (void **)&sid) == FAILURE || !sid)
		return FAILURE;
	return Z_LVAL_PP(sid);
}

uint32_t schema_get_index(tarantool_object *obj, uint32_t space_no,
		char *index_name, size_t index_len) {
	HashTable *ht = HASH_OF(obj->schema_hash);
	zval **stuple, **ituple, **iid;
	if (zend_hash_index_find(ht, space_no,
			   (void **)&stuple) == FAILURE || !stuple)
		return FAILURE;
	if (zend_hash_index_find(ht, 2, (void **)&ituple) == FAILURE || !ituple)
		return FAILURE;
	if (zend_hash_find(ht, index_name, index_len,
				(void **)&iid) == FAILURE || !iid)
		return FAILURE;
	return Z_LVAL_PP(iid);
}

int get_spaceno_by_name(tarantool_object *obj, zval *id, zval *name) {
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Space ID must be String or Long");
		return FAILURE;
	}
	uint32_t space_no = schema_get_space(obj,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no != FAILURE)
		return space_no;

	zval *fname, *spaceno, *indexno, *key;
	ALLOC_INIT_ZVAL(key); array_init_size(key, 1);
	ALLOC_INIT_ZVAL(fname); ZVAL_STRING(fname, "select", 1);
	ALLOC_INIT_ZVAL(spaceno); ZVAL_LONG(spaceno, SPACE_SPACE);
	ALLOC_INIT_ZVAL(indexno); ZVAL_LONG(indexno, INDEX_SPACE_NAME);
	add_next_index_zval(key, name);
	zval *retval; ALLOC_INIT_ZVAL(retval);
	zval *args[] = {id, spaceno, key, indexno};
	call_user_function(NULL, &obj, fname, retval, 4, args TSRMLS_CC);
	if (Z_TYPE_P(retval) == IS_BOOL && !Z_BVAL_P(retval))
		return FAILURE;
	zval **tuple, **field;
	if (zend_hash_index_find(HASH_OF(retval), 0,
				(void **)&tuple) == FAILURE) {
		THROW_EXC("No space '%s' defined", Z_STRVAL_P(name));
		return FAILURE;
	}
	if (zend_hash_index_find(HASH_OF(*tuple), 0,
				(void **)&field) == FAILURE ||
			!field || Z_TYPE_PP(field) != IS_LONG) {
		THROW_EXC("Bad data came from server");
		return FAILURE;
	}
	if (schema_add_space(obj, Z_LVAL_PP(field),
			Z_STRVAL_P(name), Z_STRLEN_P(name)) == FAILURE) {
		THROW_EXC("Internal Error");
		return FAILURE;
	}
	long space = Z_LVAL_PP(field);
	zval_ptr_dtor(&key);
	zval_dtor(fname);
	zval_dtor(spaceno);
	zval_dtor(indexno);
	zval_ptr_dtor(&retval);
	return space;
}

int get_indexno_by_name(tarantool_object *obj, zval *id,
		int space_no, zval *name) {
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Index ID must be String or Long");
		return FAILURE;
	}
	uint32_t index_no = schema_get_space(obj,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
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
	if (Z_TYPE_P(retval) == IS_BOOL)
		return FAILURE;
	zval **tuple, **field;
	if (zend_hash_index_find(HASH_OF(retval), 0, (void **)&tuple) == FAILURE) {
		THROW_EXC("No index '%s' defined", Z_STRVAL_P(name));
		return FAILURE;
	}
	if (zend_hash_index_find(HASH_OF(*tuple), 1, (void **)&field) == FAILURE ||
			!tuple || Z_TYPE_PP(field) != IS_LONG) {
		THROW_EXC("Bad data came from server");
		return FAILURE;
	}
	if (schema_add_index(obj, index_no, Z_LVAL_PP(field),
			Z_STRVAL_P(name), Z_STRLEN_P(name)) == FAILURE) {
		THROW_EXC("Internal Error");
		return FAILURE;
	}
	long index = Z_LVAL_PP(field);
	zval_ptr_dtor(&key);
	zval_ptr_dtor(&fname);
	zval_ptr_dtor(&retval);
	zval_ptr_dtor(&spaceno);
	zval_ptr_dtor(&indexno);
	return index;
}

/* ####################### METHODS ####################### */

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
	if (port == 0) port = 33013;

	/* initialzie object structure */
	obj->host = estrdup(host);
	obj->port = port;
	obj->stream = NULL;
	obj->value = (smart_str *)ecalloc(sizeof(smart_str), 1);
	obj->auth = 0;
	obj->greeting = (char *)ecalloc(sizeof(char), GREETING_SIZE);
	obj->salt = NULL;
	ALLOC_INIT_ZVAL(obj->schema_hash); array_init(obj->schema_hash);
	smart_str_ensure(obj->value, GREETING_SIZE);
	return;
}

PHP_METHOD(tarantool_class, connect) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	if (tarantool_stream_open(obj) == FAILURE)
		RETURN_FALSE;
	if (tarantool_stream_read(obj, obj->greeting,
				GREETING_SIZE) != GREETING_SIZE) {
		THROW_EXC("Can't read Greeting from server");
		zend_throw_exception_ex(
				zend_exception_get_default(TSRMLS_C),
				0 TSRMLS_CC,
				"can't read greeting from server");
		RETURN_FALSE;
	}
	obj->salt = obj->greeting + SALT_PREFIX_SIZE;
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, authenticate) {
	char *login; int login_len;
	char *passwd; int passwd_len;

	TARANTOOL_PARSE_PARAMS(id, "ss", &login, &login_len,
			&passwd, &passwd_len);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	char *salt = php_base64_decode(obj->salt, SALT64_SIZE, NULL);
	char scramble[SCRAMBLE_SIZE];

	scramble_prepare(scramble, salt, passwd, passwd_len);
	efree(salt);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_auth(obj->value, sync, login, login_len, scramble);
	tarantool_stream_send(obj);

	zval *header = NULL, *body = NULL;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, flush_schema) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	schema_flush(obj);
	RETURN_TRUE;
}
PHP_METHOD(tarantool_class, ping) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(obj->value, sync);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, select) {
	zval *space, *index = NULL, *key = NULL;
	long limit = -1, offset = 0, iterator = 0;

	TARANTOOL_PARSE_PARAMS(id, "z|zzlll", &space, &key,
			&index, &limit, &offset, &iterator);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;
	long index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index);
		if (index_no == FAILURE)
			RETURN_FALSE;
	}
	key = pack_key(key, 1);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_select(obj->value, sync, space_no, index_no,
			limit, offset, iterator, key);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, insert) {
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_INSERT);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, replace) {
	zval *space, *tuple;

	TARANTOOL_PARSE_PARAMS(id, "za", &space, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_REPLACE);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, delete) {
	zval *space, *key;

	TARANTOOL_PARSE_PARAMS(id, "zz", &space, &key);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	key = pack_key(key, 0);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_delete(obj->value, sync, space_no, key);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, call) {
	char *proc; size_t proc_len;
	zval *tuple = NULL;

	TARANTOOL_PARSE_PARAMS(id, "s|z", &proc, &proc_len, &tuple);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	tuple = pack_key(tuple, 1);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_call(obj->value, sync, proc, proc_len, tuple);
	tarantool_stream_send(obj);

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}

PHP_METHOD(tarantool_class, update)
{
	zval *space, *key, *args;

	TARANTOOL_PARSE_PARAMS(id, "zza", &space, &key, &args);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	key = pack_key(key, 0);

	args = tarantool_update_verify_args(args);
	if (!args)
		RETURN_FALSE;
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_update(obj->value, sync, space_no, key, args);
	tarantool_stream_send(obj);

	zval_ptr_dtor(&args);
	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}
