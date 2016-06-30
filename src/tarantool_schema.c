#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>
#include <stdint.h>

#include "php_tarantool.h"
#include "tarantool_schema.h"
#include "tarantool_network.h"

#include "third_party/PMurHash.h"
#define MUR_SEED 13
#include "third_party/msgpuck.h"

/*static inline void
schema_field_value_free(struct schema_field_value *val) {
	free(val->field_name);
}*/

int
mh_indexcmp_eq(
		const struct schema_index_value **lval,
		const struct schema_index_value **rval,
		void *arg) {
	(void )arg;
	if ((*lval)->key.id_len != (*rval)->key.id_len) return 0;
	return !memcmp((*lval)->key.id, (*rval)->key.id, (*rval)->key.id_len);
}

int
mh_indexcmp_key_eq(
		const struct schema_key *key,
		const struct schema_index_value **val,
		void *arg) {
	(void )arg;
	if (key->id_len != (*val)->key.id_len) return 0;
	return !memcmp(key->id, (*val)->key.id, key->id_len);
}

#define mh_arg_t void *

#define mh_eq(a, b, arg)      mh_indexcmp_eq(a, b, arg)
#define mh_eq_key(a, b, arg)  mh_indexcmp_key_eq(a, b, arg)
#define mh_hash(x, arg)       PMurHash32(MUR_SEED, (*x)->key.id, (*x)->key.id_len)
#define mh_hash_key(x, arg)   PMurHash32(MUR_SEED, (x)->id, (x)->id_len);

#define mh_node_t struct schema_index_value *
#define mh_key_t  struct schema_key *

#define MH_CALLOC(x, y) pecalloc((x), (y), 1)
#define MH_FREE(x)      pefree((x), 1)

#define mh_name               _schema_index
#define MH_SOURCE             1
#include                      "third_party/mhash.h"

static inline void
schema_index_value_free(const struct schema_index_value *val) {
	if (val) {
		pefree(val->index_name, 1);
		/*
		int i = 0;
		for (i = val->index_parts_len; i > 0; --i)
			schema_field_value_free(&(val->index_parts[i - 1]));
		pefree(val->index_parts, 1);
		*/
	}
}

static inline void
schema_index_free(struct mh_schema_index_t *schema) {
	int pos = 0;
	mh_int_t index_slot = 0;
	mh_foreach(schema, pos) {
		const struct schema_index_value *ivalue = *mh_schema_index_node(schema, pos);
		const struct schema_index_value *iv1 = NULL, *iv2 = NULL;
		do {
			struct schema_key key_number = {(void *)&(ivalue->index_number), sizeof(uint32_t)};
			index_slot = mh_schema_index_find(schema, &key_number, NULL);
			if (index_slot == mh_end(schema))
				break;
			iv1 = *mh_schema_index_node(schema, index_slot);
			mh_schema_index_del(schema, index_slot, NULL);
		} while (0);
		do {
			struct schema_key key_string = {ivalue->index_name, ivalue->index_name_len};
			index_slot = mh_schema_index_find(schema, &key_string, NULL);
			if (index_slot == mh_end(schema))
				break;
			iv2 = *mh_schema_index_node(schema, index_slot);
			mh_schema_index_del(schema, index_slot, NULL);
		} while (0);
		schema_index_value_free(ivalue);
		if (iv1) pefree((void *)iv1, 1);
		if (iv2) pefree((void *)iv2, 1);
	}
}


int
mh_spacecmp_eq(
		const struct schema_space_value **lval,
		const struct schema_space_value **rval,
		void *arg) {
	(void )arg;
	if ((*lval)->key.id_len != (*rval)->key.id_len) return 0;
	return !memcmp((*lval)->key.id, (*rval)->key.id, (*rval)->key.id_len);
}

int
mh_spacecmp_key_eq(
		const struct schema_key *key,
		const struct schema_space_value **val,
		void *arg) {
	(void )arg;
	if (key->id_len != (*val)->key.id_len) return 0;
	return !memcmp(key->id, (*val)->key.id, key->id_len);
}

#define mh_arg_t void *

