#include "php_tarantool.h"

#include "tarantool_tp.h"
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

static char *php_tp_pack_header(smart_string *str, size_t size,
				uint32_t request, uint32_t sync) {
	char *sz = SSTR_POS(str);
	php_mp_pack_package_size(str, size);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_CODE);
	php_mp_pack_long(str, request);
	php_mp_pack_long(str, TNT_SYNC);
	php_mp_pack_long(str, sync);
	return sz;
}

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen, zend_bool nopass) {
	size_t val = php_tp_sizeof_header(TNT_AUTH, sync) +
		     php_mp_sizeof_hash(2)               +
		     php_mp_sizeof_long(TNT_USERNAME)    +
		     php_mp_sizeof_string(ulen)          +
		     php_mp_sizeof_long(TNT_TUPLE)       +
		     php_mp_sizeof_array((nopass ? 0 : 2));
	if (!nopass) {
		val += php_mp_sizeof_string(9)      +
		php_mp_sizeof_string(SCRAMBLE_SIZE) ;
	}
	return val;
}

void php_tp_encode_auth(
		smart_string *str,
		uint32_t sync,
		char * const username,
		size_t username_len,
		char * const scramble) {
	zend_bool nopass = (zend_bool )(scramble != NULL);
	size_t packet_size = php_tp_sizeof_auth(sync, username_len, nopass);
	smart_string_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_AUTH, sync);

	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_USERNAME);
	php_mp_pack_string(str, username, username_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack_array(str, (nopass ? 0 : 2));
	if (!nopass) {
		php_mp_pack_string(str, "chap-sha1", 9);
		php_mp_pack_string(str, scramble, SCRAMBLE_SIZE);
	}
}

size_t php_tp_sizeof_ping(uint32_t sync) {
	return php_tp_sizeof_header(TNT_PING, sync);
}

