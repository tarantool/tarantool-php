#include <time.h>
#include <stdio.h>
#include <limits.h>

#include "php_tarantool.h"

#include "tarantool_network.h"
#include "tarantool_msgpack.h"
#include "tarantool_proto.h"
#include "tarantool_schema.h"
#include "tarantool_tp.h"

int __tarantool_authenticate(tarantool_connection *obj);

double
now_gettimeofday(void)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * 1e9 + t.tv_usec * 1e3;
}

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

static int le_tarantool = 0;
static zend_class_entry *tarantool_ce = NULL;
static zend_class_entry *tarantool_exception_ce = NULL;

#define TARANTOOL_PARSE_PARAMS(ID, FORMAT, ...)				\
	zval *ID;							\
	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC,	\
				getThis(), "O" FORMAT,			\
				&ID, Tarantool_ptr,		\
				__VA_ARGS__) == FAILURE)		\
		RETURN_FALSE;

#define TARANTOOL_FETCH_OBJECT(NAME, ID)				\
	tarantool_object *t_##NAME = (tarantool_object *)		\
			zend_object_store_get_object(ID TSRMLS_CC);	\
	tarantool_connection *NAME = (t_##NAME)->obj;

#define TARANTOOL_CONNECT_ON_DEMAND(CON, ID)					\
	if (!CON->stream) {							\
		if (__tarantool_connect(t_##CON, ID TSRMLS_CC) == FAILURE)	\
			RETURN_FALSE;						\
	}									\
	if (CON->stream && php_stream_eof(CON->stream) != 0)			\
		if (__tarantool_reconnect(t_##CON, ID TSRMLS_CC) == FAILURE)	\
			RETURN_FALSE;

#define TARANTOOL_RETURN_DATA(HT, HEAD, BODY)				\
	HashTable *ht_ ## HT = HASH_OF(HT);				\
	zval **answer = NULL;						\
	if (zend_hash_index_find(ht_ ## HT, TNT_DATA,			\
			(void **)&answer) == FAILURE || !answer) {	\
		THROW_EXC("No field DATA in body");			\
		zval_ptr_dtor(&HEAD);					\
		zval_ptr_dtor(&BODY);					\
		RETURN_FALSE;						\
	}								\
	RETVAL_ZVAL(*answer, 1, 0);					\
	zval_ptr_dtor(&HEAD);						\
	zval_ptr_dtor(&BODY);						\
	return;

#define TARANTOOL_PERSISTENT_FIND(NAME, LEN, WHERE)			\
	zend_hash_find(&EG(persistent_list), (NAME), (LEN),		\
		       (void *)&(WHERE))

#define TARANTOOL_PERSISTENT_UPDATE(NAME, WHERE)			\
	zend_hash_find(&EG(persistent_list), (NAME), strlen((NAME)),	\
		       (void *)&(WHERE))

#if HAVE_SPL
static zend_class_entry *spl_ce_RuntimeException = NULL;
#endif

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_exception_base(int root TSRMLS_DC)
{
#if HAVE_SPL
	if (!root) {
		if (!spl_ce_RuntimeException) {
			zend_class_entry **pce;

			if (zend_hash_find(CG(class_table), "runtimeexception",
					   sizeof("RuntimeException"),
					   (void **) &pce) == SUCCESS) {
				spl_ce_RuntimeException = *pce;
				return *pce;
			}
		} else {
			return spl_ce_RuntimeException;
		}
	}
#endif
#if (PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 2)
	return zend_exception_get_default();
#else
	return zend_exception_get_default(TSRMLS_C);
#endif
}

zend_function_entry tarantool_module_functions[] = {
	{NULL, NULL, NULL}
};

zend_module_entry tarantool_module_entry = {
	STANDARD_MODULE_HEADER,
	PHP_TARANTOOL_EXTNAME,
	tarantool_module_functions,
	PHP_MINIT(tarantool),
	PHP_MSHUTDOWN(tarantool),
	PHP_RINIT(tarantool),
	NULL,
	PHP_MINFO(tarantool),
	PHP_TARANTOOL_VERSION,
	STANDARD_MODULE_PROPERTIES
};

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("tarantool.persistent", "0", PHP_INI_ALL,
			  OnUpdateBool, persistent, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.timeout", "10.0", PHP_INI_ALL,
			  OnUpdateReal, timeout, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.request_timeout", "10.0", PHP_INI_ALL,
			  OnUpdateReal, request_timeout, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.retry_count", "1", PHP_INI_ALL,
			  OnUpdateLong, retry_count, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.retry_sleep", "0.1", PHP_INI_ALL,
			  OnUpdateReal, retry_sleep, zend_tarantool_globals,
			  tarantool_globals)
PHP_INI_END()

#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif

static int
tarantool_stream_send(tarantool_connection *obj TSRMLS_DC) {
	int rv = tntll_stream_send(obj->stream, SSTR_BEG(obj->value),
				   SSTR_LEN(obj->value));
	if (rv) return FAILURE;
	SSTR_LEN(obj->value) = 0;
	smart_string_nullify(obj->value);
	return SUCCESS;
}

static char *persistent_id(const char *host, int port, const char *login,
			   const char *prefix, int *olen,
			   const char *suffix, int suffix_len) {
	char *plist_id = NULL, *tmp = NULL;
	/* if login is not defined, then login is 'guest' */
	login = (login ? login : "guest");
	int len = 0;
	len = spprintf(&plist_id, 0, "tarantool-%s:id=%s:%d-%s", prefix, host,
		       port, login) + 1;
	if (suffix) {
		len = spprintf(&tmp,0,"%s[%.*s]",plist_id,suffix_len,suffix);
		efree(plist_id);
		plist_id = tmp;
	}
	tmp = pestrdup(plist_id, 1);
	efree(plist_id);
	if (olen)
		*olen = len;
	return tmp;
}

/*
 * Legacy rtsisyk code, php_stream_read made right
 * See https://bugs.launchpad.net/tarantool/+bug/1182474
 */
static size_t
tarantool_stream_read(tarantool_connection *obj, char *buf, size_t size) {
	return tntll_stream_read2(obj->stream, buf, size);
}

static void
tarantool_stream_close(tarantool_connection *obj) {
	if (obj->stream || obj->persistent_id) {
		tntll_stream_close(obj->stream, obj->persistent_id);
	}
	obj->stream = NULL;
	if (obj->persistent_id != NULL) {
		pefree(obj->persistent_id, 1);
		obj->persistent_id = NULL;
	}
}

int __tarantool_connect(tarantool_object *t_obj, zval *id TSRMLS_DC) {
	tarantool_connection *obj = t_obj->obj;
	int status = SUCCESS;
	long count = TARANTOOL_G(retry_count);
	struct timespec sleep_time = {0};
	double_to_ts(INI_FLT("retry_sleep"), &sleep_time);
	char *err = NULL;

	if (t_obj->is_persistent) {
		if (!obj->persistent_id)
			obj->persistent_id = persistent_id(obj->host, obj->port,
							   obj->orig_login,
							   "stream", NULL,
							   obj->suffix,
							   obj->suffix_len);
		int rv = tntll_stream_fpid2(obj->persistent_id, &obj->stream);
		if (obj->stream == NULL || rv != PHP_STREAM_PERSISTENT_SUCCESS)
			goto retry;
		return status;
	}
retry:
	while (count > 0) {
		--count;
		if (err) {
			/* If we're here, then there war error */
			nanosleep(&sleep_time, NULL);
			efree(err);
			err = NULL;
		}
		if (t_obj->is_persistent) {
			if (obj->persistent_id)
				pefree(obj->persistent_id, 1);
			obj->persistent_id = persistent_id(obj->host, obj->port,
							   obj->orig_login,
							   "stream", NULL,
							   obj->suffix,
							   obj->suffix_len);

		}
		if (tntll_stream_open(obj->host, obj->port,
				      obj->persistent_id,
				      &obj->stream, &err) == -1) {
			continue;
		}
		if (tntll_stream_read2(obj->stream, obj->greeting,
				       GREETING_SIZE) == -1) {
			continue;
		}
		++count;
		break;
	}
	if (count == 0) {
		char errstr[256];
		snprintf(errstr, 256, "%s", err);
		THROW_EXC(errstr);
		efree(err);
		return FAILURE;
	}
	if (obj->login != NULL && obj->passwd != NULL) {
		status = __tarantool_authenticate(obj);
	}
	return status;
}

int __tarantool_reconnect(tarantool_object *t_obj, zval *id TSRMLS_DC) {
	tarantool_connection *obj = t_obj->obj;
	tarantool_stream_close(obj);
	return __tarantool_connect(t_obj, id TSRMLS_CC);
}

static void
tarantool_connection_free(tarantool_connection *obj, int is_persistent
			  TSRMLS_DC) {
	if (obj == NULL)
		return;
	if (obj->greeting) {
		pefree(obj->greeting, is_persistent);
		obj->greeting = NULL;
	}
	tarantool_stream_close(obj);
	if (obj->persistent_id) {
		pefree(obj->persistent_id, 1);
		obj->persistent_id = NULL;
	}
	if (obj->schema) {
		tarantool_schema_delete(obj->schema, is_persistent);
		obj->schema = NULL;
	}
	if (obj->host) {
		pefree(obj->host, is_persistent);
		obj->host = NULL;
	}
	if (obj->login) {
		pefree(obj->login, is_persistent);
		obj->login = NULL;
	}
	if (obj->orig_login) {
		pefree(obj->orig_login, is_persistent);
		obj->orig_login = NULL;
	}
	if (obj->suffix) {
		pefree(obj->suffix, is_persistent);
		obj->suffix = NULL;
	}
	if (obj->passwd) {
		pefree(obj->passwd, is_persistent);
		obj->passwd = NULL;
	}
	if (obj->value) {
		smart_string_free_ex(obj->value, 1);
		pefree(obj->value, 1);
		obj->value = NULL;
	}
	if (obj->tps) {
		tarantool_tp_free(obj->tps, is_persistent);
		obj->tps = NULL;
	}
	pefree(obj, is_persistent);
}

static void
tarantool_object_free(tarantool_object *obj TSRMLS_DC) {
	if (obj == NULL)
		return;
	if (!obj->is_persistent && obj->obj != NULL) {
		tarantool_connection_free(obj->obj, obj->is_persistent
					  TSRMLS_CC);
		obj->obj = NULL;
	}
	efree(obj);
}

static zend_object_value tarantool_create(zend_class_entry *ce TSRMLS_DC) {
	zend_object_value retval;
	tarantool_object *obj = NULL;

	obj = (tarantool_object *)ecalloc(1, sizeof(tarantool_object));
	zend_object_std_init(&obj->zo, ce TSRMLS_CC);
#if PHP_VERSION_ID >= 50400
	object_properties_init((zend_object *)obj, ce);
#else
	{
		zval *tmp;
		zend_hash_copy(obj->zo.properties, &ce->default_properties,
				(copy_ctor_func_t) zval_add_ref, (void *)&tmp,
				sizeof(zval *));
	}
#endif
	retval.handle = zend_objects_store_put(obj,
		(zend_objects_store_dtor_t )zend_objects_destroy_object,
		(zend_objects_free_object_storage_t )tarantool_object_free,
			NULL TSRMLS_CC);
	retval.handlers = zend_get_std_object_handlers();
	return retval;
}

static int64_t tarantool_step_recv(tarantool_connection *obj, unsigned long sync,
				   zval **header, zval **body TSRMLS_DC) {
	char pack_len[5] = {0, 0, 0, 0, 0};
	*header = NULL;
	*body = NULL;
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server (failed to read length)");
		goto error_con;
	}
	if (php_mp_check(pack_len, 5)) {
		THROW_EXC("Failed verifying msgpack");
		goto error_con;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_POS(obj->value),
				  body_size) != body_size) {
		THROW_EXC("Can't read query from server (failed to read %d "
			  "bytes from server [header + body])", body_size);
		goto error;
	}
	SSTR_LEN(obj->value) += body_size;

	char *pos = SSTR_BEG(obj->value);
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying header [bad msgpack]");
		goto error;
	}
	if (php_mp_unpack(header, &pos) == FAILURE ||
	    Z_TYPE_PP(header) != IS_ARRAY) {
		*header = NULL;
		goto error;
	}
	if (php_mp_check(pos, body_size)) {
		THROW_EXC("Failed verifying body [bad msgpack]");
		goto error_con;
	}
	if (php_mp_unpack(body, &pos) == FAILURE) {
		*body = NULL;
		goto error;
	}

	HashTable *hash = HASH_OF(*header);
	zval **val = NULL;

	if (zend_hash_index_find(hash, TNT_SYNC, (void **)&val) == SUCCESS) {
		if (Z_LVAL_PP(val) != sync) {
			THROW_EXC("request sync is not equal response sync. "
				  "closing connection");
			goto error_con;
		}
	}
	val = NULL;
	if (zend_hash_index_find(hash, TNT_CODE, (void **)&val) == SUCCESS) {
		if (Z_LVAL_PP(val) == TNT_OK) {
			SSTR_LEN(obj->value) = 0;
			smart_string_nullify(obj->value);
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
error_con:
	tarantool_stream_close(obj);
	obj->stream = NULL;
error:
	if (*header) zval_ptr_dtor(header);
	if (*body) zval_ptr_dtor(body);
	SSTR_LEN(obj->value) = 0;
	smart_string_nullify(obj->value);
	return FAILURE;
}

// connect, reconnect, flush_schema, close, ping
ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_construct, 0, 0, 0)
	ZEND_ARG_INFO(0, host)
	ZEND_ARG_INFO(0, port)
	ZEND_ARG_INFO(0, login)
	ZEND_ARG_INFO(0, password)
	ZEND_ARG_INFO(0, persistent_id)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_authenticate, 0, 0, 1)
	ZEND_ARG_INFO(0, login)
	ZEND_ARG_INFO(0, password)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_select, 0, 0, 1)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, index)
	ZEND_ARG_INFO(0, limit)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, iterator)
