#include <zend.h>
#include <zend_API.h>

#include "php_tarantool.h"
#include "php_msgpack.h"
#include "third_party/msgpuck.h"

/* Declarations */

void smart_str_ensure(smart_str *str, size_t len) {
	if (str->a > str->len + len)
		return;
	size_t needed = str->a * 2;
	if (str->len + len > needed)
		needed = str->len + len;
	register size_t __n1;
	smart_str_alloc4(str, needed, 0, __n1);
}

void php_mp_pack_nil(smart_str *str) {
	size_t needed = mp_sizeof_nil();
	smart_str_ensure(str, needed);
	mp_encode_nil(str->c + str->len);
	str->len += needed;
}

void php_mp_pack_package_size(smart_str *str, size_t val) {
	size_t needed = 5;
	smart_str_ensure(str, needed);
	*(str->c + str->len) = 0xce;
	*(uint32_t *)(str->c + str->len + 1) = val;
	str->len += needed;
}

size_t php_mp_unpack_package_size(char* buf) {
	return mp_decode_uint((const char **)&buf);
}

void php_mp_pack_long(smart_str *str, long val) {
	size_t needed = mp_sizeof_int(val);
	smart_str_ensure(str, needed);
	mp_encode_int(str->c + str->len, val);
	str->len += needed;
}

void php_mp_pack_double(smart_str *str, double val) {
	size_t needed = mp_sizeof_double(val);
	smart_str_ensure(str, needed);
	mp_encode_double(str->c + str->len, val);
	str->len += needed;
}

void php_mp_pack_bool(smart_str *str, unsigned char val) {
	size_t needed = mp_sizeof_bool(val);
	smart_str_ensure(str, needed);
	mp_encode_bool(str->c + str->len, val);
	str->len += needed;
}

void php_mp_pack_string(smart_str *str, char *c, size_t len) {
	size_t needed = mp_sizeof_str(len);
	smart_str_ensure(str, needed);
	mp_encode_str(str->c + str->len, c, len);
	str->len += needed;
}

void php_mp_pack_hash(smart_str *str, size_t len) {
	size_t needed = mp_sizeof_map(len);
	smart_str_ensure(str, needed);
	mp_encode_map(str->c + str->len, len);
	str->len += needed;
}

void php_mp_pack_array(smart_str *str, size_t len) {
	size_t needed = mp_sizeof_array(len);
	smart_str_ensure(str, needed);
	mp_encode_array(str->c + str->len, len);
	str->len += needed;
}

int php_mp_is_hash(zval *val) {
	HashTable *ht = Z_ARRVAL_P(val);
	int count = zend_hash_num_elements(ht);
	if (count != ht->nNextFreeElement) {
		return 1;
	} else {
		HashPosition pos = {0};
		zend_hash_internal_pointer_reset_ex(ht, &pos);
		int i = 0;
		for (; i < count; ++i) {
			if (zend_hash_get_current_key_type_ex(ht, &pos) != HASH_KEY_IS_LONG)
				return 1;
			zend_hash_move_forward_ex(ht, &pos);
		}
	}
	return 0;
}

void php_mp_pack_array_recursively(smart_str *buf, zval *val) {
	HashTable *ht = HASH_OF(val);
	size_t n = zend_hash_num_elements(ht);

	zval **data;

	php_mp_pack_array(buf, n);
	size_t key_index = 0;
	for (; key_index < n; ++key_index) {
		int status = zend_hash_index_find(ht, key_index, (void *)&(data));
		if (status != SUCCESS || !data || data == &val ||
				(Z_TYPE_PP(data) && Z_ARRVAL_PP(data)->nApplyCount > 1)) {
			php_mp_pack_nil(buf);
		} else {
			if (Z_TYPE_PP(data) == IS_ARRAY)
				Z_ARRVAL_PP(data)->nApplyCount++;
			php_mp_pack(buf, *data);
			if (Z_TYPE_PP(data) == IS_ARRAY)
				Z_ARRVAL_PP(data)->nApplyCount--;
		}
	}
}

void php_mp_pack_hash_recursively(smart_str *buf, zval *val) {
	HashTable *ht = HASH_OF(val);
	size_t n = zend_hash_num_elements(ht);

	char *key;
	uint key_len;
	int key_type;
	ulong key_index;
	zval **data;
	HashPosition pos;

	php_mp_pack_hash(buf, n);
	zend_hash_internal_pointer_reset_ex(ht, &pos);
	for (;; zend_hash_move_forward_ex(ht, &pos)) {
		key_type = zend_hash_get_current_key_ex(
			ht, &key, &key_len, &key_index, 0, &pos);
		if (key_type == HASH_KEY_NON_EXISTENT)
			break;
		switch (key_type) {
		case HASH_KEY_IS_LONG:
			php_mp_pack_long(buf, key_index);
			break;
		case HASH_KEY_IS_STRING:
			php_mp_pack_string(buf, key, key_len - 1);
			break;
		default:
			/* TODO: THROW EXCEPTION */
			php_mp_pack_string(buf, "", strlen(""));
			break;
		}
		int status = zend_hash_get_current_data_ex(ht, (void *)&data, &pos);
		if (status != SUCCESS || !data || data == &val ||
				(Z_TYPE_PP(data) == IS_ARRAY &&
				 Z_ARRVAL_PP(data)->nApplyCount > 1)) {
			php_mp_pack_nil(buf);
		} else {
			if (Z_TYPE_PP(data) == IS_ARRAY)
				Z_ARRVAL_PP(data)->nApplyCount++;
			php_mp_pack(buf, *data TSRMLS_DC);
			if (Z_TYPE_PP(data) == IS_ARRAY)
				Z_ARRVAL_PP(data)->nApplyCount--;
		}
	}
}

