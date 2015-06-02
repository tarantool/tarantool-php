#include <sys/socket.h>
#include <netinet/tcp.h>

#include <sys/time.h>
#include <time.h>
#include <limits.h>

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
#include "tarantool_schema.h"
#include "tarantool_tp.h"

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

zend_function_entry tarantool_module_functions[] = {
	PHP_ME(tarantool_class, __construct, NULL, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, evaluate, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, flushSchema, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, disconnect, close, NULL, ZEND_ACC_PUBLIC)
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
	obj->schema = tarantool_schema_new();
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
	if (obj->login != NULL && obj->passwd != NULL) {
		tarantool_schema_flush(obj->schema);
		__tarantool_authenticate(obj);
	}
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
		if (obj->passwd) efree(obj->passwd);
		obj->passwd = NULL;
		tarantool_schema_flush(obj->schema);
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
		tarantool_schema_delete(obj->schema);
	}
	if (obj->value) smart_str_free_ex(obj->value, 1);
	if (obj->tps)   tarantool_tp_free(obj->tps);
	if (obj->value) pefree(obj->value, 1);
	pefree(obj, 1);
}

static zend_object_value tarantool_create(zend_class_entry *entry TSRMLS_DC) {
	zend_object_value retval;
	tarantool_object *obj = NULL;

	obj = (tarantool_object *)pecalloc(sizeof(tarantool_object), 1, 1);
	zend_object_std_init(&obj->zo, entry TSRMLS_CC);
#if PHP_VERSION_ID >= 50400
	object_properties_init((zend_object *)obj, entry);
#else
	{
		zval *tmp;
		zend_hash_copy(obj->zo.properties,
				&entry->default_properties,
				(copy_ctor_func_t) zval_add_ref,
				(void *)&tmp,
				sizeof(zval *));
	}
#endif
	retval.handle = zend_objects_store_put(obj,
			(zend_objects_store_dtor_t )zend_objects_destroy_object,
			(zend_objects_free_object_storage_t )tarantool_free,
			NULL TSRMLS_CC);
	retval.handlers = zend_get_std_object_handlers();
	return retval;
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
		*header = NULL;
		goto error;
	}
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying msgpack");
		goto error;
	}
	if (php_mp_unpack(body, &pos) == FAILURE) {
		*body = NULL;
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
	PHP_ME(tarantool_class, authenticate, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, ping, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, select, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, insert, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, replace, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, call, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, delete, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(tarantool_class, update, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, evaluate, eval, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, flushSchema, flush_schema, NULL, ZEND_ACC_PUBLIC)
	PHP_MALIAS(tarantool_class, disconnect, close, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

/* ####################### HELPERS ####################### */

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
	Z_ADDREF_P(args);
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

int get_spaceno_by_name(tarantool_object *obj, zval *id, zval *name TSRMLS_DC) {
	if (Z_TYPE_P(name) == IS_LONG) return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Space ID must be String or Long");
		return FAILURE;
	}
	int32_t space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no != FAILURE) return space_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_SPACE, INDEX_SPACE_NAME, 0, 4096);
	tp_key(obj->tps, 1);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5 TSRMLS_CC) != 5) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_str_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c,
				body_size TSRMLS_CC) != body_size) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
		THROW_EXC("Failed to parse query");
		return FAILURE;
	}

	if (resp.error) {
		THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
		return FAILURE;
	}

	if (tarantool_schema_add_spaces(obj->schema, resp.data, resp.data_len)) {
		THROW_EXC("Failed parsing schema (space) or memory issues");
		return FAILURE;
	}
	space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no == FAILURE)
		THROW_EXC("No space '%s' defined", Z_STRVAL_P(name));
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
	int32_t index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no != FAILURE) return index_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_INDEX, INDEX_INDEX_NAME, 0, 4096);
	tp_key(obj->tps, 2);
	tp_encode_uint(obj->tps, space_no);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5 TSRMLS_CC) != 5) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_str_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c,
				body_size TSRMLS_CC) != body_size) {
		THROW_EXC("Can't read query from server");
		return FAILURE;
	}

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
		THROW_EXC("Failed to parse query");
		return FAILURE;
	}

	if (resp.error) {
		THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
		return FAILURE;
	}

	if (tarantool_schema_add_indexes(obj->schema, resp.data, resp.data_len)) {
		THROW_EXC("Failed parsing schema (index) or memory issues");
		return FAILURE;
	}
	index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no == FAILURE)
		THROW_EXC("No index '%s' defined", Z_STRVAL_P(name));
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
	obj->value = (smart_str *)pemalloc(sizeof(smart_str), 1);
	obj->auth = 0;
	obj->greeting = (char *)pecalloc(sizeof(char), GREETING_SIZE, 1);
	obj->salt = NULL;
	obj->login = NULL;
	obj->passwd = NULL;
	obj->persistent_id = NULL;
	obj->schema = tarantool_schema_new();
	obj->value->len = 0;
	obj->value->a = 0;
	obj->value->c = NULL;
	smart_str_ensure(obj->value, GREETING_SIZE);
	obj->tps = tarantool_tp_new(obj->value);
	return;
}

