#include <time.h>
#include <stdio.h>
#include <limits.h>

#include "php_tarantool.h"
#include "tarantool_internal.h"

#include "tarantool_tp.h"
#include "tarantool_proto.h"
#include "tarantool_schema.h"
#include "tarantool_msgpack.h"
#include "tarantool_network.h"
#include "tarantool_exception.h"

#include "utils.h"

static int __tarantool_authenticate(tarantool_connection *obj);
static void tarantool_stream_close(tarantool_connection *obj);

double
now_gettimeofday(void)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec * 1e9 + t.tv_usec * 1e3;
}

ZEND_DECLARE_MODULE_GLOBALS(tarantool)

static zend_class_entry *Tarantool_ptr;

PHP_TARANTOOL_API
zend_class_entry *php_tarantool_get_ce(void)
{
	return Tarantool_ptr;
}

static int le_tarantool = 0;

#define TARANTOOL_PARSE_PARAMS(ID, FORMAT, ...)				\
	if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(),	\
				"O" FORMAT, &ID, Tarantool_ptr,		\
				##__VA_ARGS__) == FAILURE) {		\
		RETURN_FALSE;						\
	}

static inline tarantool_object *php_tarantool_object(zend_object *obj) {
	return (tarantool_object *)(
		(char *)(obj) - XtOffsetOf(tarantool_object, zo)
	);
}

#define TARANTOOL_FETCH_OBJECT(NAME)						\
	tarantool_object *t_##NAME = php_tarantool_object(Z_OBJ_P(getThis()));	\
	tarantool_connection *obj  = t_##NAME->obj;