void php_mp_pack(smart_str *buf, zval *val TSRMLS_DC) {
	switch(Z_TYPE_P(val)) {
	case IS_NULL:
		php_mp_pack_nil(buf);
		break;
	case IS_LONG:
		php_mp_pack_long(buf, Z_LVAL_P(val));
		break;
	case IS_DOUBLE:
		php_mp_pack_double(buf, (double )Z_DVAL_P(val));
		break;
	case IS_BOOL:
		php_mp_pack_bool(buf, Z_BVAL_P(val));
		break;
	case IS_ARRAY:
		if (php_mp_is_hash(val))
			php_mp_pack_hash_recursively(buf, val);
		else
			php_mp_pack_array_recursively(buf, val);
		break;
	case IS_STRING:
		php_mp_pack_string(buf, Z_STRVAL_P(val), Z_STRLEN_P(val));
		break;
	default:
		/* TODO: THROW EXCEPTION */
		php_mp_pack_nil(buf);
		break;
	}
}

size_t php_mp_check(const char *str, size_t str_size) {
	return mp_check(&str, str + str_size);
}

inline ptrdiff_t php_mp_unpack_nil(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	size_t needed = mp_sizeof_nil();
	mp_decode_nil((const char **)str);
	ZVAL_NULL(*oval);
	str += 1;
	return needed;
}

inline ptrdiff_t php_mp_unpack_uint(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	unsigned long val = mp_decode_uint((const char **)str);
	ZVAL_LONG(*oval, val);
	return mp_sizeof_uint(val);
}

inline ptrdiff_t php_mp_unpack_int(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	long val = mp_decode_int((const char **)str);
	ZVAL_LONG(*oval, val);
	return mp_sizeof_int(val);
}

inline ptrdiff_t php_mp_unpack_str(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	uint32_t len = 0;
	const char *out = mp_decode_str((const char **)str, &len);
	ZVAL_STRINGL(*oval, out, len, 1);
	return mp_sizeof_str(len);
}

inline ptrdiff_t php_mp_unpack_bin(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	uint32_t len = 0;
	const char *out = mp_decode_bin((const char **)str, &len);
	char *out_alloc = emalloc(len * sizeof(char));
	memcpy(out_alloc, out, len);
	ZVAL_STRINGL(*oval, out_alloc, len, 0);
	return mp_sizeof_bin(len);
}

inline ptrdiff_t php_mp_unpack_bool(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	if (mp_decode_bool((const char **)str))
		ZVAL_TRUE(*oval);
	else
		ZVAL_FALSE(*oval);
	return mp_sizeof_bool(str);
}

inline ptrdiff_t php_mp_unpack_float(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	float val = mp_decode_float((const char **)str);
	ZVAL_DOUBLE(*oval, (double )val);
	return mp_sizeof_float(val);
}

inline ptrdiff_t php_mp_unpack_double(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	double val = mp_decode_double((const char **)str);
	ZVAL_DOUBLE(*oval, (double )val);
	return mp_sizeof_double(val);
}

inline ptrdiff_t php_mp_unpack_map(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	size_t len = mp_decode_map((const char **)str);
	array_init(*oval);
	while (len-- > 0) {
		zval *key = {0};
		php_mp_unpack(&key, str);
		zval *value = {0};
		php_mp_unpack(&value, str);
		switch (Z_TYPE_P(key)) {
		case IS_LONG:
			add_index_zval(*oval, Z_LVAL_P(key), value);
			break;
		case IS_STRING:
			add_assoc_zval_ex(*oval, Z_STRVAL_P(key), Z_STRLEN_P(key), value);
			break;
		default:
			/* TODO: THROW EXCEPTION */
			break;
		}

	}
}

inline ptrdiff_t php_mp_unpack_array(zval **oval, char **str) {
	ALLOC_INIT_ZVAL(*oval);
	size_t len = mp_decode_array((const char **)str);
	array_init(*oval);
	while (len-- > 0) {
		zval *value = {0};
		php_mp_unpack(&value, str);
		add_next_index_zval(*oval, value);
	}
}

char *php_mp_unpack(zval **oval, char **str) {
	size_t needed = 0;
	switch (mp_typeof(**str)) {
	case MP_NIL:
		php_mp_unpack_nil(oval, str);
		break;
	case MP_UINT:
		php_mp_unpack_uint(oval, str);
		break;
	case MP_INT:
		php_mp_unpack_int(oval, str);
		break;
	case MP_STR:
		php_mp_unpack_str(oval, str);
		break;
	case MP_BIN:
		php_mp_unpack_bin(oval, str);
		break;
	case MP_ARRAY:
		php_mp_unpack_array(oval, str);
		break;
	case MP_MAP:
		php_mp_unpack_map(oval, str);
		break;
	case MP_BOOL:
		php_mp_unpack_bool(oval, str);
		break;
	case MP_FLOAT:
		php_mp_unpack_float(oval, str);
		break;
	case MP_DOUBLE:
		php_mp_unpack_double(oval, str);
		break;
	case MP_EXT:
		break;
	default:
		break;
	}
	return *str;
}