#define mh_eq(a, b, arg)      mh_spacecmp_eq(a, b, arg)
#define mh_eq_key(a, b, arg)  mh_spacecmp_key_eq(a, b, arg)
#define mh_hash(x, arg)       PMurHash32(MUR_SEED, (*x)->key.id, (*x)->key.id_len)
#define mh_hash_key(x, arg)   PMurHash32(MUR_SEED, x->id, x->id_len);

#define mh_node_t struct schema_space_value *
#define mh_key_t  struct schema_key *

#define MH_CALLOC(x, y) pecalloc((x), (y), 1)
#define MH_FREE(x)      pefree((x), 1)

#define mh_name               _schema_space
#define MH_SOURCE             1
#define MH_DEBUG              1
#include                      "third_party/mhash.h"

static inline void
schema_space_value_free(const struct schema_space_value *val) {
	if (val) {
		pefree(val->space_name, 1);
		/*
		int i = 0;
		for (i = val->schema_list_len; i > 0; --i)
			schema_field_value_free(&(val->schema_list[i - 1]));
		pefree(val->schema_list, 1);
		*/
		if (val->index_hash) {
			schema_index_free(val->index_hash);
			mh_schema_index_delete(val->index_hash);
		}
	}
}

static inline void
schema_space_free(struct mh_schema_space_t *schema) {
	int pos = 0;
	mh_int_t space_slot = 0;
	mh_foreach(schema, pos) {
		const struct schema_space_value *svalue = *mh_schema_space_node(schema, pos);
		const struct schema_space_value *sv1 = NULL, *sv2 = NULL;
		do {
			struct schema_key key_number = {(void *)&(svalue->space_number), sizeof(uint32_t)};
			space_slot = mh_schema_space_find(schema, &key_number, NULL);
			if (space_slot == mh_end(schema))
				break;
			sv1 = *mh_schema_space_node(schema, space_slot);
			mh_schema_space_del(schema, space_slot, NULL);
		} while (0);
		do {
			struct schema_key key_string = {svalue->space_name, svalue->space_name_len};
			space_slot = mh_schema_space_find(schema, &key_string, NULL);
			if (space_slot == mh_end(schema))
				break;
			sv2 = *mh_schema_space_node(schema, space_slot);
			mh_schema_space_del(schema, space_slot, NULL);
		} while (0);
		schema_space_value_free(svalue);
		if (sv1) pefree((void *)sv1, 1);
		if (sv2) pefree((void *)sv2, 1);
	}
}