ZEND_END_ARG_INFO()

// insert, replace
ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_space_tuple, 0, 0, 2)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_ARRAY_INFO(0, tuple, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_delete, 0, 0, 2)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, index)
ZEND_END_ARG_INFO()

// call, eval
ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_proc_tuple, 0, 0, 1)
	ZEND_ARG_INFO(0, proc)
	ZEND_ARG_INFO(0, tuple)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_update, 0, 0, 3)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_ARRAY_INFO(0, args, 0)
	ZEND_ARG_INFO(0, index)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_upsert, 0, 0, 3)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_ARRAY_INFO(0, tuple, 0)
	ZEND_ARG_ARRAY_INFO(0, args, 0)
ZEND_END_ARG_INFO()

#define TNT_MEP(name, args) PHP_ME(Tarantool, name, args, ZEND_ACC_PUBLIC)
#define TNT_MAP(alias, name, args) PHP_MALIAS(Tarantool, alias, name, args, ZEND_ACC_PUBLIC)
const zend_function_entry Tarantool_methods[] = {
	TNT_MEP(__construct,  arginfo_tarantool_construct)
	TNT_MEP(connect,      arginfo_tarantool_void)
	TNT_MEP(reconnect,    arginfo_tarantool_void)
	TNT_MEP(close,        arginfo_tarantool_void)
	TNT_MEP(flush_schema, arginfo_tarantool_void)
	TNT_MEP(authenticate, arginfo_tarantool_authenticate)
	TNT_MEP(ping,         arginfo_tarantool_void)
	TNT_MEP(select,       arginfo_tarantool_select)
	TNT_MEP(insert,       arginfo_tarantool_space_tuple)
	TNT_MEP(replace,      arginfo_tarantool_space_tuple)
	TNT_MEP(call,         arginfo_tarantool_proc_tuple)
	TNT_MEP(eval,         arginfo_tarantool_proc_tuple)
	TNT_MEP(delete,       arginfo_tarantool_delete)
	TNT_MEP(update,       arginfo_tarantool_update)
	TNT_MEP(upsert,       arginfo_tarantool_upsert)
	TNT_MAP(evaluate,     eval,         arginfo_tarantool_proc_tuple)
	TNT_MAP(flushSchema,  flush_schema, arginfo_tarantool_void)
	TNT_MAP(disconnect,   close,        arginfo_tarantool_void)
	{NULL, NULL, NULL}
};
#undef TNT_MEP
#undef TNT_MAP

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
		goto cleanup;
	}
	if (zend_hash_find(ht, "field", 6, (void **)&oppos) == FAILURE ||
			!oppos || Z_TYPE_PP(oppos) != IS_LONG) {
		THROW_EXC("Field FIELD must be provided and must be LONG at "
				"position %d", position);
		goto cleanup;
	}
	zval **oparg, **splice_len, **splice_val;
	switch(Z_STRVAL_PP(opstr)[0]) {
	case ':':
		if (n != 5) {
			THROW_EXC("Five fields must be provided for splice"
					" at position %d", position);
			goto cleanup;
		}
		if (zend_hash_find(ht, "offset", 7,
				(void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			THROW_EXC("Field OFFSET must be provided and must be LONG for "
					"splice at position %d", position);
			goto cleanup;
		}
		if (zend_hash_find(ht, "length", 7, (void **)&splice_len) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_len) != IS_LONG) {
			THROW_EXC("Field LENGTH must be provided and must be LONG for "
					"splice at position %d", position);
			goto cleanup;
		}
		if (zend_hash_find(ht, "list", 5, (void **)&splice_val) == FAILURE ||
				!oparg || Z_TYPE_PP(splice_val) != IS_STRING) {
			THROW_EXC("Field LIST must be provided and must be STRING for "
					"splice at position %d", position);
			goto cleanup;
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
			goto cleanup;
		}
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || Z_TYPE_PP(oparg) != IS_LONG) {
			THROW_EXC("Field ARG must be provided and must be LONG for "
					"'%s' at position %d", Z_STRVAL_PP(opstr), position);
			goto cleanup;
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
			goto cleanup;
		}
		if (zend_hash_find(ht, "arg", 4, (void **)&oparg) == FAILURE ||
				!oparg || !PHP_MP_SERIALIZABLE_PP(oparg)) {
			THROW_EXC("Field ARG must be provided and must be SERIALIZABLE for "
					"'%s' at position %d", Z_STRVAL_PP(opstr), position);
			goto cleanup;
		}
		add_next_index_stringl(arr, Z_STRVAL_PP(opstr), 1, 1);
		add_next_index_long(arr, Z_LVAL_PP(oppos));
		SEPARATE_ZVAL_TO_MAKE_IS_REF(oparg);
		add_next_index_zval(arr, *oparg);
		break;
	default:
		THROW_EXC("Unknown operation '%s' at position %d",
				Z_STRVAL_PP(opstr), position);
		goto cleanup;
	}
	return arr;
