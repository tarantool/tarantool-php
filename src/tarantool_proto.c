#include "tarantool_proto.h"
#include "tarantool_msgpack.h"

#include "third_party/msgpuck.h"

static size_t php_tp_sizeof_header(uint32_t request, uint32_t sync) {
	return php_mp_sizeof_hash(2)        +
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

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen, zend_bool guest) {
	size_t val = php_tp_sizeof_header(TNT_AUTH, sync) +
		     php_mp_sizeof_hash(2)               +
		     php_mp_sizeof_long(TNT_USERNAME)    +
		     php_mp_sizeof_string(ulen)          +
		     php_mp_sizeof_long(TNT_TUPLE)       +
		     php_mp_sizeof_array((guest ? 0 : 2));
	if (!guest) {
		val += php_mp_sizeof_string(9)      +
		php_mp_sizeof_string(PHP_SCRAMBLE_SIZE) ;
	}
	return val;
}

void php_tp_encode_auth(
		smart_str *str,
		uint32_t sync,
		char * const username,
		size_t username_len,
		char * const scramble) {
	zend_bool guest = (strncmp(username, "guest", 6) == 0);
	size_t packet_size = php_tp_sizeof_auth(sync, username_len, guest);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_AUTH, sync);

	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_USERNAME);
	php_mp_pack_string(str, username, username_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack_array(str, (guest ? 0 : 2));
	if (!guest) {
		php_mp_pack_string(str, "chap-sha1", 9);
		php_mp_pack_string(str, scramble, PHP_SCRAMBLE_SIZE);
	}
}

size_t php_tp_sizeof_ping(uint32_t sync) {
	return php_tp_sizeof_header(TNT_PING, sync);
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
		php_mp_sizeof_hash(6)                  +
		php_mp_sizeof_long(TNT_SPACE)          +
		php_mp_sizeof_long(space_no)           +
		php_mp_sizeof_long(TNT_INDEX)          +
		php_mp_sizeof_long(index_no)           +
		php_mp_sizeof_long(TNT_OFFSET)         +
		php_mp_sizeof_long(offset)             +
		php_mp_sizeof_long(TNT_LIMIT)          +
		php_mp_sizeof_long(limit)              +
		php_mp_sizeof_long(TNT_ITERATOR)       +
		php_mp_sizeof_long(iterator)           +
		php_mp_sizeof_long(TNT_KEY)            +
		php_mp_sizeof(key)                     ;
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
		php_mp_sizeof(tuple)             ;
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
			    uint32_t space_no, uint32_t index_no,
			    zval *tuple) {
	return  php_tp_sizeof_header(TNT_DELETE, sync) +
		php_mp_sizeof_hash(3)                  +
		php_mp_sizeof_long(TNT_SPACE)          +
		php_mp_sizeof_long(space_no)           +
		php_mp_sizeof_long(TNT_INDEX)          +
		php_mp_sizeof_long(index_no)           +
		php_mp_sizeof_long(TNT_KEY)            +
		php_mp_sizeof(tuple)                   ;
}

void php_tp_encode_delete(smart_str *str, uint32_t sync,
			  uint32_t space_no, uint32_t index_no,
			  zval *tuple) {
	size_t packet_size = php_tp_sizeof_delete(sync,
			space_no, index_no, tuple);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_DELETE, sync);
	php_mp_pack_hash(str, 3);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_INDEX);
	php_mp_pack_long(str, index_no);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, tuple);
}

size_t php_tp_sizeof_call(uint32_t sync,
		uint32_t proc_len, zval *tuple) {
	return  php_tp_sizeof_header(TNT_CALL, sync) +
		php_mp_sizeof_hash(2)                +
		php_mp_sizeof_long(TNT_FUNCTION)     +
		php_mp_sizeof_string(proc_len)       +
		php_mp_sizeof_long(TNT_TUPLE)        +
		php_mp_sizeof(tuple)                 ;
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

size_t php_tp_sizeof_eval(uint32_t sync,
		uint32_t proc_len, zval *tuple) {
	return  php_tp_sizeof_header(TNT_EVAL, sync) +
		php_mp_sizeof_hash(2)                +
		php_mp_sizeof_long(TNT_EXPRESSION)   +
		php_mp_sizeof_string(proc_len)       +
		php_mp_sizeof_long(TNT_TUPLE)        +
		php_mp_sizeof(tuple)                 ;
}

void php_tp_encode_eval(smart_str *str, uint32_t sync,
		char *proc, uint32_t proc_len, zval *tuple) {
	size_t packet_size = php_tp_sizeof_eval(sync,
			proc_len, tuple);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_EVAL, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_EXPRESSION);
	php_mp_pack_string(str, proc, proc_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, tuple);
}

