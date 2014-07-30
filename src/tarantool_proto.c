#include "tarantool_proto.h"
#include "tarantool_msgpack.h"

#include "third_party/msgpuck.h"

static void php_tp_pack_header(smart_str *str, size_t size,
		uint32_t request, uint32_t sync) {
	php_mp_pack_package_size(str, size);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_CODE);
	php_mp_pack_long(str, request);
	php_mp_pack_long(str, TNT_SYNC);
	php_mp_pack_long(str, sync);
}

static size_t php_tp_sizeof_header(uint32_t request, uint32_t sync) {
	return mp_sizeof_map(2) +
	       mp_sizeof_uint(TNT_CODE) +
	       mp_sizeof_uint(request)  +
	       mp_sizeof_uint(TNT_SYNC) +
	       mp_sizeof_uint(sync);
}

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen) {
	 return php_tp_sizeof_header(TNT_AUTH, sync) +
		mp_sizeof_map(2)             +
		mp_sizeof_uint(TNT_USERNAME) +
		mp_sizeof_str(ulen)          +
		mp_sizeof_uint(TNT_TUPLE)    +
		mp_sizeof_array(2)           +
		mp_sizeof_str(9)             +
		mp_sizeof_str(SCRAMBLE_SIZE);
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