#define TARANTOOL_FUNCTION_BEGIN(NAME, ID, FORMAT, ...)			\
	zval *ID;							\
	TARANTOOL_PARSE_PARAMS(ID, FORMAT, ##__VA_ARGS__);		\
	TARANTOOL_FETCH_OBJECT(NAME);

#define TARANTOOL_CONNECT_ON_DEMAND(CON)				\
	if (!CON->stream)						\
		if (__tarantool_connect(t_##CON) == FAILURE)		\
			RETURN_FALSE;					\
	if (CON->stream && php_stream_eof(CON->stream) != 0)		\
		if (__tarantool_reconnect(t_##CON) == FAILURE)		\
			RETURN_FALSE;

#define TARANTOOL_RETURN_DATA(HT, HEAD, BODY)				\
	HashTable *ht = HASH_OF(HT);					\
	zval *answer;							\
	answer = zend_hash_index_find(ht, TNT_DATA);			\
	if (!answer) {							\
		THROW_EXC("No field DATA in body");			\
		zval_ptr_dtor(HEAD);					\
		zval_ptr_dtor(BODY);					\
		RETURN_FALSE;						\
	}								\
	RETVAL_ZVAL(answer, 1, 0);					\
	zval_ptr_dtor(HEAD);						\
	zval_ptr_dtor(BODY);						\
	return;

zend_object_handlers tarantool_obj_handlers;

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
	STD_PHP_INI_BOOLEAN("tarantool.persistent", "0", PHP_INI_ALL,
			    OnUpdateBool, persistent, zend_tarantool_globals,
			    tarantool_globals)
	STD_PHP_INI_BOOLEAN("tarantool.use_namespace", "0", PHP_INI_SYSTEM,
			    OnUpdateBool, use_namespace,
			    zend_tarantool_globals, tarantool_globals)
	STD_PHP_INI_BOOLEAN("tarantool.connection_alias", "0", PHP_INI_SYSTEM,
			    OnUpdateBool, connection_alias,
			    zend_tarantool_globals, tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.timeout", "3600.0", PHP_INI_ALL,
			  OnUpdateReal, timeout, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.request_timeout", "3600.0", PHP_INI_ALL,
			  OnUpdateReal, request_timeout, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.retry_count", "1", PHP_INI_ALL,
			  OnUpdateLong, retry_count, zend_tarantool_globals,
			  tarantool_globals)
	STD_PHP_INI_ENTRY("tarantool.retry_sleep", "10", PHP_INI_ALL,
			  OnUpdateReal, retry_sleep, zend_tarantool_globals,
			  tarantool_globals)
PHP_INI_END()

#ifdef COMPILE_DL_TARANTOOL
ZEND_GET_MODULE(tarantool)
#endif

inline static int
tarantool_stream_send(tarantool_connection *obj TSRMLS_DC) {
	int rv = tntll_stream_send(obj->stream, SSTR_BEG(obj->value),
				   SSTR_LEN(obj->value));
	if (rv < 0) {
		tarantool_stream_close(obj);
		tarantool_throw_ioexception("Failed to send message");
		return FAILURE;
	}
	SSTR_LEN(obj->value) = 0;
	smart_string_nullify(obj->value);
	return SUCCESS;
}

/*
 * Generate persistent_id for connection.
 * Must be freed by efree()/pefree(ptr, 0)
 */
static char *pid_gen(const char *host, int port, const char *login,
		     const char *prefix, size_t *olen,
		     const char *suffix, size_t suffix_len) {
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
	if (olen) *olen = len;
	return plist_id;
}

/*
 * Generate persistent string with persistent_id for connection.
 * Must be freed by pefree(ptr, 1)
 */
static char *pid_pgen(const char *host, int port, const char *login,
		      const char *prefix, size_t *olen,
		      const char *suffix, size_t suffix_len) {
	char *plist_id = NULL, *tmp = NULL;
	size_t len = 0;
	plist_id = pid_gen(host, port, login, prefix, &len, suffix, suffix_len);
	tmp = pestrdup(plist_id, 1);
	efree(plist_id);
	if (olen) *olen = len;
	return tmp;
}

/*
 * Generate zend_string with persistent_id for connection.
 * Must be freed using zend_string_release(<ptr>);
 */
static zend_string *pid_zsgen(const char *host, int port, const char *login,
			      const char *prefix, const char *suffix,
			      size_t suffix_len) {
	size_t len = 0;
	const char *plist_id = NULL;
	plist_id = pid_gen(host, port, login, prefix, &len, suffix, suffix_len);
	if (plist_id == NULL || len == 0)
		return NULL;
	zend_string *out = zend_string_init(plist_id, len - 1, 0);
	efree((void *)plist_id);
	return out;
}

/*
 * Generate persistent zend_string with persistent_id for connection.
 * Must be freed using zend_string_release(<ptr>);
 */
static zend_string *pid_pzsgen(const char *host, int port, const char *login,
			       const char *prefix, const char *suffix,
			       size_t suffix_len) {
	size_t len = 0;
	const char *plist_id = NULL;
	plist_id = pid_gen(host, port, login, prefix, &len, suffix, suffix_len);
	if (plist_id == NULL || len == 0)
		return NULL;
	zend_string *out = zend_string_init(plist_id, len - 1, 1);
	efree((void *)plist_id);
	return out;

}

/*
 * Legacy rtsisyk code, php_stream_read made right
 * See https://bugs.launchpad.net/tarantool/+bug/1182474
 */
inline static int
tarantool_stream_read(tarantool_connection *obj, char *buf, size_t size) {
	size_t got = tntll_stream_read2(obj->stream, buf, size);
	const char *suffix = "";
	if (got == 0 && tntll_stream_is_timedout())
		suffix = " (request timeout reached)";
	char errno_suffix[256] = {0};
	if (got != size) {
		tarantool_throw_ioexception("Failed to read %ld bytes %s",
					    size, suffix);
		tarantool_stream_close(obj);
		return FAILURE;
	}
	return SUCCESS;
}

static void
tarantool_stream_close(tarantool_connection *obj) {
	if (obj->stream || obj->persistent_id)
		tntll_stream_close(obj->stream, obj->persistent_id);
	obj->stream = NULL;
	if (obj->persistent_id != NULL) {
		zend_string_release(obj->persistent_id);
		obj->persistent_id = NULL;
	}
}

static int __tarantool_connect(tarantool_object *t_obj) {
	TSRMLS_FETCH();
	tarantool_connection *obj = t_obj->obj;
	int status = SUCCESS;
	long count = TARANTOOL_G(retry_count);
	struct timespec sleep_time = {0};
	double_to_ts(INI_FLT("retry_sleep"), &sleep_time);
	char *err = NULL;

	if (t_obj->is_persistent) {
		if (!obj->persistent_id)
			obj->persistent_id = pid_pzsgen(obj->host, obj->port,
							obj->orig_login,
							"stream", obj->suffix,
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
				zend_string_release(obj->persistent_id);
			obj->persistent_id = pid_pzsgen(obj->host, obj->port,
							obj->orig_login,
							"stream", obj->suffix,
							obj->suffix_len);

		}
		if (tntll_stream_open(obj->host, obj->port, obj->persistent_id,
				      &obj->stream, &err) == -1)
			continue;
		if (tntll_stream_read2(obj->stream, obj->greeting,
				       GREETING_SIZE) != GREETING_SIZE) {
			continue;
		}
		if (php_tp_verify_greetings(obj->greeting) == 0) {
			spprintf(&err, 0, "Bad greetings");
			goto ioexception;
		}
		++count;
		break;
	}
	if (count == 0) {
ioexception:
		// raise (SIGABRT);
		tarantool_throw_ioexception("%s", err);
		efree(err);
		return FAILURE;
	}
	if (obj->login != NULL && obj->passwd != NULL) {
		status = __tarantool_authenticate(obj);
	}
	return status;
}

inline static int __tarantool_reconnect(tarantool_object *t_obj) {
	tarantool_stream_close(t_obj->obj);
	return __tarantool_connect(t_obj);
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
		zend_string_release(obj->persistent_id);
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
		tarantool_connection_free(obj->obj,
					  obj->is_persistent
					  TSRMLS_CC);
		obj->obj = NULL;
	}
	zend_object_std_dtor(&obj->zo);
}

static void
tarantool_free(zend_object *zo) {
	tarantool_object *t_obj = php_tarantool_object(zo);
	tarantool_object_free(t_obj);
}

static zend_object *tarantool_create(zend_class_entry *entry) {
	tarantool_object *obj = NULL;

	obj = (tarantool_object *)pecalloc(1, sizeof(tarantool_object) +
		sizeof(zval) * (entry->default_properties_count - 1), 0);
	zend_object_std_init(&obj->zo, entry);
	obj->zo.handlers = &tarantool_obj_handlers;

	return &obj->zo;
}

static int tarantool_step_recv(
		tarantool_connection *obj,
		unsigned long sync,
		zval *header,
		zval *body) {
	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) == FAILURE) {
		header = NULL;
		body   = NULL;
		goto error_con;
	}
	if (php_mp_check(pack_len, 5)) {
		header = NULL;
		body   = NULL;
		tarantool_throw_parsingexception("package length");
		goto error_con;
	}
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, SSTR_POS(obj->value),
				  body_size) == FAILURE) {
		header = NULL;
		body   = NULL;
		goto error_con;
	}
	SSTR_LEN(obj->value) += body_size;

	char *pos = SSTR_BEG(obj->value);
	if (php_mp_check(pos, body_size)) {
		header = NULL;
		body   = NULL;
		tarantool_throw_parsingexception("package header");
		goto error_con;
	}
	if (php_mp_unpack(header, &pos) == FAILURE ||
	    Z_TYPE_P(header) != IS_ARRAY) {
		header = NULL;
		body   = NULL;
		goto error_con;
	}
	if (php_mp_check(pos, body_size)) {
		header = NULL;
		body   = NULL;
		tarantool_throw_parsingexception("package body");
		goto error_con;
	}
	if (php_mp_unpack(body, &pos) == FAILURE) {
		header = NULL;
		body   = NULL;
		goto error_con;
	}

	HashTable *hash = HASH_OF(header);
	zval *val;

	val = zend_hash_index_find(hash, TNT_SYNC);
	if (val) {
		if (Z_LVAL_P(val) != sync) {
			THROW_EXC("request sync is not equal response sync. "
				  "closing connection");
			goto error_con;
		}
	}
	val = zend_hash_index_find(hash, TNT_CODE);
	if (val) {
		if (Z_LVAL_P(val) == TNT_OK) {
			SSTR_LEN(obj->value) = 0;
			smart_string_nullify(obj->value);
			return SUCCESS;
		}
		HashTable *hash = HASH_OF(body);
		zval *z_error_str;
		long errcode = Z_LVAL_P(val) & ((1 << 15) - 1 );

		const char *error_str = NULL;
		size_t error_str_len = 0;
		z_error_str = zend_hash_index_find(hash, TNT_ERROR);
		if (z_error_str) {
			if (Z_TYPE_P(z_error_str) == IS_STRING) {
				error_str     = Z_STRVAL_P(z_error_str);
				error_str_len = Z_STRLEN_P(z_error_str);
			} else {
				tarantool_throw_exception(
						"Bad error field type. Expected"
						" STRING, got %s",
						tutils_op_to_string(z_error_str));
				goto error_con;
			}
		} else {
			error_str = "empty";
			error_str_len = sizeof(error_str);
		}
		tarantool_throw_clienterror(errcode, error_str,
					    error_str_len);
		goto error;
	}
	tarantool_throw_exception("Failed to retrieve answer code");
error_con:
	tarantool_stream_close(obj);
error:
	if (header) zval_ptr_dtor(header);
	if (body) zval_ptr_dtor(body);
	SSTR_LEN(obj->value) = 0;
	smart_string_nullify(obj->value);
	return FAILURE;
}