cleanup:
	zval_ptr_dtor(&arr);
	return NULL;
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
			if (op_arr)
				zval_ptr_dtor(&op_arr);
			goto cleanup;
		}
	}
	return arr;
cleanup:
	zval_ptr_dtor(&arr);
	return NULL;
}

int get_spaceno_by_name(tarantool_connection *obj, zval *id, zval *name TSRMLS_DC) {
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
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server (failed to read length)");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c,
				body_size) != body_size) {
		THROW_EXC("Can't read query from server (failed to read %d "
			  "bytes from server [header + body])", body_size);
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

int get_indexno_by_name(tarantool_connection *obj, zval *id,
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
	if (tarantool_stream_read(obj, pack_len, 5) != 5) {
		THROW_EXC("Can't read query from server (failed to read length)");
		return FAILURE;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c,
				body_size) != body_size) {
		THROW_EXC("Can't read query from server (failed to read %d "
			  "bytes from server [header + body])", body_size);
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

zend_class_entry *Tarantool_ptr;

PHP_RINIT_FUNCTION(tarantool) {
	return SUCCESS;
}

static void
php_tarantool_init_globals(zend_tarantool_globals *tarantool_globals) {
	tarantool_globals->sync_counter    = 0;
	tarantool_globals->retry_count     = 1;
	tarantool_globals->retry_sleep     = 0.1;
	tarantool_globals->timeout         = 10.0;
	tarantool_globals->request_timeout = 10.0;
}

ZEND_RSRC_DTOR_FUNC(php_tarantool_dtor)
{
	if (rsrc->ptr) {
		tarantool_connection *obj = (tarantool_connection *)rsrc->ptr;
		tarantool_connection_free(obj, 1 TSRMLS_CC);
		rsrc->ptr = NULL;
		// tarantool_connection_free((tarantool_connection *)rsrc->ptr, 1);
		/* Free tarantool_obj here (in rsrc->ptr) */
	}
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_ce(void)
{
	return tarantool_ce;
}

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_exception(void)
{
	return tarantool_exception_ce;
}

PHP_MINIT_FUNCTION(tarantool) {
	/* Init global variables */
	ZEND_INIT_MODULE_GLOBALS(tarantool, php_tarantool_init_globals, NULL);
	REGISTER_INI_ENTRIES();

	#define RLCI(NAME) REGISTER_LONG_CONSTANT("TARANTOOL_ITER_" # NAME,	\
						  ITERATOR_ ## NAME,		\
						  CONST_CS | CONST_PERSISTENT)

	/* Register constants: DEPRECATED */
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

	#undef RLCI

	le_tarantool = zend_register_list_destructors_ex(NULL, php_tarantool_dtor,
			"Tarantool persistent connection", module_number);

	/* Init class entries */
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Tarantool", Tarantool_methods);
	tarantool_ce = zend_register_internal_class(&ce TSRMLS_CC);
	tarantool_ce->create_object = tarantool_create;

	#define REGISTER_TNT_CLASS_CONST_LONG(NAME)				\
		zend_declare_class_constant_long(php_tarantool_get_ce(),	\
				ZEND_STRS( #NAME ) - 1, NAME TSRMLS_CC)

	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_EQ);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_REQ);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_ALL);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_LT);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_LE);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_GE);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_GT);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ALL_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ANY_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ALL_NOT_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_OVERLAPS);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_NEIGHBOR);

	#undef REGISTER_TNT_CLASS_CONST_LONG

	INIT_CLASS_ENTRY(ce, "TarantoolException", NULL);
	tarantool_exception_ce = zend_register_internal_class_ex(&ce,
			php_tarantool_get_exception_base(0 TSRMLS_CC),
			NULL TSRMLS_CC);

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