static inline int
schema_add_space(
		struct mh_schema_space_t *schema,
		const char **data) {
	const char *tuple = *data;
	if (mp_typeof(*tuple) != MP_ARRAY) goto error;
	uint32_t tuple_len = mp_decode_array(&tuple);
	if (tuple_len < 6) goto error;
	struct schema_space_value *space_string = pemalloc(sizeof(struct schema_space_value), 1);
	if (!space_string) goto error;
	memset(space_string, 0, sizeof(struct schema_space_value));
	if (mp_typeof(*tuple) != MP_UINT) goto error;
	space_string->space_number = mp_decode_uint(&tuple);
	/* skip owner id */
	mp_next(&tuple);
	if (mp_typeof(*tuple) != MP_STR) goto error;
	const char *space_name_tmp = mp_decode_str(&tuple, &space_string->space_name_len);
	space_string->space_name = pemalloc(space_string->space_name_len, 1);
	if (!space_string->space_name) goto error;
	memcpy(space_string->space_name, space_name_tmp, space_string->space_name_len);
	/* skip engine name */
	mp_next(&tuple);
	/* skip field count */
	mp_next(&tuple);
	/* skip format */
	mp_next(&tuple);
	/* parse format */
	mp_next(&tuple);
	/* skip_format
	uint32_t fmp_tmp_len = 0;
	if (mp_typeof(*tuple) != MP_ARRAY) goto error;
	uint32_t fmt_len = mp_decode_array(&tuple);
	space_string->schema_list_len = fmt_len;
	space_string->schema_list = pecalloc(fmt_len, sizeof(struct schema_field_value), 1);
	if (!space_string->schema_list) goto error;
	while (fmt_len-- > 0) {
		struct schema_field_value *val = &(space_string->schema_list[
			(space_string->schema_list_len - fmt_len - 1)]);
		if (mp_typeof(*tuple) != MP_MAP) goto error;
		uint32_t arrsz = mp_decode_map(&tuple);
		while (arrsz-- > 0) {
			uint32_t sfield_len = 0;
			if (mp_typeof(*tuple) != MP_STR) goto error;
			const char *sfield = mp_decode_str(&tuple, &sfield_len);
			if (memcmp(sfield, "name", sfield_len) == 0) {
				if (mp_typeof(*tuple) != MP_STR) goto error;
				sfield = mp_decode_str(&tuple, &val->field_name_len);
				val->field_name = pemalloc(val->field_name_len, 1);
				if (!val->field_name) goto error;
				memcpy(val->field_name, sfield, val->field_name_len);
			} else if (memcmp(sfield, "type", sfield_len) == 0) {
				if (mp_typeof(*tuple) != MP_STR) goto error;
				sfield = mp_decode_str(&tuple, &sfield_len);
				switch(*sfield) {
				case ('s'):
				case ('S'):
					val->field_type = FT_STR;
					break;
				case ('n'):
				case ('N'):
					val->field_type = FT_NUM;
					break;
				default:
					val->field_type = FT_OTHER;
				}
			}
		}
	}
	*/
	space_string->index_hash = mh_schema_index_new();
	if (!space_string->index_hash) goto error;
	struct schema_space_value *space_number = pemalloc(sizeof(struct schema_space_value), 1);
	if (!space_number) goto error;
	memcpy(space_number, space_string, sizeof(struct schema_space_value));
	space_string->key.id = space_string->space_name;
	space_string->key.id_len = space_string->space_name_len;
	space_number->key.id = (void *)&(space_number->space_number);
	space_number->key.id_len = sizeof(uint32_t);
	mh_schema_space_put(schema,
			(const struct schema_space_value **)&space_string,
			NULL, NULL);
	mh_schema_space_put(schema,
			(const struct schema_space_value **)&space_number,
			NULL, NULL);
	*data = tuple;
	return 0;
error:
	schema_space_value_free(space_string);
	return -1;
}

int tarantool_schema_add_spaces(struct tarantool_schema *schema_obj,
				const char *data,
				uint32_t size) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	const char *tuple = data;
	if (mp_check(&tuple, tuple + size))
		return -1;
	tuple = data;
	if (mp_typeof(*tuple) != MP_ARRAY)
		return -1;
	uint32_t space_count = mp_decode_array(&tuple);
	while (space_count-- > 0) {
		if (schema_add_space(schema, &tuple))
			return -1;
	}
	return 0;
}

