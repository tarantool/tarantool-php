#include "tarantool_proto.h"
#include "tarantool_msgpack.h"

#include "third_party/msgpuck.h"

static size_t php_tp_sizeof_header(uint32_t request, uint32_t sync) {
	return php_mp_sizeof_hash(2) +
	       php_mp_sizeof_long(TNT_CODE) +
	       php_mp_sizeof_long(request)  +
	       php_mp_sizeof_long(TNT_SYNC) +
	       php_mp_sizeof_long(sync)     ;
}

static void php_tp_pack_header(smart_str *str, size_t size,
		uint32_t request, uint32_t sync) {
	php_mp_pack_package_size(str, size);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_CODE);
	php_mp_pack_long(str, request);
	php_mp_pack_long(str, TNT_SYNC);
	php_mp_pack_long(str, sync);
}

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen) {
	 return php_tp_sizeof_header(TNT_AUTH, sync) +
		php_mp_sizeof_hash(2)             +
		php_mp_sizeof_long(TNT_USERNAME) +
		php_mp_sizeof_string(ulen)          +
		php_mp_sizeof_long(TNT_TUPLE)    +
		php_mp_sizeof_array(2)           +
		php_mp_sizeof_string(9)             +
		php_mp_sizeof_string(SCRAMBLE_SIZE) ;
}

void php_tp_encode_auth(
		smart_str *str,
		uint32_t sync,
		char * const username,
		size_t username_len,
		char * const scramble) {
	size_t packet_size = php_tp_sizeof_auth(sync, username_len);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_AUTH, sync);

	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_USERNAME);
	php_mp_pack_string(str, username, username_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack_array(str, 2);
	php_mp_pack_string(str, "chap-sha1", 9);
	php_mp_pack_string(str, scramble, SCRAMBLE_SIZE);
}

size_t php_tp_sizeof_ping(uint32_t sync) {
	return php_tp_sizeof_header(TNT_AUTH, sync);
}

void php_tp_encode_ping(
		smart_str *str,
		uint32_t sync) {
	size_t packet_size = php_tp_sizeof_ping(sync);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_PING, sync);
}

size_t php_tp_sizeof_select(uint32_t sync, uint32_t space_no,
		uint32_t index_no, uint32_t limit,
		uint32_t offset, uint32_t iterator,
		zval *key) {
	return  php_tp_sizeof_header(TNT_SELECT, sync) +
		php_mp_sizeof_hash(6)            +
		php_mp_sizeof_long(TNT_SPACE)    +
		php_mp_sizeof_long(space_no)     +
		php_mp_sizeof_long(TNT_INDEX)    +
		php_mp_sizeof_long(index_no)     +
		php_mp_sizeof_long(TNT_OFFSET)   +
		php_mp_sizeof_long(offset)       +
		php_mp_sizeof_long(TNT_LIMIT)    +
		php_mp_sizeof_long(limit)        +
		php_mp_sizeof_long(TNT_ITERATOR) +
		php_mp_sizeof_long(iterator)     +
		php_mp_sizeof_long(TNT_KEY)      +
		php_mp_sizeof(key);
}

void php_tp_encode_select(smart_str *str,
		uint32_t sync, uint32_t space_no,
		uint32_t index_no, uint32_t limit,
		uint32_t offset, uint32_t iterator,
		zval *key) {
	size_t packet_size = php_tp_sizeof_select(sync,
			space_no, index_no, offset, limit, iterator, key);

	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_SELECT, sync);
	php_mp_pack_hash(str, 6);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_INDEX);
	php_mp_pack_long(str, index_no);
	php_mp_pack_long(str, TNT_OFFSET);
	php_mp_pack_long(str, offset);
	php_mp_pack_long(str, TNT_LIMIT);
	php_mp_pack_long(str, limit);
	php_mp_pack_long(str, TNT_ITERATOR);
	php_mp_pack_long(str, iterator);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, key);
}