static int php_tarantool_list_entry() {
	return le_tarantool;
}

PHP_METHOD(Tarantool, __construct) {
	char *host = NULL, *login = NULL, *passwd = NULL;
	int host_len = 0, login_len = 0, passwd_len = 0;
	long port = 0;
	int is_persistent = 0, plist_new_entry = 1;

	const char *plist_id = NULL, *suffix = NULL;
	int plist_id_len = 0, suffix_len = 0;

	TARANTOOL_PARSE_PARAMS(id, "|slsss", &host, &host_len, &port,
			       &login, &login_len, &passwd, &passwd_len,
			       &suffix, &suffix_len);
	TARANTOOL_FETCH_OBJECT(obj, id);

	if (host == NULL) {
		host = "localhost";
	}

	if (port < 0 || port >= 65536) {
		THROW_EXC("Invalid primary port value: %li", port);
		RETURN_FALSE;
	} else if (port == 0) {
		port = 3301;
	}
	if (login == NULL) {
		login = "guest";
	}
	if (passwd != NULL && passwd_len == 0) {
		passwd = NULL;
	}

	/* Not sure how persistency and ZTS are combined*/
	/* #ifndef   ZTS */
	/* Do not allow not persistent connections, for now */
	is_persistent = (TARANTOOL_G(persistent) || suffix ? 1 : 0);
	/* #endif *//* ZTS */

	if (is_persistent) {
		zend_rsrc_list_entry *le = NULL;

		plist_id = persistent_id(host, port, login, "plist",
					 &plist_id_len, suffix, suffix_len);

		if (TARANTOOL_PERSISTENT_FIND(plist_id, plist_id_len, le) ==
		    SUCCESS) {
			/* It's unlikely */
			if (le->type == php_tarantool_list_entry()) {
				obj = (struct tarantool_connection *) le->ptr;
				plist_new_entry = 0;
			}
		}
		t_obj->obj = obj;
	}

	if (obj == NULL) {
		obj = pecalloc(1, sizeof(tarantool_connection),
				 is_persistent);
		if (obj == NULL) {
			if (plist_id) {
				pefree((void *)plist_id, 1);
				plist_id = NULL;
			}
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "out of "
					 "memory: cannot allocate handle");
		}

		/* initialzie object structure */
		obj->host = pestrdup(host, is_persistent);
		obj->port = port;
		obj->value = (smart_string *)pecalloc(1,sizeof(smart_string),1);
		/* CHECK obj->value */
		memset(obj->value, 0, sizeof(smart_string));
		smart_string_ensure(obj->value, GREETING_SIZE);
		obj->greeting = (char *)pecalloc(GREETING_SIZE, sizeof(char),
						 is_persistent);
		/* CHECK obj->greeting */
		obj->salt = obj->greeting + SALT_PREFIX_SIZE;
		obj->login = pestrdup(login, is_persistent);
		obj->orig_login = pestrdup(login, is_persistent);
		/* If passwd == NULL, then authenticate without password */
		if (passwd) {
			obj->passwd = pestrdup(passwd, is_persistent);
		}
		if (is_persistent) {
			obj->persistent_id = persistent_id(host, port, login,
							   "stream", NULL,
							   suffix, suffix_len);
		}
		obj->schema = tarantool_schema_new(is_persistent);
		/* CHECK obj->schema */
		obj->tps = tarantool_tp_new(obj->value, is_persistent);
		/* CHECK obj->tps */
	}

	if (is_persistent && plist_new_entry) {
		zend_rsrc_list_entry le;
		memset(&le, 0, sizeof(zend_rsrc_list_entry));

		le.type = php_tarantool_list_entry();
		le.ptr  = obj;
		if (zend_hash_update(&EG(persistent_list), plist_id,
				     plist_id_len - 1, (void *)&le,
				     sizeof(zend_rsrc_list_entry),
				     NULL) == FAILURE) {
			if (plist_id) {
				pefree((void *)plist_id, 1);
				plist_id = NULL;
			}
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "could not "
					 "register persistent entry");
		}
	}
	t_obj->obj = obj;
	t_obj->is_persistent = is_persistent;

	if (plist_id) {
		pefree((void *)plist_id, 1);
	}
	return;
}