/* connect, reconnect, flush_schema, close, ping */
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

/* insert, replace */
ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_space_tuple, 0, 0, 2)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_ARRAY_INFO(0, tuple, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_tarantool_delete, 0, 0, 2)
	ZEND_ARG_INFO(0, space)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, index)
ZEND_END_ARG_INFO()

/* call, eval */
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

#define TNT_MEP(name, args)        PHP_ME    (Tarantool,        name, args, ZEND_ACC_PUBLIC)
#define TNT_MAP(alias, name, args) PHP_MALIAS(Tarantool, alias, name, args, ZEND_ACC_PUBLIC)
const zend_function_entry Tarantool_methods[] = {
	TNT_MEP(__construct,                arginfo_tarantool_construct    )
	TNT_MEP(connect,                    arginfo_tarantool_void         )
	TNT_MEP(reconnect,                  arginfo_tarantool_void         )
	TNT_MEP(close,                      arginfo_tarantool_void         )
	TNT_MEP(flush_schema,               arginfo_tarantool_void         )
	TNT_MEP(authenticate,               arginfo_tarantool_authenticate )
	TNT_MEP(ping,                       arginfo_tarantool_void         )
	TNT_MEP(select,                     arginfo_tarantool_select       )
	TNT_MEP(insert,                     arginfo_tarantool_space_tuple  )
	TNT_MEP(replace,                    arginfo_tarantool_space_tuple  )
	TNT_MEP(call,                       arginfo_tarantool_proc_tuple   )
	TNT_MEP(eval,                       arginfo_tarantool_proc_tuple   )
	TNT_MEP(delete,                     arginfo_tarantool_delete       )
	TNT_MEP(update,                     arginfo_tarantool_update       )
	TNT_MEP(upsert,                     arginfo_tarantool_upsert       )
	TNT_MAP(evaluate,     eval,         arginfo_tarantool_proc_tuple   )
	TNT_MAP(flushSchema,  flush_schema, arginfo_tarantool_void         )
	TNT_MAP(disconnect,   close,        arginfo_tarantool_void         )
	{NULL, NULL, NULL}
};
#undef TNT_MEP
#undef TNT_MAP

/* ####################### HELPERS ####################### */