static inline int
schema_add_index(
		struct mh_schema_space_t *schema,
		const char **data) {
	const char *tuple = *data;
	if (mp_typeof(*tuple) != MP_ARRAY) goto error;
	int64_t tuple_len = mp_decode_array(&tuple);
	if (tuple_len < 6) goto error;
	uint32_t space_number = mp_decode_uint(&tuple);
	if (mp_typeof(*tuple) != MP_UINT) goto error;
	struct schema_key space_key = {(void *)&(space_number), sizeof(uint32_t)};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema, space_slot);
	struct schema_index_value *index_string = pemalloc(sizeof(struct schema_index_value), 1);
	if (!index_string) goto error;
	memset(index_string, 0, sizeof(struct schema_index_value));
	if (mp_typeof(*tuple) != MP_UINT) goto error;
	index_string->index_number = mp_decode_uint(&tuple);
	if (mp_typeof(*tuple) != MP_STR) goto error;
	const char *index_name_tmp = mp_decode_str(&tuple, &index_string->index_name_len);
	index_string->index_name = pemalloc(index_string->index_name_len, 1);
	if (!index_string->index_name) goto error;
	memcpy(index_string->index_name, index_name_tmp, index_string->index_name_len);
	/* skip index type */
	mp_next(&tuple);
	/* skip unique flag */
	mp_next(&tuple);
	mp_next(&tuple);
	/* skip fields
	uint32_t part_count = mp_decode_uint(&tuple);
	if (mp_typeof(*tuple) != MP_UINT) goto error;
	uint32_t rpart_count = part_count;
	index_string->index_parts = pecalloc(part_count, sizeof(struct schema_field_value), 1);
	if (!index_string->index_parts) goto error;
	memset(index_string->index_parts, 0, part_count * sizeof(struct schema_field_value));
	if (tuple_len - part_count * 2 != 6) goto error;
	while (part_count--) {
		struct schema_field_value *val = &(index_string->index_parts[rpart_count - part_count - 1]);
		if (mp_typeof(*tuple) != MP_UINT) goto error;
		val->field_number = mp_decode_uint(&tuple);
		uint32_t sfield_len = 0;
		if (mp_typeof(*tuple) != MP_STR) goto error;
		const char *sfield = mp_decode_str(&tuple, &sfield_len);
		switch(*sfield) {
			case ('s'):
			case ('S'):
				val->field_type = FT_STR;
				break;
			case ('n'):
			case ('N'):
				val->field_type = FT_NUM;
				break;
			default:
				val->field_type = FT_OTHER;
		}
		index_string->index_parts_len++;
	}
	*/
	struct schema_index_value *index_number = pemalloc(sizeof(struct schema_index_value), 1);
	if (!index_number) goto error;
	memcpy(index_number, index_string, sizeof(struct schema_index_value));
	index_string->key.id = index_string->index_name;
	index_string->key.id_len = index_string->index_name_len;
	index_number->key.id = (void *)&(index_number->index_number);
	index_number->key.id_len = sizeof(uint32_t);
	mh_schema_index_put(space->index_hash,
			(const struct schema_index_value **)&index_string,
			NULL, NULL);
	mh_schema_index_put(space->index_hash,
			(const struct schema_index_value **)&index_number,
			NULL, NULL);
	*data = tuple;
	return 0;
error:
	schema_index_value_free(index_string);
	return -1;
}

int tarantool_schema_add_indexes(struct tarantool_schema *schema_obj,
				 const char *data,
				 uint32_t size) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	const char *tuple = data;
	if (mp_check(&tuple, tuple + size))
		return -1;
	tuple = data;
	if (mp_typeof(*tuple) != MP_ARRAY)
		return -1;
	uint32_t space_count = mp_decode_array(&tuple);
	while (space_count-- > 0) {
		if (schema_add_index(schema, &tuple))
			return -1;
	}
	return 0;
}

int32_t tarantool_schema_get_sid_by_string(struct tarantool_schema *schema_obj,
					   const char *space_name,
					   uint32_t space_name_len) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {space_name, space_name_len};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema, space_slot);
	return space->space_number;
}

int32_t tarantool_schema_get_iid_by_string(struct tarantool_schema *schema_obj,
					   uint32_t sid, const char *index_name,
					   uint32_t index_name_len) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {(void *)&sid, sizeof(uint32_t)};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema, space_slot);
	struct schema_key index_key = {index_name, index_name_len};
	mh_int_t index_slot = mh_schema_index_find(space->index_hash, &index_key, NULL);
	if (index_slot == mh_end(space->index_hash))
		return -1;
	const struct schema_index_value *index = *mh_schema_index_node(space->index_hash, index_slot);
	return index->index_number;
}

struct tarantool_schema *tarantool_schema_new(int is_persistent) {
	struct tarantool_schema *obj = pemalloc(sizeof(struct tarantool_schema *), 1);
	obj->space_hash = mh_schema_space_new();
	return obj;
}

void tarantool_schema_flush(struct tarantool_schema *obj) {
	schema_space_free(obj->space_hash);
}

void tarantool_schema_delete(struct tarantool_schema *obj, int is_persistent) {
	if (obj == NULL) return;
	schema_space_free(obj->space_hash);
	mh_schema_space_delete(obj->space_hash);
	pefree(obj, 1);
}