size_t php_tp_sizeof_update(uint32_t sync,
			    uint32_t space_no, uint32_t index_no,
			    zval *key, zval *args) {
	return  php_tp_sizeof_header(TNT_UPDATE, sync) +
		php_mp_sizeof_hash(4)                  +
		php_mp_sizeof_long(TNT_SPACE)          +
		php_mp_sizeof_long(space_no)           +
		php_mp_sizeof_long(TNT_INDEX)          +
		php_mp_sizeof_long(index_no)           +
		php_mp_sizeof_long(TNT_KEY)            +
		php_mp_sizeof(key)                     +
		php_mp_sizeof_long(TNT_TUPLE)          +
		php_mp_sizeof(args);
}

void php_tp_encode_update(smart_str *str, uint32_t sync,
			  uint32_t space_no, uint32_t index_no,
			  zval *key, zval *args) {
	size_t packet_size = php_tp_sizeof_update(sync,
			space_no, index_no, key, args);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_UPDATE, sync);
	php_mp_pack_hash(str, 4);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_INDEX);
	php_mp_pack_long(str, index_no);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, key);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, args);
}

size_t php_tp_sizeof_upsert(uint32_t sync, uint32_t space_no, zval *tuple,
			    zval *args) {
	return  php_tp_sizeof_header(TNT_UPSERT, sync) +
		php_mp_sizeof_hash(3)                  +
		php_mp_sizeof_long(TNT_SPACE)          +
		php_mp_sizeof_long(space_no)           +
		php_mp_sizeof_long(TNT_TUPLE)          +
		php_mp_sizeof(tuple)                   +
		php_mp_sizeof_long(TNT_OPS)            +
		php_mp_sizeof(args);
}

void php_tp_encode_upsert(smart_str *str, uint32_t sync, uint32_t space_no,
			  zval *tuple, zval *args) {
	size_t packet_size = php_tp_sizeof_upsert(sync, space_no, tuple, args);
	smart_str_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_UPSERT, sync);
	php_mp_pack_hash(str, 3);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, tuple);
	php_mp_pack_long(str, TNT_OPS);
	php_mp_pack(str, args);
}

int64_t php_tp_response(struct tnt_response *r, char *buf, size_t size)
{
	memset(r, 0, sizeof(*r));
	const char *p = buf;
	/* len */
	uint32_t len = size;
	/* header */
	if (mp_typeof(*p) != MP_MAP)
		return -1;
	uint32_t n = mp_decode_map(&p);
	while (n-- > 0) {
		if (mp_typeof(*p) != MP_UINT)
			return -1;
		uint32_t key = mp_decode_uint(&p);
		if (mp_typeof(*p) != MP_UINT)
			return -1;
		switch (key) {
		case TNT_SYNC:
			r->sync = mp_decode_uint(&p);
			break;
		case TNT_CODE:
			r->code = mp_decode_uint(&p);
			break;
		default:
			mp_next(&p);
		}
		r->bitmap |= (1ULL << key);
	}
	/* body */
	if (mp_typeof(*p) != MP_MAP)
		return -1;
	n = mp_decode_map(&p);
	while (n-- > 0) {
		uint32_t key = mp_decode_uint(&p);
		switch (key) {
		case TNT_ERROR:
			if (mp_typeof(*p) != MP_STR)
				return -1;
			uint32_t elen = 0;
			r->error = mp_decode_str(&p, &elen);
			r->error_len = elen;
			r->code &= ((1 << 15) - 1);
			break;
		case TNT_DATA:
			if (mp_typeof(*p) != MP_ARRAY)
				return -1;
			r->data = p;
			mp_next(&p);
			r->data_len = p - r->data;
			break;
		default:
			mp_next(&p);
		}
		r->bitmap |= (1ULL << key);
	}
	return p - buf;
}