void pack_key(zval *args, char select, zval *arr) {
	if (args && Z_TYPE_P(args) == IS_ARRAY) {
		ZVAL_DUP(arr, args);
		return;
	}
	if (select && (!args || Z_TYPE_P(args) == IS_NULL)) {
		array_init(arr);
		return;
	}
	Z_TRY_ADDREF_P(args);
	array_init(arr);
	add_next_index_zval(arr, args);
}

int convert_iterator(zval *iter, int all) {
	TSRMLS_FETCH();
	if (iter == NULL || Z_TYPE_P(iter) == IS_NULL) {
		if (all) {
			return ITERATOR_ALL;
		} else {
			return ITERATOR_EQ;
		}
	} else if (Z_TYPE_P(iter) == IS_LONG) {
		return Z_LVAL_P(iter);
	} else if (Z_TYPE_P(iter) != IS_STRING) {
		tarantool_throw_exception("Bad iterator type, expected NULL/STR"
					  "ING/LONG, got %s",
					  tutils_op_to_string(iter));
	}
	const char *iter_str = Z_STRVAL_P(iter);
	size_t iter_str_len = Z_STRLEN_P(iter);
	int iter_type = convert_iter_str(iter_str, iter_str_len);
	if (iter_type >= 0)
		return iter_type;
error:
	tarantool_throw_exception("Bad iterator name '%.*s'", iter_str_len,
				  iter_str);
	return -1;
}

int get_spaceno_by_name(tarantool_connection *obj, zval *name) {
	if (Z_TYPE_P(name) == IS_LONG)
		return Z_LVAL_P(name);
	if (Z_TYPE_P(name) != IS_STRING) {
		tarantool_throw_exception("Space ID must be String or Long");
		return FAILURE;
	}
	int32_t space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no != FAILURE)
		return space_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_SPACE, INDEX_SPACE_NAME, 0, 4096);
	tp_key(obj->tps, 1);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) == FAILURE)
		return FAILURE;
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c,
				body_size) == FAILURE)
		return FAILURE;

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
		tarantool_throw_parsingexception("query");
		return FAILURE;
	}

	if (resp.error) {
		tarantool_throw_clienterror(resp.code, resp.error,
					    resp.error_len);
		return FAILURE;
	}

	if (tarantool_schema_add_spaces(obj->schema, resp.data, resp.data_len)) {
		tarantool_throw_parsingexception("schema (space)");
		return FAILURE;
	}
	space_no = tarantool_schema_get_sid_by_string(obj->schema,
			Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (space_no == FAILURE)
		THROW_EXC("No space '%s' defined", Z_STRVAL_P(name));
	return space_no;
}

int get_indexno_by_name(tarantool_connection *obj, int space_no,
			zval *name TSRMLS_DC) {
	if (Z_TYPE_P(name) == IS_NULL) {
		return 0;
	} else if (Z_TYPE_P(name) == IS_LONG) {
		return Z_LVAL_P(name);
	} else if (Z_TYPE_P(name) != IS_STRING) {
		THROW_EXC("Index ID must be String or Long");
		return FAILURE;
	}
	int32_t index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no != FAILURE)
		return index_no;

	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_INDEX, INDEX_INDEX_NAME, 0, 4096);
	tp_key(obj->tps, 2);
	tp_encode_uint(obj->tps, space_no);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) == FAILURE)
		return FAILURE;
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c, body_size) == FAILURE)
		return FAILURE;

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
		tarantool_throw_parsingexception("query");
		return FAILURE;
	}

	if (resp.error) {
		tarantool_throw_clienterror(resp.code, resp.error,
					    resp.error_len);
		return FAILURE;
	}

	if (tarantool_schema_add_indexes(obj->schema, resp.data,
					 resp.data_len) == -1) {
		tarantool_throw_parsingexception("schema (index)");
		return FAILURE;
	}
	index_no = tarantool_schema_get_iid_by_string(obj->schema,
			space_no, Z_STRVAL_P(name), Z_STRLEN_P(name));
	if (index_no == FAILURE)
		THROW_EXC("No index '%s' defined", Z_STRVAL_P(name));
	return index_no;
}

int get_fieldno_by_name(tarantool_connection *obj, uint32_t space_no,
			zval *name) {
	if (Z_TYPE_P(name) == IS_LONG) {
		return Z_LVAL_P(name);
	}

	int fid = tarantool_schema_get_fid_by_string(obj->schema, space_no,
						     Z_STRVAL_P(name),
						     Z_STRLEN_P(name));
	if (fid != FAILURE)
		return fid + 1;
	tarantool_tp_update(obj->tps);
	tp_select(obj->tps, SPACE_SPACE, INDEX_SPACE_NAME, 0, 4096);
	tp_key(obj->tps, 1);
	tp_encode_str(obj->tps, Z_STRVAL_P(name), Z_STRLEN_P(name));
	tp_reqid(obj->tps, TARANTOOL_G(sync_counter)++);

	obj->value->len = tp_used(obj->tps);
	tarantool_tp_flush(obj->tps);

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	char pack_len[5] = {0, 0, 0, 0, 0};
	if (tarantool_stream_read(obj, pack_len, 5) == FAILURE)
		return FAILURE;
	size_t body_size = php_mp_unpack_package_size(pack_len);
	smart_string_ensure(obj->value, body_size);
	if (tarantool_stream_read(obj, obj->value->c, body_size) == FAILURE)
		return FAILURE;

	struct tnt_response resp; memset(&resp, 0, sizeof(struct tnt_response));
	if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
		tarantool_throw_parsingexception("query");
		return FAILURE;
	}

	if (resp.error) {
		tarantool_throw_clienterror(resp.code, resp.error,
					    resp.error_len);
		return FAILURE;
	}

	if (tarantool_schema_add_spaces(obj->schema, resp.data, resp.data_len)) {
		tarantool_throw_parsingexception("schema (space)");
		return FAILURE;
	}
	fid = tarantool_schema_get_fid_by_string(obj->schema, space_no,
						 Z_STRVAL_P(name),
						 Z_STRLEN_P(name));
	if (fid == FAILURE)
		THROW_EXC("No field '%s' defined", Z_STRVAL_P(name));
	return fid + 1;
}