PHP_METHOD(Tarantool, connect) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	if (obj->stream && obj->stream->mode) RETURN_TRUE;
	if (__tarantool_connect(t_obj, id TSRMLS_CC) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

PHP_METHOD(Tarantool, reconnect) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);
	if (__tarantool_reconnect(t_obj, id TSRMLS_CC) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

int __tarantool_authenticate(tarantool_connection *obj) {
	TSRMLS_FETCH();

	tarantool_schema_flush(obj->schema);
	tarantool_tp_update(obj->tps);
	int batch_count = 3;
	size_t passwd_len = (obj->passwd ? strlen(obj->passwd) : 0);
	tp_auth(obj->tps, obj->salt, obj->login, strlen(obj->login),
		obj->passwd, passwd_len);
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
		if (tarantool_stream_read(obj, pack_len, 5) != 5) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		size_t body_size = php_mp_unpack_package_size(pack_len);
		smart_string_ensure(obj->value, body_size);
		if (tarantool_stream_read(obj, obj->value->c,
					body_size) != body_size) {
			THROW_EXC("Can't read query from server");
			return FAILURE;
		}
		if (status == FAILURE) continue;
		struct tnt_response resp;
		memset(&resp, 0, sizeof(struct tnt_response));
		if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
			THROW_EXC("Failed to parse query");
			status = FAILURE;
		}

		if (resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code,
				  resp.error_len, resp.error);
			status = FAILURE;
		}
		if (resp.sync == space_sync) {
			if (tarantool_schema_add_spaces(obj->schema, resp.data,
						        resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (space) or "
					  "memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == index_sync) {
			if (tarantool_schema_add_indexes(obj->schema, resp.data,
							 resp.data_len) &&
					status != FAILURE) {
				THROW_EXC("Failed parsing schema (index) or "
					  "memory issues");
				status = FAILURE;
			}
		} else if (resp.sync == auth_sync && resp.error) {
			THROW_EXC("Query error %d: %.*s", resp.code,
				  resp.error_len, resp.error);
			status = FAILURE;
		}
	}

	return status;
}