void php_tp_encode_ping(
		smart_string *str,
		uint32_t sync) {
	size_t packet_size = php_tp_sizeof_ping(sync);
	smart_string_ensure(str, packet_size + 5);
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

void php_tp_encode_select(smart_string *str,
		uint32_t sync, uint32_t space_no,
		uint32_t index_no, uint32_t limit,
		uint32_t offset, uint32_t iterator,
		zval *key) {
	size_t packet_size = php_tp_sizeof_select(sync,
			space_no, index_no, offset, limit, iterator, key);

	smart_string_ensure(str, packet_size + 5);
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

void php_tp_encode_insert_or_replace(smart_string *str, uint32_t sync,
		uint32_t space_no, zval *tuple, uint32_t type) {
	assert(type == TNT_INSERT || type == TNT_REPLACE);
	size_t packet_size = php_tp_sizeof_insert_or_replace(sync,
			space_no, tuple, type);
	smart_string_ensure(str, packet_size + 5);
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

void php_tp_encode_delete(smart_string *str, uint32_t sync,
			  uint32_t space_no, uint32_t index_no,
			  zval *tuple) {
	size_t packet_size = php_tp_sizeof_delete(sync,
			space_no, index_no, tuple);
	smart_string_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, TNT_DELETE, sync);
	php_mp_pack_hash(str, 3);
	php_mp_pack_long(str, TNT_SPACE);
	php_mp_pack_long(str, space_no);
	php_mp_pack_long(str, TNT_INDEX);
	php_mp_pack_long(str, index_no);
	php_mp_pack_long(str, TNT_KEY);
	php_mp_pack(str, tuple);
}

static size_t php_tp_sizeof_call(uint32_t sync, uint32_t proc_len,
				 enum tnt_request_type type, zval *tuple) {
	return  php_tp_sizeof_header(type, sync)     +
		php_mp_sizeof_hash(2)                +
		php_mp_sizeof_long(TNT_FUNCTION)     +
		php_mp_sizeof_string(proc_len)       +
		php_mp_sizeof_long(TNT_TUPLE)        +
		php_mp_sizeof(tuple)                 ;
}

static void php_tp_encode_call_impl(smart_string *str, uint32_t sync,
				    char *proc, uint32_t proc_len,
				    enum tnt_request_type type, zval *tuple) {
	size_t packet_size = php_tp_sizeof_call(sync, proc_len, type, tuple);
	smart_string_ensure(str, packet_size + 5);
	php_tp_pack_header(str, packet_size, type, sync);
	php_mp_pack_hash(str, 2);
	php_mp_pack_long(str, TNT_FUNCTION);
	php_mp_pack_string(str, proc, proc_len);
	php_mp_pack_long(str, TNT_TUPLE);
	php_mp_pack(str, tuple);
}

void php_tp_encode_call(smart_string *str, uint32_t sync, char *proc,
			uint32_t proc_len, zval *tuple) {
	php_tp_encode_call_impl(str,sync, proc, proc_len, TNT_CALL, tuple);
}

void php_tp_encode_call_16(smart_string *str, uint32_t sync, char *proc,
			   uint32_t proc_len, zval *tuple) {
	php_tp_encode_call_impl(str,sync, proc, proc_len, TNT_CALL_16, tuple);
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

void php_tp_encode_eval(smart_string *str, uint32_t sync,
		char *proc, uint32_t proc_len, zval *tuple) {
	size_t packet_size = php_tp_sizeof_eval(sync,
			proc_len, tuple);
	smart_string_ensure (str, packet_size + 5);
	php_tp_pack_header  (str, packet_size, TNT_EVAL, sync);
	php_mp_pack_hash    (str, 2);
	php_mp_pack_long    (str, TNT_EXPRESSION);
	php_mp_pack_string  (str, proc, proc_len);
	php_mp_pack_long    (str, TNT_TUPLE);
	php_mp_pack         (str, tuple);
}

char *php_tp_encode_update(smart_string *str, uint32_t sync,
			   uint32_t space_no, uint32_t index_no,
			   zval *key) {
	char *sz = php_tp_pack_header(str, 0, TNT_UPDATE, sync);
	php_mp_pack_hash (str, 4);
	php_mp_pack_long (str, TNT_SPACE);
	php_mp_pack_long (str, space_no);
	php_mp_pack_long (str, TNT_INDEX);
	php_mp_pack_long (str, index_no);
	php_mp_pack_long (str, TNT_KEY);
	php_mp_pack      (str, key);
	php_mp_pack_long (str, TNT_TUPLE);
	return sz;
}

char *php_tp_encode_upsert(smart_string *str, uint32_t sync,
			   uint32_t space_no, zval *tuple) {
	char *sz = php_tp_pack_header(str, 0, TNT_UPSERT, sync);
	php_mp_pack_hash (str, 3);
	php_mp_pack_long (str, TNT_SPACE);
	php_mp_pack_long (str, space_no);
	php_mp_pack_long (str, TNT_TUPLE);
	php_mp_pack      (str, tuple);
	php_mp_pack_long (str, TNT_OPS);
	return sz;
}

void php_tp_encode_uheader(smart_string *str, size_t op_count) {
	php_mp_pack_array(str, op_count);
}

void php_tp_encode_uother(smart_string *str, char type, uint32_t fieldno,
			  zval *value) {
	php_mp_pack_array(str, 3);
	php_mp_pack_string(str, (const char *)&type, 1);
	php_mp_pack_long(str, fieldno);
	php_mp_pack(str, value);
}

void php_tp_encode_usplice(smart_string *str, uint32_t fieldno,
			   uint32_t position, uint32_t offset,
			   const char *buffer, size_t buffer_len) {
	php_mp_pack_array(str, 5);
	php_mp_pack_string(str, ":", 1);
	php_mp_pack_long(str, fieldno);
	php_mp_pack_long(str, position);
	php_mp_pack_long(str, offset);
	php_mp_pack_string(str, buffer, buffer_len);
}

void php_tp_reencode_length(smart_string *str, size_t orig_len) {
	ssize_t package_size = (SSTR_LEN(str) - orig_len) - 5;
	assert(package_size > 0);
	char *sz = SSTR_BEG(str) + orig_len;
	php_mp_pack_package_size_basic(sz, package_size);
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

int convert_iter_str(const char *i, size_t i_len) {
	int first = toupper(i[0]);
	switch (first) {
	case 'A':
		if (i_len == 3 && toupper(i[1]) == 'L' && toupper(i[2]) == 'L')
			return ITERATOR_ALL;
		break;
	case 'B':
		if (i_len > 7            && toupper(i[1]) == 'I' &&
		    toupper(i[2]) == 'T' && toupper(i[3]) == 'S' &&
		    toupper(i[4]) == 'E' && toupper(i[5]) == 'T' &&
		    toupper(i[6]) == '_') {
			if (i_len == 18           && toupper(i[7])  == 'A' &&
			    toupper(i[8])  == 'L' && toupper(i[9])  == 'L' &&
			    toupper(i[10]) == '_' && toupper(i[11]) == 'N' &&
			    toupper(i[12]) == 'O' && toupper(i[13]) == 'T' &&
			    toupper(i[14]) == '_' && toupper(i[15]) == 'S' &&
			    toupper(i[16]) == 'E' && toupper(i[17]) == 'T')
				return ITERATOR_BITSET_ALL_NOT_SET;
			else if (i_len == 14           && toupper(i[7])  == 'A' &&
			         toupper(i[8])  == 'L' && toupper(i[9])  == 'L' &&
			         toupper(i[10]) == '_' && toupper(i[11]) == 'S' &&
			         toupper(i[12]) == 'E' && toupper(i[13]) == 'T')
				return ITERATOR_BITSET_ALL_SET;
			else if (i_len == 14           && toupper(i[7])  == 'A' &&
			         toupper(i[8])  == 'N' && toupper(i[9])  == 'Y' &&
			         toupper(i[10]) == '_' && toupper(i[11]) == 'S' &&
			         toupper(i[12]) == 'E' && toupper(i[13]) == 'T')
				return ITERATOR_BITSET_ANY_SET;
		}
		if (i_len > 4            && toupper(i[1]) == 'I' &&
		    toupper(i[2]) == 'T' && toupper(i[3]) == 'S' &&
		    toupper(i[4]) == '_') {
			if (i_len == 16           && toupper(i[5])  == 'A' &&
			    toupper(i[6])  == 'L' && toupper(i[7])  == 'L' &&
			    toupper(i[8])  == '_' && toupper(i[9])  == 'N' &&
			    toupper(i[10]) == 'O' && toupper(i[11]) == 'T' &&
			    toupper(i[12]) == '_' && toupper(i[13]) == 'S' &&
			    toupper(i[14]) == 'E' && toupper(i[15]) == 'T')
				return ITERATOR_BITSET_ALL_NOT_SET;
			else if (i_len == 12           && toupper(i[5])  == 'A' &&
			         toupper(i[6])  == 'L' && toupper(i[7])  == 'L' &&
			         toupper(i[8])  == '_' && toupper(i[9])  == 'S' &&
			         toupper(i[10]) == 'E' && toupper(i[11]) == 'T')
				return ITERATOR_BITSET_ALL_SET;
			else if (i_len == 12           && toupper(i[5])  == 'A' &&
			         toupper(i[6])  == 'N' && toupper(i[7])  == 'Y' &&
			         toupper(i[8])  == '_' && toupper(i[9])  == 'S' &&
			         toupper(i[10]) == 'E' && toupper(i[11]) == 'T')
				return ITERATOR_BITSET_ANY_SET;
		}
		break;
	case 'E':
		if (i_len == 2 && toupper(i[1]) == 'Q')
			return ITERATOR_EQ;
		break;
	case 'G':
		if (i_len == 2) {
			int second = toupper(i[1]);
			switch (second) {
			case 'E':
				return ITERATOR_GE;
			case 'T':
				return ITERATOR_GT;
			}
		}
		break;
	case 'L':
		if (i_len == 2) {
			int second = toupper(i[1]);
			switch (second) {
			case 'T':
				return ITERATOR_LT;
			case 'E':
				return ITERATOR_LE;
			}
		}
		break;
	case 'N':
		if (i_len == 8           && toupper(i[1]) == 'E' &&
		    toupper(i[2]) == 'I' && toupper(i[3]) == 'G' &&
		    toupper(i[4]) == 'H' && toupper(i[5]) == 'B' &&
		    toupper(i[6]) == 'O' && toupper(i[7]) == 'R')
			return ITERATOR_NEIGHBOR;
		break;
	case 'O':
		if (i_len == 8           && toupper(i[1]) == 'V' &&
		    toupper(i[2]) == 'E' && toupper(i[3]) == 'R' &&
		    toupper(i[4]) == 'L' && toupper(i[5]) == 'A' &&
		    toupper(i[6]) == 'P' && toupper(i[7]) == 'S')
			return ITERATOR_OVERLAPS;
		break;
	case 'R':
		if (i_len == 3 && toupper(i[1]) == 'E' && toupper(i[2]) == 'Q')
			return ITERATOR_REQ;
		break;
	default:
		break;
	};
	return -1;
}

uint32_t php_tp_verify_greetings(char *greetingbuf) {
	/* Check basic structure - magic string and \n delimiters */
	if (memcmp(greetingbuf, "Tarantool ", strlen("Tarantool ")) != 0 ||
		   greetingbuf[GREETING_SIZE / 2 - 1] != '\n' ||
		   greetingbuf[GREETING_SIZE     - 1] != '\n')
		return 0;

	int h = GREETING_SIZE / 2;
	const char *pos = greetingbuf + strlen("Tarantool ");
	const char *end = greetingbuf + h;

	/* Extract a version string - a string until ' ' */
	char version[20];
	const char *vend = (const char *) memchr(pos, ' ', end - pos);
	if (vend == NULL || (size_t)(vend - pos) >= sizeof(version))
		return 0;
	memcpy(version, pos, vend - pos);
	version[vend - pos] = '\0';
	pos = vend + 1;
	for (; pos < end && *pos == ' '; ++pos); /* skip spaces */

	/* Parse a version string - 1.6.6-83-gc6b2129 or 1.6.7 */
	unsigned major, minor, patch;
	if (sscanf(version, "%u.%u.%u", &major, &minor, &patch) != 3)
		return 0;
	return version_id(major, minor, patch);
}