int tarantool_uwrite_op(tarantool_connection *obj, zval *op, uint32_t pos,
			uint32_t space_id) {
	if (Z_TYPE_P(op) != IS_ARRAY || !php_mp_is_hash(op)) {
		THROW_EXC("Op must be MAP at pos %d", pos);
		return 0;
	}
	HashTable *ht = HASH_OF(op);
	size_t n = zend_hash_num_elements(ht);
	zval *opstr, *z_op_pos;
	int op_pos = 0;

	opstr = zend_hash_str_find(ht, "op", strlen("op"));
	z_op_pos = zend_hash_str_find(ht, "field", strlen("field"));
	if (!opstr || Z_TYPE_P(opstr) != IS_STRING || Z_STRLEN_P(opstr) != 1) {
		THROW_EXC("Field OP must be provided and must be STRING with "
			  "length=1 at position %d", pos);
		goto cleanup;
	}
	if (!z_op_pos || (Z_TYPE_P(z_op_pos) != IS_LONG &&
		       Z_TYPE_P(z_op_pos) != IS_STRING)) {
		THROW_EXC("Field FIELD must be provided and must be LONG or "
			  "STRING at position %d", pos);
		goto cleanup;
	}
	op_pos = get_fieldno_by_name(obj, space_id, z_op_pos);
	zval *oparg, *splice_len, *splice_val;
	switch(Z_STRVAL_P(opstr)[0]) {
	case ':':
		oparg      = zend_hash_str_find(ht, "offset", strlen("offset"));
		splice_len = zend_hash_str_find(ht, "length", strlen("length"));
		splice_val = zend_hash_str_find(ht, "list",   strlen("list"));
		if (n != 5) {
			THROW_EXC("Five fields must be provided for splice"
				  " at position %d", pos);
			goto cleanup;
		}
		if (!oparg || Z_TYPE_P(oparg) != IS_LONG) {
			THROW_EXC("Field OFFSET must be provided and must be "
				  "LONG for splice at position %d", pos);
			goto cleanup;
		}
		if (!oparg || Z_TYPE_P(splice_len) != IS_LONG) {
			THROW_EXC("Field LENGTH must be provided and must be "
				  "LONG for splice at position %d", pos);
			goto cleanup;
		}
		if (!oparg || Z_TYPE_P(splice_val) != IS_STRING) {
			THROW_EXC("Field LIST must be provided and must be "
				  "STRING for splice at position %d", pos);
			goto cleanup;
		}
		php_tp_encode_usplice(obj->value, op_pos,
				      Z_LVAL_P(oparg), Z_LVAL_P(splice_len),
				      Z_STRVAL_P(splice_val),
				      Z_STRLEN_P(splice_val));
		break;
	case '+':
	case '-':
	case '&':
	case '|':
	case '^':
		oparg = zend_hash_str_find(ht, "arg", strlen("arg"));
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
				  "position %d", Z_STRVAL_P(opstr), pos);
			goto cleanup;
		}
		if (!oparg || (Z_TYPE_P(oparg) != IS_LONG &&
			       Z_TYPE_P(oparg) != IS_DOUBLE)) {
			THROW_EXC("Field ARG must be provided and must be LONG "
				  "or DOUBLE for '%s' at position %d (got '%s')",
				  Z_STRVAL_P(opstr), pos,
				  tutils_op_to_string(oparg));
			goto cleanup;
		}
		php_tp_encode_uother(obj->value, Z_STRVAL_P(opstr)[0],
				     op_pos, oparg);
		break;
	case '#':
		oparg = zend_hash_str_find(ht, "arg", strlen("arg"));
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
				  "position %d", Z_STRVAL_P(opstr), pos);
			goto cleanup;
		}
		if (!oparg || (Z_TYPE_P(oparg) != IS_LONG)) {
			THROW_EXC("Field ARG must be provided and must be LONG "
				  "for '%s' at position %d (got '%s')",
				  Z_STRVAL_P(opstr), pos,
				  tutils_op_to_string(oparg));
			goto cleanup;
		}
		php_tp_encode_uother(obj->value, Z_STRVAL_P(opstr)[0],
				     op_pos, oparg);
		break;
	case '=':
	case '!':
		oparg = zend_hash_str_find(ht, "arg", strlen("arg"));
		if (n != 3) {
			THROW_EXC("Three fields must be provided for '%s' at "
				  "position %d", Z_STRVAL_P(opstr), pos);
			goto cleanup;
		}
		if (!oparg || !PHP_MP_SERIALIZABLE_P(oparg)) {
			THROW_EXC("Field ARG must be provided and must be "
				  "SERIALIZABLE for '%s' at position %d",
				  Z_STRVAL_P(opstr), pos);
			goto cleanup;
		}
		php_tp_encode_uother(obj->value, Z_STRVAL_P(opstr)[0],
				     op_pos, oparg);
		break;
	default:
		THROW_EXC("Unknown operation '%s' at position %d",
			  Z_STRVAL_P(opstr), pos);
		goto cleanup;

	}
	return 0;
