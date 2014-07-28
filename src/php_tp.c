#include "php_tp_const.h"
#include "php_msgpack.h"

#include "php_tarantool.h"
#include "third_party/msgpuck.h"

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen) {
	 return 5 + mp_sizeof_map(2) +
		mp_sizeof_uint(TNT_CODE) +
		mp_sizeof_uint(TNT_AUTH) +
		mp_sizeof_uint(TNT_SYNC) +
		mp_sizeof_uint(sync) +
		mp_sizeof_map(2) +
		mp_sizeof_uint(TNT_USERNAME) +
		mp_sizeof_str(ulen) +
		mp_sizeof_uint(TNT_TUPLE) +
		mp_sizeof_array(2) +
		mp_sizeof_str(0) +
		mp_sizeof_str(SCRAMBLE_SIZE);
}

void php_tp_encode_auth(
		smart_str *str,
		uint32_t sync,
		char * const username,
		size_t username_len,
		char * const scramble) {
	size_t packet_size = php_tp_sizeof_auth(sync, username_len);
	smart_str_ensure(str, packet_size);
	php_mp_pack_package_size(str, packet_size);
	size_t hlen = mp_format(str->c + str->len, packet_size, "{%u%u%u%u}",
			TNT_CODE, TNT_AUTH,
			TNT_SYNC, sync);
	str->len += hlen; packet_size -= hlen;
	mp_format(str->c + str->len, packet_size, "{%u%.*s [%s%.*s]}",
			TNT_USERNAME, username_len,
			username, "", scramble,
			SCRAMBLE_SIZE);
}

size_t php_tp_sizeof_ping(uint32_t sync) {
	return 5 + mp_sizeof_map(2) +
		mp_sizeof_uint(TNT_CODE) +
		mp_sizeof_uint(TNT_PING) +
		mp_sizeof_uint(TNT_SYNC) +
		mp_sizeof_uint(sync);
}

void php_tp_encode_ping(
		smart_str *str,
		uint32_t sync) {
	size_t packet_size = php_tp_sizeof_ping(sync);
	smart_str_ensure(str, packet_size);
	php_mp_pack_package_size(str, packet_size);
	mp_format(str->c + str->len, packet_size, "{%u%u%u%u}",
				TNT_CODE, TNT_PING,
				TNT_SYNC, sync);
}
