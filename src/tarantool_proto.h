#ifndef PHP_TP_H
#define PHP_TP_H

#define SALT64_SIZE   44
#define SALT_SIZE     64
#define SCRAMBLE_SIZE 20

#include <stdint.h>
#include <ext/standard/php_smart_str.h>

/* header */
enum tnt_header_key_t {
	TNT_CODE = 0x00,
	TNT_SYNC = 0x01
};

/* request body */
enum tnt_body_key_t {
	TNT_SPACE = 0x10,
	TNT_INDEX = 0x11,
	TNT_LIMIT = 0x12,
	TNT_OFFSET = 0x13,
	TNT_ITERATOR = 0x14,
	TNT_KEY = 0x20,
	TNT_TUPLE = 0x21,
	TNT_FUNCTION = 0x22,
	TNT_USERNAME = 0x23
};

/* response body */
enum tnt_response_key_t {
	TNT_DATA = 0x30,
	TNT_ERROR = 0x31
};

/* request types */
enum tnt_request_type {
	TNT_OK = 0,
	TNT_SELECT = 1,
	TNT_INSERT = 2,
	TNT_REPLACE = 3,
	TNT_UPDATE = 4,
	TNT_DELETE = 5,
	TNT_CALL = 6,
	TNT_AUTH = 7,
	TNT_PING = 64
};

size_t php_tp_sizeof_auth(uint32_t sync, size_t ulen);
size_t php_tp_sizeof_ping(uint32_t sync);

void php_tp_encode_auth(smart_str *str, uint32_t sync, char * const username, size_t username_len, char * const scramble);
void php_tp_encode_ping(smart_str *str, uint32_t sync);

#endif /* PHP_TP_H */