cleanup:
	return -1;
}

int tarantool_uwrite_ops(tarantool_connection *obj, zval *ops,
			 uint32_t space_id) {
	if (Z_TYPE_P(ops) != IS_ARRAY || php_mp_is_hash(ops)) {
		THROW_EXC("Provided value for update OPS must be Array");
		return 0;
	}
	HashTable *ht = HASH_OF(ops);
	size_t n = zend_hash_num_elements(ht);

	php_tp_encode_uheader(obj->value, n);

	size_t key_index = 0;
	for(; key_index < n; ++key_index) {
		zval *op = zend_hash_index_find(ht, key_index);
		if (!op) {
			THROW_EXC("Internal Array Error");
			goto cleanup;
		}
		if (tarantool_uwrite_op(obj, op, key_index, space_id) == -1) {
			goto cleanup;
		}
	}
	return 0;
cleanup:
	return -1;
}
/* ####################### METHODS ####################### */

PHP_RINIT_FUNCTION(tarantool) {
	return SUCCESS;
}

static void php_tarantool_init_globals(zend_tarantool_globals *tarantool_globals) {
	tarantool_globals->sync_counter    = 0;
	tarantool_globals->retry_count     = 1;
	tarantool_globals->retry_sleep     = 10;
	tarantool_globals->timeout         = 3600.0;
	tarantool_globals->request_timeout = 3600.0;
}

static void tarantool_destructor_connection(zend_resource *rsrc TSRMLS_DC) {
	tarantool_connection *obj = (tarantool_connection *)rsrc->ptr;
	if (obj == NULL)
		return;
	tarantool_connection_free(obj, 1 TSRMLS_CC);
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

	le_tarantool = zend_register_list_destructors_ex(
			tarantool_destructor_connection,
			NULL, "Tarantool persistent connection",
			module_number
	);

	/* Init class entries */
	zend_class_entry tarantool_class;

	/* Initialize class entry */
	if (TARANTOOL_G(connection_alias)) {
		TNT_INIT_CLASS_ENTRY(tarantool_class, "Tarantool16",
				     "Tarantool\\Connection16",
				     Tarantool_methods);
	} else {
		TNT_INIT_CLASS_ENTRY(tarantool_class, "Tarantool",
				     "Tarantool\\Connection",
				     Tarantool_methods);
	}
	tarantool_class.create_object = tarantool_create;
	Tarantool_ptr = zend_register_internal_class(&tarantool_class);
	/* Initialize object handlers */
	memcpy(&tarantool_obj_handlers,
	       zend_get_std_object_handlers(),
	       sizeof(zend_object_handlers));
	tarantool_obj_handlers.offset = XtOffsetOf(tarantool_object, zo);
	tarantool_obj_handlers.free_obj = tarantool_free;

	/* Register class constants */
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
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITS_ALL_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ALL_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITS_ANY_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ANY_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITS_ALL_NOT_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_BITSET_ALL_NOT_SET);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_OVERLAPS);
	REGISTER_TNT_CLASS_CONST_LONG(ITERATOR_NEIGHBOR);

	#undef REGISTER_TNT_CLASS_CONST_LONG

	ZEND_MODULE_STARTUP_N(tarantool_exception)(INIT_FUNC_ARGS_PASSTHRU);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(tarantool) {
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MINFO_FUNCTION(tarantool) {
	php_info_print_table_start  ();
	php_info_print_table_header (2, "Tarantool support", "enabled");
	php_info_print_table_row    (2, "Extension version", PHP_TARANTOOL_VERSION);
	php_info_print_table_end    ();
	DISPLAY_INI_ENTRIES();
}

static int php_tarantool_list_entry() {
	return le_tarantool;
}

PHP_METHOD(Tarantool, __construct) {
	/* Input arguments */
	char  *host = NULL, *login = NULL, *passwd = NULL;
	size_t host_len = 0, login_len = 0, passwd_len = 0;
	long   port = 0;

	zend_bool is_persistent = false, plist_new_entry = true;
	const char *suffix = NULL;
	size_t suffix_len = 0;
	zend_string *plist_id = NULL;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "|slss!s", &host, &host_len, &port,
				 &login, &login_len, &passwd, &passwd_len,
				 &suffix, &suffix_len)

	if (host  == NULL)                     host   = "localhost";
	if (port  == 0)                        port   = 3301;
	if (login == NULL)                     login  = "guest";
	if (passwd != NULL && passwd_len == 0) passwd = NULL;

	if (port < 0 || port >= 65536) {
		THROW_EXC("Invalid primary port value: %li", port);
		RETURN_FALSE;
	}

	is_persistent = (TARANTOOL_G(persistent) || suffix ? true : false);

	if (is_persistent) {
		zend_resource *le = NULL;

		plist_id = pid_pzsgen(host, port, login, "plist",
				      suffix, suffix_len);

		le = zend_hash_find_ptr(&EG(persistent_list), plist_id);
		if (le != NULL) {
			/* It's likely */
			if (le->type == php_tarantool_list_entry()) {
				obj = (tarantool_connection *) le->ptr;
				plist_new_entry = false;
				zend_string_release(plist_id);
			}
		}
		t_obj->obj = obj;
	}

	if (obj == NULL) {
		obj = pecalloc(1, sizeof(tarantool_connection), is_persistent);
		if (obj == NULL) {
			if (plist_id) zend_string_release(plist_id);
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "out of "
					 "memory: cannot allocate handle");
		}

		/* initialzie object structure */
		obj->host = pestrdup(host, is_persistent);
		obj->port = port;
		obj->value = (smart_string *)pecalloc(1, sizeof(smart_string), 1);
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
		if (passwd != NULL) {
			obj->passwd = pestrdup(passwd, is_persistent);
		}
		if (is_persistent) {
			obj->persistent_id = pid_pzsgen(host, port, login,
					"stream", suffix, suffix_len);
		}
		obj->schema = tarantool_schema_new(is_persistent);
		/* CHECK obj->schema */
		obj->tps = tarantool_tp_new(obj->value, is_persistent);
		/* CHECK obj->tps */
	}

	if (is_persistent && plist_new_entry) {
		zend_resource le;
		memset(&le, 0, sizeof(zend_resource));
		le.type = php_tarantool_list_entry();
		le.ptr  = obj;
		GC_REFCOUNT(&le) = 1;

		assert(plist_id != NULL);
		if (zend_hash_update_mem(&EG(persistent_list), plist_id,
				(void *)&le, sizeof(zend_resource)) == NULL) {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "could not "
					 "register persistent entry");
		}
		zend_string_release(plist_id);
	}
	t_obj->obj = obj;
	t_obj->is_persistent = is_persistent;
	return;
}

