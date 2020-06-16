#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#include "php_tarantool.h"
#include "tarantool_schema.h"
#include "tarantool_network.h"

#include "third_party/PMurHash.h"
#define MUR_SEED 13
#include "third_party/msgpuck.h"

int mh_indexcmp_eq(
		const struct schema_index_value **lval,
		const struct schema_index_value **rval,
		void *arg
) {
	(void )arg;
	if ((*lval)->key.id_len != (*rval)->key.id_len)
		return 0;
	return !memcmp((*lval)->key.id, (*rval)->key.id, (*rval)->key.id_len);
}

int mh_indexcmp_key_eq(
		const struct schema_key *key,
		const struct schema_index_value **val,
		void *arg
) {
	(void )arg;
	if (key->id_len != (*val)->key.id_len)
		return 0;
	return !memcmp(key->id, (*val)->key.id, key->id_len);
}

#define mh_arg_t void *

#define mh_eq(a, b, arg)     mh_indexcmp_eq(a, b, arg)
#define mh_eq_key(a, b, arg) mh_indexcmp_key_eq(a, b, arg)
#define mh_hash(x, arg)      PMurHash32(MUR_SEED, (*x)->key.id, (*x)->key.id_len)
#define mh_hash_key(x, arg)  PMurHash32(MUR_SEED, (x)->id, (x)->id_len);

#define mh_node_t struct schema_index_value *
#define mh_key_t  struct schema_key *

#define MH_CALLOC(x, y) pecalloc((x), (y), 1)
#define MH_FREE(x)      pefree((x), 1)

#define mh_name               _schema_index
#define MH_SOURCE             1
#include                      "third_party/mhash.h"

static inline void
schema_field_value_free(struct schema_field_value *val) {
	free(val->field_name);
}

static inline void
schema_index_value_free(const struct schema_index_value *val) {
	if (val) {
		pefree(val->index_name, 1);
		int i = 0;
		for (i = val->index_parts_len; i > 0; --i)
			schema_field_value_free(&(val->index_parts[i - 1]));
		pefree(val->index_parts, 1);
	}
}

