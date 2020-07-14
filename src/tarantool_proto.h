#ifndef   PHP_TNT_PROTO_H
#define   PHP_TNT_PROTO_H

#define SALT64_SIZE      44
#define SALT_SIZE        64
// #define SCRAMBLE_SIZE    20
#define GREETING_SIZE    128
#define SALT_PREFIX_SIZE 64

#define GREETING_PROTOCOL_LEN_MAX 32

#define SPACE_SPACE 281
#define SPACE_INDEX 289

#define INDEX_SPACE_NO      0
#define INDEX_INDEX_NO      0
#define INDEX_SPACE_NAME    2
#define INDEX_INDEX_NAME    2

#include <stdint.h>

#include <php_tarantool.h>

/**
 * Pack version into uint32_t.
 * The highest byte or result means major version, next - minor,
 * middle - patch, last - revision.
 */
static inline uint32_t
version_id(unsigned major, unsigned minor, unsigned patch)
{
	return (((major << 8) | minor) << 8) | patch;
}

static inline unsigned
version_id_major(uint32_t version_id)
{
	return (version_id >> 16) & 0xff;
}

static inline unsigned
version_id_minor(uint32_t version_id)
{
	return (version_id >> 8) & 0xff;
}

static inline unsigned
version_id_patch(uint32_t version_id)
{
	return version_id & 0xff;
}


/* header */
enum tnt_header_key_t {
	TNT_CODE      = 0x00,
	TNT_SYNC      = 0x01,
	TNT_SCHEMA_ID = 0x05
};

/* request body */
enum tnt_body_key_t {
	TNT_SPACE      = 0x10,
	TNT_INDEX      = 0x11,
	TNT_LIMIT      = 0x12,
	TNT_OFFSET     = 0x13,
	TNT_ITERATOR   = 0x14,
	TNT_KEY        = 0x20,
	TNT_TUPLE      = 0x21,
	TNT_FUNCTION   = 0x22,
	TNT_USERNAME   = 0x23,
	TNT_EXPRESSION = 0x27,
	TNT_OPS        = 0x28
};

/* response body */
enum tnt_response_key_t {
	TNT_DATA  = 0x30,
	TNT_ERROR = 0x31
};

/* request types */
enum tnt_request_type {
	TNT_OK      = 0x00,
	TNT_SELECT  = 0x01,
	TNT_INSERT  = 0x02,
	TNT_REPLACE = 0x03,
	TNT_UPDATE  = 0x04,
	TNT_DELETE  = 0x05,
	TNT_CALL_16 = 0x06,
	TNT_AUTH    = 0x07,
	TNT_EVAL    = 0x08,
	TNT_UPSERT  = 0x09,
	TNT_CALL    = 0x0a,
	TNT_PING    = 0x40
};

enum tnt_iterator_type {
	ITERATOR_EQ                 = 0,
	ITERATOR_REQ                = 1,
	ITERATOR_ALL                = 2,
	ITERATOR_LT                 = 3,
	ITERATOR_LE                 = 4,
	ITERATOR_GE                 = 5,
	ITERATOR_GT                 = 6,
	ITERATOR_BITS_ALL_SET       = 7,
	ITERATOR_BITSET_ALL_SET     = 7,
	ITERATOR_BITS_ANY_SET       = 8,
	ITERATOR_BITSET_ANY_SET     = 8,
	ITERATOR_BITS_ALL_NOT_SET   = 9,
	ITERATOR_BITSET_ALL_NOT_SET = 9,
	ITERATOR_OVERLAPS           = 10,
	ITERATOR_NEIGHBOR           = 11,
};

struct tnt_response {
	uint64_t    bitmap;
	const char *buf;
	uint32_t    code;
	uint32_t    sync;
	const char *error;
	size_t      error_len;
	const char *data;
	size_t      data_len;
};

int64_t php_tp_response(struct tnt_response *r, char *buf, size_t size);

void php_tp_encode_auth(smart_string *str, uint32_t sync, char * const username,
			size_t username_len, char * const scramble);
void php_tp_encode_ping(smart_string *str, uint32_t sync);
void php_tp_encode_select(smart_string *str, uint32_t sync, uint32_t space_no,
			  uint32_t index_no, uint32_t limit, uint32_t offset,
			  uint32_t iterator, zval *key);
void php_tp_encode_insert_or_replace(smart_string *str, uint32_t sync,
				     uint32_t space_no, zval *tuple,
				     uint32_t type);
void php_tp_encode_delete(smart_string *str, uint32_t sync, uint32_t space_no,
			  uint32_t index_no, zval *tuple);
void php_tp_encode_call(smart_string *str, uint32_t sync, char *proc,
			uint32_t proc_len, zval *tuple);
void php_tp_encode_call_16(smart_string *str, uint32_t sync, char *proc,
			   uint32_t proc_len, zval *tuple);
void php_tp_encode_eval(smart_string *str, uint32_t sync, char *proc,
			uint32_t proc_len, zval *tuple);

char *php_tp_encode_update(smart_string *str, uint32_t sync,
			   uint32_t space_no, uint32_t index_no,
			   zval *key);
char *php_tp_encode_upsert(smart_string *str, uint32_t sync,
			   uint32_t space_no, zval *tuple);
void php_tp_encode_uheader(smart_string *str, size_t op_count);
void php_tp_encode_uother(smart_string *str, char type, uint32_t fieldno,
			  zval *value);
void php_tp_encode_usplice(smart_string *str, uint32_t fieldno,
			   uint32_t position, uint32_t offset,
			   const char *buffer, size_t buffer_len);
void php_tp_reencode_length(smart_string *str, size_t orig_len);

int convert_iter_str(const char *i, size_t i_len);

uint32_t php_tp_verify_greetings(char *greetingbuf);

#endif /* PHP_TNT_PROTO_H */