PHP_METHOD(tarantool_class, connect) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	if (obj->stream && obj->stream->mode) RETURN_TRUE;
	if (__tarantool_connect(obj, id TSRMLS_CC) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

int __tarantool_authenticate(tarantool_object *obj) {
	TSRMLS_FETCH();

	tarantool_tp_update(obj->tps);
	int batch_count = 3;
	size_t passwd_len = (obj->passwd ? strlen(obj->passwd) : 0);
	tp_auth(obj->tps, obj->salt, obj->login, strlen(obj->login), obj->passwd, passwd_len);
	uint32_t auth_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, auth_sync);
	tp_select(obj->tps, SPACE_SPACE, 0, 0, 4096);
	tp_key(obj->tps, 0);
	uint32_t space_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, space_sync);
	tp_select(obj->tps, SPACE_INDEX, 0, 0, 4096);
	tp_key(obj->tps, 0);
	uint32_t index_sync = TARANTOOL_G(sync_counter)++;
	tp_reqid(obj->tps, index_sync);
	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		return FAILURE;

	int status = SUCCESS;

	while (batch_count-- > 0) {
		char pack_len[5] = {0, 0, 0, 0, 0};
		if (tarantool_stream_read(obj, pack_len, 5 TSRMLS_CC) != 5) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		size_t body_size = php_mp_unpack_package_size(pack_len);
		smart_str_ensure(obj->value, body_size);
		if (tarantool_stream_read(obj, obj->value->c,
					body_size TSRMLS_CC) != body_size) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		if (status == FAILURE) continue;
		struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
		if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
			THROW_EXC("Failed to parse query");
			status = FAILURE;
		}

		if (resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
			status = FAILURE;
		}
		if (resp.sync == space_sync) {
			if (tarantool_schema_add_spaces(obj->schema, resp.data,
						        resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (space) or memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == index_sync) {
			if (tarantool_schema_add_indexes(obj->schema, resp.data,
							 resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (index) or memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == auth_sync && resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code, resp.error_len, resp.error);
			status = FAILURE;
		}
	}

	return status;
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

PHP_METHOD(tarantool_class, flush_schema) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	tarantool_schema_flush(obj->schema);
	RETURN_TRUE;
}

PHP_METHOD(tarantool_class, close) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	if (!TARANTOOL_G(persistent)) {
		tarantool_stream_close(obj TSRMLS_CC);
		obj->stream = NULL;
		tarantool_schema_delete(obj->schema);
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
	long limit = LONG_MAX-1, offset = 0, iterator = 0;

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
		zval_ptr_dtor(&key_new);
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
		zval_ptr_dtor(&key_new);
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
		zval_ptr_dtor(&tuple_new);
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
		zval_ptr_dtor(&tuple_new);
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
		zval_ptr_dtor(&key_new);
	}
	//zval_ptr_dtor(&args);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}