PHP_METHOD(Tarantool, connect) {
	TARANTOOL_FUNCTION_BEGIN(obj, id, "");
	if (obj->stream) RETURN_TRUE;
	if (__tarantool_connect(t_obj) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

PHP_METHOD(Tarantool, reconnect) {
	TARANTOOL_FUNCTION_BEGIN(obj, id, "");
	if (__tarantool_reconnect(t_obj) == FAILURE)
		RETURN_FALSE;
	RETURN_TRUE;
}

static int __tarantool_authenticate(tarantool_connection *obj) {
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

	if (tarantool_stream_send(obj) == FAILURE)
		return FAILURE;

	int status = SUCCESS;

	while (batch_count-- > 0) {
		char pack_len[5] = {0, 0, 0, 0, 0};
		if (tarantool_stream_read(obj, pack_len, 5) == FAILURE)
			return FAILURE;
		size_t body_size = php_mp_unpack_package_size(pack_len);
		smart_string_ensure(obj->value, body_size);
		if (tarantool_stream_read(obj, obj->value->c,
					  body_size) == FAILURE)
			return FAILURE;
		if (status == FAILURE)
			continue;
		struct tnt_response resp;
		memset(&resp, 0, sizeof(struct tnt_response));
		if (php_tp_response(&resp, obj->value->c, body_size) == -1) {
			tarantool_throw_parsingexception("query");
			status = FAILURE;
		}

		if (resp.error) {
			tarantool_throw_clienterror(resp.code, resp.error,
						    resp.error_len);
			status = FAILURE;
		}
		if (status != FAILURE) {
			if (resp.sync == space_sync && tarantool_schema_add_spaces(
					obj->schema,
					resp.data,
					resp.data_len) == -1) {
				tarantool_throw_parsingexception("schema (space)");
				status = FAILURE;
			} else if (resp.sync == index_sync && tarantool_schema_add_indexes(
						obj->schema,
						resp.data,
						resp.data_len) == -1) {
				tarantool_throw_parsingexception("schema (index)");
				status = FAILURE;
			}
		}
	}
	return status;
}

PHP_METHOD(Tarantool, authenticate) {
	const char *login  = NULL; size_t login_len  = 0;
	const char *passwd = NULL; size_t passwd_len = 0;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "s|s", &login, &login_len,
				 &passwd, &passwd_len);
	if (obj->login != NULL)  pefree(obj->login,  t_obj->is_persistent);
	if (obj->passwd != NULL) pefree(obj->passwd, t_obj->is_persistent);
	obj->login = pestrdup(login, t_obj->is_persistent);
	if (passwd != NULL) {
		obj->passwd = pestrdup(passwd, t_obj->is_persistent);
	}
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	__tarantool_authenticate(obj);

	RETURN_NULL();
}

PHP_METHOD(Tarantool, flush_schema) {
	TARANTOOL_FUNCTION_BEGIN(obj, id, "");

	tarantool_schema_flush(obj->schema);

	RETURN_TRUE;
}

PHP_METHOD(Tarantool, close) {
	TARANTOOL_FUNCTION_BEGIN(obj, id, "");

	RETURN_TRUE;
}

PHP_METHOD(Tarantool, ping) {
	TARANTOOL_FUNCTION_BEGIN(obj, id, "");
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_ping(obj->value, sync);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	zval_ptr_dtor(&header);
	zval_ptr_dtor(&body);
	RETURN_TRUE;
}

PHP_METHOD(Tarantool, select) {
	zval *space = NULL, *index = NULL, *key = NULL;
	zval *zlimit = NULL, *ziterator = NULL;
	long limit = LONG_MAX - 1, offset = 0, iterator = 0;
	zval key_new = {0};

	TARANTOOL_FUNCTION_BEGIN(obj, id, "z|zzzlz", &space, &key, &index,
				 &zlimit, &offset, &ziterator);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	if (zlimit != NULL &&
	    Z_TYPE_P(zlimit) != IS_NULL &&
	    Z_TYPE_P(zlimit) != IS_LONG) {
		THROW_EXC("wrong type of 'limit' - expected long/null, got '%s'",
				zend_zval_type_name(zlimit));
		RETURN_FALSE;
	} else if (zlimit != NULL && Z_TYPE_P(zlimit) == IS_LONG) {
		limit = Z_LVAL_P(zlimit);
	}

	/* Obtain space number */
	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE) RETURN_FALSE;

	/* Obtain index number */
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	/* Obtain iterator type */
	/* If
	 * - key == NULL
	 * - type(key) == NULL
	 * - type(key) == ARRAY and count(key) == 0
	 * and type of iterator is not set, then we need to obtain all
	 * tuple from Tarantool's space.
	 */
	int is_iterator_all = (!key ||
		Z_TYPE_P(key) == IS_NULL || (
			Z_TYPE_P(key) == IS_ARRAY &&
			zend_hash_num_elements(Z_ARRVAL_P(key)) == 0
		)
	);
	iterator = convert_iterator(ziterator, is_iterator_all);
	if (iterator == -1)
		RETURN_FALSE;

	pack_key(key, 1, &key_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_select(obj->value, sync, space_no, index_no,
			     limit, offset, iterator, &key_new);
	zval_ptr_dtor(&key_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, insert) {
	zval *space, *tuple;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "za", &space, &tuple);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_INSERT);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, replace) {
	zval *space, *tuple;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "za", &space, &tuple);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE)
		RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_insert_or_replace(obj->value, sync, space_no,
			tuple, TNT_REPLACE);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, delete) {
	zval *space = NULL, *key = NULL, *index = NULL;
	zval key_new = {0};

	TARANTOOL_FUNCTION_BEGIN(obj, id, "zz|z", &space, &key, &index);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	pack_key(key, 0, &key_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_delete(obj->value, sync, space_no, index_no, &key_new);
	zval_ptr_dtor(&key_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, call) {
	char *proc; size_t proc_len;
	zval *tuple = NULL, tuple_new;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "s|z", &proc, &proc_len, &tuple);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	pack_key(tuple, 1, &tuple_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_call(obj->value, sync, proc, proc_len, &tuple_new);
	zval_ptr_dtor(&tuple_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, eval) {
	char *code; size_t code_len;
	zval *tuple = NULL, tuple_new;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "s|z", &code, &code_len, &tuple);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	pack_key(tuple, 1, &tuple_new);

	long sync = TARANTOOL_G(sync_counter)++;
	php_tp_encode_eval(obj->value, sync, code, code_len, &tuple_new);
	zval_ptr_dtor(&tuple_new);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, update) {
	zval *space = NULL, *key = NULL, *index = NULL, *args = NULL;
	zval key_new;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "zza|z", &space, &key, &args, &index);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE) RETURN_FALSE;
	int32_t index_no = 0;
	if (index) {
		index_no = get_indexno_by_name(obj, space_no, index);
		if (index_no == FAILURE) RETURN_FALSE;
	}

	pack_key(key, 0, &key_new);
	long sync = TARANTOOL_G(sync_counter)++;
	/* Remember where we've stopped */
	size_t before_len = SSTR_LEN(obj->value);
	char *sz = php_tp_encode_update(obj->value, sync, space_no,
					index_no, &key_new);
	zval_ptr_dtor(&key_new);
	if (tarantool_uwrite_ops(obj, args, space_no) == -1) {
		/* rollback all written changes */
		SSTR_LEN(obj->value) = before_len;
		RETURN_FALSE;
	}
	php_tp_reencode_length(obj->value, before_len);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}

PHP_METHOD(Tarantool, upsert) {
	zval *space = NULL, *tuple = NULL, *args = NULL;

	TARANTOOL_FUNCTION_BEGIN(obj, id, "zaa", &space, &tuple, &args);
	TARANTOOL_CONNECT_ON_DEMAND(obj);

	long space_no = get_spaceno_by_name(obj, space);
	if (space_no == FAILURE) RETURN_FALSE;

	long sync = TARANTOOL_G(sync_counter)++;
	/* Remember where we've stopped */
	size_t before_len = SSTR_LEN(obj->value);
	char *sz = php_tp_encode_upsert(obj->value, sync,
					space_no, tuple);
	if (tarantool_uwrite_ops(obj, args, space_no) == -1) {
		/* rollback all written changes */
		SSTR_LEN(obj->value) = before_len;
		RETURN_FALSE;
	}
	php_tp_reencode_length(obj->value, before_len);
	if (tarantool_stream_send(obj) == FAILURE)
		RETURN_FALSE;

	zval header, body;
	if (tarantool_step_recv(obj, sync, &header, &body) == FAILURE)
		RETURN_FALSE;

	TARANTOOL_RETURN_DATA(&body, &header, &body);
}