PHP_METHOD(Tarantool, authenticate) {
	char *login; int login_len;
	char *passwd = NULL; int passwd_len = 0;

	TARANTOOL_PARSE_PARAMS(id, "s|s!", &login, &login_len,
			&passwd, &passwd_len);
	TARANTOOL_FETCH_OBJECT(obj, id);
	if (obj->login != NULL) {
		pefree(obj->login, t_obj->is_persistent);
		obj->login = NULL;
	}
	obj->login = pestrdup(login, t_obj->is_persistent);
	if (obj->passwd != NULL) {
		pefree(obj->passwd, t_obj->is_persistent);
		obj->passwd = NULL;
	}
	if (passwd != NULL) {
		obj->passwd = pestrdup(passwd, t_obj->is_persistent);
	}
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	__tarantool_authenticate(obj);
	RETURN_NULL();
}

PHP_METHOD(Tarantool, flush_schema) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	tarantool_schema_flush(obj->schema);
	RETURN_TRUE;
}

PHP_METHOD(Tarantool, close) {
	TARANTOOL_PARSE_PARAMS(id, "", id);
	TARANTOOL_FETCH_OBJECT(obj, id);

	RETURN_TRUE;
}

PHP_METHOD(Tarantool, ping) {
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

PHP_METHOD(Tarantool, select) {
	zval *space = NULL, *index = NULL;
	zval *key = NULL, *key_new = NULL;
	zval *zlimit = NULL;
	long limit = LONG_MAX-1, offset = 0, iterator = 0;

	TARANTOOL_PARSE_PARAMS(id, "z|zzzll", &space, &key,
			&index, &zlimit, &offset, &iterator);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	if (zlimit != NULL && Z_TYPE_P(zlimit) != IS_NULL && Z_TYPE_P(zlimit) != IS_LONG) {
		THROW_EXC("wrong type of 'limit' - expected long/null, got '%s'",
				zend_zval_type_name(zlimit));
		RETURN_FALSE;
	} else if (zlimit != NULL && Z_TYPE_P(zlimit) == IS_LONG) {
		limit = Z_LVAL_P(zlimit);
	}

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

PHP_METHOD(Tarantool, insert) {
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

PHP_METHOD(Tarantool, replace) {
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

PHP_METHOD(Tarantool, delete) {
	zval *space = NULL, *key = NULL, *index = NULL;
	zval *key_new = NULL;

	TARANTOOL_PARSE_PARAMS(id, "zz|z", &space, &key, &index);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index TSRMLS_CC);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	key_new = pack_key(key, 0);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_delete(obj->value, sync, space_no, index_no, key_new);
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

PHP_METHOD(Tarantool, call) {
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

PHP_METHOD(Tarantool, eval) {
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

PHP_METHOD(Tarantool, update) {
	zval *space = NULL, *key = NULL, *index = NULL, *args = NULL;
	zval *key_new = NULL;

	TARANTOOL_PARSE_PARAMS(id, "zza|z", &space, &key, &args, &index);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, id, space_no, index TSRMLS_CC);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	args = tarantool_update_verify_args(args TSRMLS_CC);
	if (!args) RETURN_FALSE;
	key_new = pack_key(key, 0);
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_update(obj->value, sync, space_no, index_no, key_new, args);
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

PHP_METHOD(Tarantool, upsert) {
	zval *space = NULL, *tuple = NULL, *args = NULL;

	TARANTOOL_PARSE_PARAMS(id, "zaa", &space, &tuple, &args);
	TARANTOOL_FETCH_OBJECT(obj, id);
	TARANTOOL_CONNECT_ON_DEMAND(obj, id);

	long space_no = get_spaceno_by_name(obj, id, space TSRMLS_CC);
	if (space_no == FAILURE) RETURN_FALSE;

	args = tarantool_update_verify_args(args TSRMLS_CC);
	if (!args) RETURN_FALSE;
	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_upsert(obj->value, sync, space_no, tuple, args);
	if (tarantool_stream_send(obj TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	zval *header, *body;
	if (tarantool_step_recv(obj, sync, &header, &body TSRMLS_CC) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(body, header, body);
}