size_t php_tp_sizeof_insert_or_replace(uint32_t sync,
		uint32_t space_no, zval *tuple, uint32_t type) {
	return  php_tp_sizeof_header(type, sync) +
		php_mp_sizeof_hash(2)            +
		php_mp_sizeof_long(TNT_SPACE)    +
		php_mp_sizeof_long(space_no)     +
		php_mp_sizeof_long(TNT_TUPLE)    +
		php_mp_sizeof(tuple);
}

void php_tp_encode_insert_or_replace(smart_str *str, uint32_t sync,
		uint32_t space_no, zval *tuple, uint32_t type) {
	assert(type == TNT_INSERT || type == TNT_REPLACE);
	size_t packet_size = php_tp_sizeof_insert_or_replace(sync,
			space_no, tuple, type);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, type, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, tuple);
}

size_t php_tp_sizeof_delete(uint32_t sync,
		uint32_t space_no, zval *tuple) {
	return  php_tp_sizeof_header(TNT_DELETE, sync) +
		php_mp_sizeof_hash(2)            +
		php_mp_sizeof_long(TNT_SPACE)    +
		php_mp_sizeof_long(space_no)     +
		php_mp_sizeof_long(TNT_KEY)      +
		php_mp_sizeof(tuple);
}

void php_tp_encode_delete(smart_str *str, uint32_t sync,
		uint32_t space_no, zval *tuple) {
	size_t packet_size = php_tp_sizeof_delete(sync,
			space_no, tuple);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_DELETE, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, tuple);
}

size_t php_tp_sizeof_call(uint32_t sync,
		uint32_t proc_len, zval *tuple) {
	return  php_tp_sizeof_header(TNT_CALL, sync) +
		php_mp_sizeof_hash(2)            +
		php_mp_sizeof_long(TNT_FUNCTION) +
		php_mp_sizeof_string(proc_len)   +
		php_mp_sizeof_long(TNT_TUPLE)    +
		php_mp_sizeof(tuple);
}

void php_tp_encode_call(smart_str *str, uint32_t sync,
		char *proc, uint32_t proc_len, zval *tuple) {
	size_t packet_size = php_tp_sizeof_call(sync,
			proc_len, tuple);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_CALL, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_FUNCTION);
	php_mp_pack_string(str, proc, proc_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, tuple);
}

size_t php_tp_sizeof_update_ops(zval *ops) {
	HashTable *hops = Z_ARRVAL_P(ops);
	size_t n = zend_hash_num_elements(hops);
	size_t needed = php_mp_sizeof_array(n);
	zval **op;
	size_t key_index = 0;
	for(; key_index < n; ++key_index) {
		zend_hash_index_find(hops, key_index, (void **)&op);
		HashTable *hop = Z_ARRVAL_PP(op);
		size_t n = zend_hash_num_elements(hop);
		zval **opstr, **oppos;
		zend_hash_find(hop, "op", 2, (void **)&opstr);
		zend_hash_find(hop, "field", 5, (void **)&oppos);
		zval **oparg, **splice_len, **splice_val;

		switch(Z_STRVAL_PP(opstr)[0]) {
		case '|':
			zend_hash_find(hop, "offset", 6, (void **)&oparg);
			zend_hash_find(hop, "length", 6, (void **)&splice_len);
			zend_hash_find(hop, "list", 4, (void **)&splice_val);
			needed += php_mp_sizeof_array(5) +
				  php_mp_sizeof_string(1) +
				  php_mp_sizeof_long(Z_LVAL_PP(oppos)) +
				  php_mp_sizeof_long(Z_LVAL_PP(oparg)) +
				  php_mp_sizeof_long(Z_LVAL_PP(splice_len)) +
				  php_mp_sizeof_string(Z_STRLEN_PP(splice_val));
			break;
		case '+':
		case '-':
		case '&':
		case '^':
			zend_hash_find(hop, "arg", 3, (void **)&oparg);
			needed += php_mp_sizeof_array(3) +
				  php_mp_sizeof_string(1);
				  php_mp_sizeof_long(Z_LVAL_PP(oppos));
				  php_mp_sizeof_long(Z_LVAL_PP(oparg));
			break;
		case '=':
		case '!':
			zend_hash_find(hop, "arg", 3, (void **)&oparg);
			needed += php_mp_sizeof_array(3) +
				  php_mp_sizeof_string(1) +
				  php_mp_sizeof_long(Z_LVAL_PP(oppos)) +
				  php_mp_sizeof(*oparg);
			break;
		case '#':
			needed += php_mp_sizeof_array(2) +
				  php_mp_sizeof_string(1) +
			          php_mp_sizeof_long(Z_LVAL_PP(oppos));
			//php_mp_sizeof_long(0);
			break;
		default:
			break;
		}
	}
	return needed;
}