static inline void
schema_index_free(struct mh_schema_index_t *schema) {
	int pos = 0;
	mh_int_t index_slot = 0;
	mh_foreach(schema, pos) {
		const struct schema_index_value *ivalue, *iv1 = NULL, *iv2 = NULL;
		ivalue = *mh_schema_index_node(schema, pos);
		do {
			struct schema_key key_number = {
				(void *)&(ivalue->index_number),
				sizeof(uint32_t), 0
			};
			index_slot = mh_schema_index_find(schema, &key_number,
							  NULL);
			if (index_slot == mh_end(schema))
				break;
			iv1 = *mh_schema_index_node(schema, index_slot);
			mh_schema_index_del(schema, index_slot, NULL);
		} while (0);
		do {
			struct schema_key key_string = {
				ivalue->index_name,
				ivalue->index_name_len, 0
			};
			index_slot = mh_schema_index_find(schema, &key_string,
							  NULL);
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
	if ((*lval)->key.id_len != (*rval)->key.id_len)
		return 0;
	return !memcmp((*lval)->key.id, (*rval)->key.id, (*rval)->key.id_len);
}

int
mh_spacecmp_key_eq(
		const struct schema_key *key,
		const struct schema_space_value **val,
		void *arg) {
	(void )arg;
	if (key->id_len != (*val)->key.id_len)
		return 0;
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
		int i = 0;
		for (i = val->schema_list_len; i > 0; --i)
			schema_field_value_free(&(val->schema_list[i - 1]));
		pefree(val->schema_list, 1);
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
		const struct schema_space_value *svalue, *sv1 = NULL, *sv2 = NULL;
		svalue = *mh_schema_space_node(schema, pos);
		do {
			struct schema_key key_number = {
				(void *)&(svalue->space_number),
				sizeof(uint32_t), 0
			};
			space_slot = mh_schema_space_find(schema, &key_number,
							  NULL);
			if (space_slot == mh_end(schema))
				break;
			sv1 = *mh_schema_space_node(schema, space_slot);
			mh_schema_space_del(schema, space_slot, NULL);
		} while (0);
		do {
			struct schema_key key_string = {
				svalue->space_name,
				svalue->space_name_len, 0
			};
			space_slot = mh_schema_space_find(schema, &key_string,
							  NULL);
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

int parse_field_type(const char *sfield, size_t sfield_len) {
	if (sfield_len == 3) {
		if (tolower(sfield[0]) == 's' &&
		    tolower(sfield[1]) == 't' &&
		    tolower(sfield[2]) == 'r')
			return FT_STR;
		if (tolower(sfield[0]) == 'n' &&
		    tolower(sfield[1]) == 'u' &&
		    tolower(sfield[2]) == 'm')
			return FT_NUM;
	}
	return FT_OTHER;
}

static inline int
parse_schema_space_value_value(struct schema_field_value *fld,
			       const char **tuple) {
	uint32_t sfield_len = 0;
	if (mp_typeof(**tuple) != MP_STR)
		goto error;
	const char *sfield = mp_decode_str(tuple, &sfield_len);
	if (memcmp(sfield, "name", sfield_len) == 0) {
		if (mp_typeof(**tuple) != MP_STR)
			goto error;
		sfield = mp_decode_str(tuple, &fld->field_name_len);
		fld->field_name = pemalloc(fld->field_name_len, 1);
		if (!fld->field_name)
			goto error;
		memcpy(fld->field_name, sfield, fld->field_name_len);
	} else if (memcmp(sfield, "type", sfield_len) == 0) {
		if (mp_typeof(**tuple) != MP_STR)
			goto error;
		sfield = mp_decode_str(tuple, &sfield_len);
		fld->field_type = parse_field_type(sfield, sfield_len);
	} else {
		mp_next(tuple);
	}
	return 0;
error:
	return -1;
}

static inline int
parse_schema_space_value(struct schema_space_value *space_string,
			 const char **tuple) {
	uint32_t fmp_tmp_len = 0;
	if (mp_typeof(**tuple) != MP_ARRAY)
		goto error;
	uint32_t fmt_len = mp_decode_array(tuple);
	space_string->schema_list_len = fmt_len;
	space_string->schema_list = pecalloc(fmt_len,
			sizeof(struct schema_field_value), 1);
	if (space_string->schema_list == NULL)
		goto error;
	int i = 0;
	for (i = 0; i < fmt_len; ++i) {
		struct schema_field_value *val = &(space_string->schema_list[i]);
		if (mp_typeof(**tuple) != MP_MAP)
			goto error;
		uint32_t arrsz = mp_decode_map(tuple);
		while (arrsz-- > 0) {
			if (parse_schema_space_value_value(val, tuple) < 0)
				goto error;
		}
		val->field_number = i;
	}
	return 0;
error:
	return -1;
}

static inline int
parse_schema_index_value(struct schema_index_value *index_string,
			 const char **tuple) {
	if (mp_typeof(**tuple) != MP_ARRAY)
		goto error;
	uint32_t part_count = mp_decode_array(tuple);
	index_string->index_parts_len = part_count;
	index_string->index_parts = pecalloc(part_count,
					     sizeof(struct schema_field_value),
					     1);
	if (!index_string->index_parts)
		goto error;
	memset(index_string->index_parts, 0, part_count *
	       sizeof(struct schema_field_value));
	int i = 0;
	for (i = 0; i < part_count; ++i) {
		if (mp_typeof(**tuple) != MP_ARRAY)
			goto error;
		if (mp_decode_array(tuple) != 2)
			goto error;
		struct schema_field_value *val = &(index_string->index_parts[i]);
		if (mp_typeof(**tuple) != MP_UINT)
			goto error;
		val->field_number = mp_decode_uint(tuple);
		uint32_t sfield_len = 0;
		if (mp_typeof(**tuple) != MP_STR)
			goto error;
		const char *sfield = mp_decode_str(tuple, &sfield_len);
		val->field_type = parse_field_type(sfield, sfield_len);
	}
	return 0;
error:
	return -1;
}

static inline int
schema_add_space(
		struct mh_schema_space_t *schema,
		const char **data
) {
	const char *tuple = *data;
	if (mp_typeof(*tuple) != MP_ARRAY)
		goto error;
	uint32_t tuple_len = mp_decode_array(&tuple);
	if (tuple_len < 6)
		goto error;
	struct schema_space_value *space_number = NULL, *space_string = NULL;
	space_string = pemalloc(sizeof(struct schema_space_value), 1);
	if (space_string == NULL)
		goto error;
	memset(space_string, 0, sizeof(struct schema_space_value));
	int pos = 0;
	for (pos = 0; pos < tuple_len; ++pos) {
		int error = 0;
		switch (pos) {
		/* space ID (uint) */
		case 0:
			if (mp_typeof(*tuple) != MP_UINT)
				goto error;
			space_string->space_number = mp_decode_uint(&tuple);
			break;
		/* owner ID (uint) */
		case 1:
			mp_next(&tuple);
			break;
		/* space name (str)*/
		case 2:
			if (mp_typeof(*tuple) != MP_STR)
				goto error;
			const char *space_name_tmp = mp_decode_str(&tuple,
					&space_string->space_name_len);
			space_string->space_name = pemalloc(
					space_string->space_name_len, 1);
			if (space_string->space_name == NULL)
				goto error;
			memcpy(space_string->space_name, space_name_tmp,
			       space_string->space_name_len);
			break;
		/* space engine (str) */
		case 3:
			mp_next(&tuple);
			break;
		/* field count (uint) */
		case 4:
			mp_next(&tuple);
			break;
		/* space flags (skipped, for now) */
		case 5:
			mp_next(&tuple);
			break;
		/*
		 * space format (array of maps)
		 * map's format is: {
		 * 	'name' = <str>,
		 * 	'type' = <str>
		 * }
		 */
		case 6:
			if (parse_schema_space_value(space_string, &tuple) < 0)
				goto error;
			break;
		default:
			break;
		}
	}
	space_string->index_hash = mh_schema_index_new();
	if (!space_string->index_hash)
		goto error;
	space_number = pemalloc(sizeof(struct schema_space_value), 1);
	if (!space_number)
		goto error;
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

int
tarantool_schema_add_spaces(
		struct tarantool_schema *schema_obj,
		const char *data,
		uint32_t size
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	const char *tuple = data;
	if (mp_check(&tuple, tuple + size))
		goto error;
	tuple = data;
	if (mp_typeof(*tuple) != MP_ARRAY)
		goto error;
	uint32_t space_count = mp_decode_array(&tuple);
	while (space_count-- > 0) {
		if (schema_add_space(schema, &tuple))
			goto error;
	}
	return 0;
error:
	return -1;
}

static inline int schema_add_index(
	struct mh_schema_space_t *schema,
	const char **data
) {
	uint32_t space_number = 0;
	struct schema_key space_key;
	const struct schema_space_value *space;
	struct schema_index_value *index_string, *index_number;

	const char *tuple = *data;
	/* parse index tuple header */
	if (mp_typeof(*tuple) != MP_ARRAY)
		goto error;
	int64_t tuple_len = mp_decode_array(&tuple);
	if (tuple_len < 6)
		goto error;

	index_string = pemalloc(sizeof(struct schema_index_value), 1);
	if (!index_string)
		goto error;
	memset(index_string, 0, sizeof(struct schema_index_value));
	index_number = pemalloc(sizeof(struct schema_index_value), 1);
	if (!index_number)
		goto error;
	memset(index_number, 0, sizeof(struct schema_index_value));
	int pos = 0;
	for (pos = 0; pos < tuple_len; ++pos) {
		int error = 0;
		switch (pos) {
		/* space ID */
		case 0:
			if (mp_typeof(*tuple) != MP_UINT)
				goto error;
			space_key.number = mp_decode_uint(&tuple);
			space_key.id = (void *)&(space_key.number);
			space_key.id_len = sizeof(uint32_t);
			break;
		/* index ID */
		case 1:
			if (mp_typeof(*tuple) != MP_UINT)
				goto error;
			index_string->index_number = mp_decode_uint(&tuple);
			break;
		/* index name */
		case 2:
			if (mp_typeof(*tuple) != MP_STR)
				goto error;
			const char *index_name_tmp = mp_decode_str(&tuple,
					&index_string->index_name_len);
			index_string->index_name = pemalloc(
					index_string->index_name_len, 1);
			if (!index_string->index_name)
				goto error;
			memcpy(index_string->index_name, index_name_tmp,
					index_string->index_name_len);
			break;
		/* index type */
		case 3:
			mp_next(&tuple);
			break;
		/* index opts */
		case 4:
			mp_next(&tuple);
			break;
		/* index parts */
		case 5:
			if (parse_schema_index_value(index_string, &tuple) < 0)
				goto error;
			break;
		default:
			break;
		}
	}
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		goto error;
	space = *mh_schema_space_node(schema, space_slot);
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

int
tarantool_schema_add_indexes(
		struct tarantool_schema *schema_obj,
		const char *data,
		uint32_t size
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	const char *tuple = data;
	if (mp_check(&tuple, tuple + size))
		goto error;
	tuple = data;
	if (mp_typeof(*tuple) != MP_ARRAY)
		goto error;
	uint32_t space_count = mp_decode_array(&tuple);
	while (space_count-- > 0) {
		if (schema_add_index(schema, &tuple))
			goto error;
	}
	return 0;
error:
	return -1;
}

int32_t
tarantool_schema_get_sid_by_string(
		struct tarantool_schema *schema_obj, const char *space_name,
		uint32_t space_name_len
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {
		space_name,
		space_name_len, 0
	};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema,
			space_slot);
	return space->space_number;
}

int32_t
tarantool_schema_get_sid_by_number(
		struct tarantool_schema *schema_obj,
		uint32_t sid
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {
		(void *)&sid,
		sizeof(uint32_t),
		sid, /* ignored */
	};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space =
		*mh_schema_space_node(schema, space_slot);
	return space->space_number;
}

int32_t
tarantool_schema_get_iid_by_string(
		struct tarantool_schema *schema_obj, uint32_t sid,
		const char *index_name, uint32_t index_name_len
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {
		(void *)&sid,
		sizeof(uint32_t), 0
	};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema,
			space_slot);
	struct schema_key index_key = {
		index_name,
		index_name_len, 0
	};
	mh_int_t index_slot = mh_schema_index_find(space->index_hash,
			&index_key, NULL);
	if (index_slot == mh_end(space->index_hash))
		return -1;
	const struct schema_index_value *index = *mh_schema_index_node(
			space->index_hash, index_slot);
	return index->index_number;
}

int32_t
tarantool_schema_get_fid_by_string(
		struct tarantool_schema *schema_obj, uint32_t sid,
		const char *field_name, uint32_t field_name_len
) {
	struct mh_schema_space_t *schema = schema_obj->space_hash;
	struct schema_key space_key = {
		(void *)&sid,
		sizeof(uint32_t), 0
	};
	mh_int_t space_slot = mh_schema_space_find(schema, &space_key, NULL);
	if (space_slot == mh_end(schema))
		return -1;
	const struct schema_space_value *space = *mh_schema_space_node(schema,
			space_slot);
	int i = 0;
	for (i = 0; i < space->schema_list_len; ++i) {
		struct schema_field_value *val = &space->schema_list[i];
		if (strncmp(val->field_name, field_name, field_name_len) == 0)
			return val->field_number;
	}
	return -1;
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
	if (obj == NULL)
		return;
	schema_space_free(obj->space_hash);
	mh_schema_space_delete(obj->space_hash);
	pefree(obj, 1);
}