void php_tp_encode_update_ops(smart_str *str, zval *ops) {
	HashTable *hops = Z_ARRVAL_P(ops);
	size_t n = zend_hash_num_elements(hops);
	zval **op;
	size_t key_index = 0;
	php_mp_pack_array(str, n);
	for(; key_index < n; ++key_index) {
		zend_hash_index_find(hops, key_index, (void **)&op);
		HashTable *hop = Z_ARRVAL_PP(op);
		size_t n = zend_hash_num_elements(hop);
		zval **opstr, **oppos;
		zend_hash_find(hop, "op", 2, (void **)&opstr);
		zend_hash_find(hop, "field", 5, (void **)&oppos);
		zval **oparg, **splice_len, **splice_val;

		switch(Z_STRVAL_PP(opstr)[0]) {
		case '|':
			zend_hash_find(hop, "offset", 6, (void **)&oparg);
			zend_hash_find(hop, "length", 6, (void **)&splice_len);
			zend_hash_find(hop, "list", 4, (void **)&splice_val);
			php_mp_pack_array(str, 5);
			php_mp_pack_string(str, Z_STRVAL_PP(opstr), 1);
			php_mp_pack_long(str, Z_LVAL_PP(oppos));
			php_mp_pack_long(str, Z_LVAL_PP(oparg));
			php_mp_pack_long(str, Z_LVAL_PP(splice_len));
			php_mp_pack_string(str, Z_STRVAL_PP(splice_val), Z_STRLEN_PP(splice_val));
			break;
		case '+':
		case '-':
		case '&':
		case '^':
			zend_hash_find(hop, "arg", 3, (void **)&oparg);
			php_mp_pack_array(str, 3);
			php_mp_pack_string(str, Z_STRVAL_PP(opstr), 1);
			php_mp_pack_long(str, Z_LVAL_PP(oppos));
			php_mp_pack_long(str, Z_LVAL_PP(oparg));
			break;
		case '=':
		case '!':
			zend_hash_find(hop, "arg", 3, (void **)&oparg);
			php_mp_pack_array(str, 3);
			php_mp_pack_string(str, Z_STRVAL_PP(opstr), 1);
			php_mp_pack_long(str, Z_LVAL_PP(oppos));
			php_mp_pack(str, *oparg);
			break;
		case '#':
			php_mp_pack_array(str, 2);
			php_mp_pack_string(str, Z_STRVAL_PP(opstr), 1);
			php_mp_pack_long(str, Z_LVAL_PP(oppos));
			//php_mp_pack_long(str, 0);
			break;
		default:
			break;
		}
	}
}

size_t php_tp_sizeof_update(uint32_t sync,
		uint32_t space_no, zval *key, zval *args) {
	return  php_tp_sizeof_header(TNT_UPDATE, sync) +
		php_mp_sizeof_hash(3)         +
		php_mp_sizeof_long(TNT_SPACE) +
		php_mp_sizeof_long(space_no)  +
		php_mp_sizeof_long(TNT_KEY)   +
		php_mp_sizeof(key)            +
		php_mp_sizeof_long(TNT_TUPLE) +
		php_tp_sizeof_update_ops(args);
}

void php_tp_encode_update(smart_str *str, uint32_t sync,
		uint32_t space_no, zval *key, zval *args) {
	size_t packet_size = php_tp_sizeof_update(sync,
			space_no, key, args);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_UPDATE, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, key);
	php_mp_pack_long(str, TNT_TUPLE);
	php_tp_encode_update_ops(str, args);
}
