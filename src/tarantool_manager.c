#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>

#include "php_tarantool.h"
#include "tarantool_manager.h"
#include "tarantool_proto.h"

#define mh_arg_t void *

#define mh_eq(a, b, arg)      (strcmp((*a)->prep_line, (*b)->prep_line) == 0)
#define mh_eq_key(a, b, arg)  (strcmp(a, (*b)->prep_line) == 0)
#define mh_hash(x, arg)       PMurHash32(MUR_SEED, (*x)->prep_line, strlen((*x)->prep_line))
#define mh_hash_key(x, arg)   PMurHash32(MUR_SEED, x, strlen(x));

typedef struct manager_entry *mh_node_t;
typedef const char           *mh_key_t;

#define MH_CALLOC(x, y) pecalloc(x, y, 1)
#define MH_FREE(x)      pefree(x, 1)

#define mh_name               _manager
#define MH_SOURCE             1
#define MH_INCREMENTAL_RESIZE 1
#include                      "third_party/mhash.h"

char *tarantool_tostr (
		struct tarantool_object *obj,
		struct pool_manager *pool
) {
	char *login = obj->login ? obj->login : "";
	char *fmt = (!pool->deauthorize ? "%s:%d:%s" : "%s:%d:");
	char *retval = pecalloc(256, sizeof(char), 1);
	snprintf(retval, 256, fmt, obj->host, obj->port, login);
	return retval;
}

char *tarantool_stream_persistentid(
		struct tarantool_object *obj
) {
	char  *persistent_id = pecalloc(256, sizeof(char), 1);
	TSRMLS_FETCH();
	snprintf(persistent_id, 256, "tarantool:%s:%d:%d",
		obj->host, obj->port, TARANTOOL_G(sync_counter));
	return persistent_id;
}

struct pool_manager *pool_manager_create (
		zend_bool persistent,
		int max_per_host,
		zend_bool deauthorize
) {
	struct pool_manager *manager = pemalloc(sizeof(struct pool_manager), 1);
	manager->max_per_host = max_per_host;
	manager->persistent = persistent;
	manager->deauthorize = deauthorize;
	manager->pool = mh_manager_new();
	return manager;
}

int pool_manager_free (struct pool_manager *manager) {
	if (manager == NULL)
		return 0;
	int pos = 0;
	mh_foreach(manager->pool, pos) {
		struct manager_entry *pos_val_p = *mh_manager_node(manager->pool, pos);
		pefree(pos_val_p->prep_line, 1);
		while (pos_val_p->value.end != NULL)
			manager_entry_dequeue_delete(pos_val_p);
		pefree(pos_val_p, 1);
	}
	mh_manager_delete(manager->pool);
}

struct manager_entry *manager_entry_create (
		struct pool_manager *pool,
		struct tarantool_object *obj
) {
	struct manager_entry *entry = pemalloc(sizeof(struct manager_entry), 1);
	entry->prep_line = tarantool_tostr(obj, pool);
	entry->size = 0;
	entry->value.begin = entry->value.end = 0;
	return entry;
}

int manager_entry_dequeue_delete (struct manager_entry *entry) {
	struct pool_value *pval = entry->value.begin;
	assert (pval != NULL);
	TSRMLS_FETCH();

	if (pval->greeting)      pefree(pval->greeting, 1);
	if (pval->persistent_id) pefree(pval->persistent_id, 1);
	if (pval->schema_hash)   zval_ptr_dtor(&pval->schema_hash);
	if (entry->value.begin == entry->value.end)
		entry->value.begin = entry->value.end = NULL;
	else
		entry->value.begin = entry->value.begin->next;
	pval->next = NULL;
	--entry->size;
	pefree(pval, 1);
}

int manager_entry_pop_apply (
		struct pool_manager *pool,
		struct manager_entry *entry,
		struct tarantool_object *obj
) {
	if (entry->value.end == NULL)
		return 1;
	struct pool_value *pval = entry->value.end;
	if (entry->value.begin == entry->value.end)
		entry->value.begin = entry->value.end = NULL;
	else
		entry->value.begin = entry->value.begin->next;
	if (obj->persistent_id) pefree(obj->persistent_id, 1);
	if (obj->greeting)      pefree(obj->greeting, 1);
	obj->persistent_id = pval->persistent_id;
	obj->greeting = pval->greeting;
	obj->salt = pval->greeting + SALT_PREFIX_SIZE;
	obj->schema_hash = pval->schema_hash;
	--entry->size;
	return 0;
}

int manager_entry_enqueue_assure (
		struct pool_manager *pool,
		struct manager_entry *entry,
		struct tarantool_object *obj
) {
	if (entry->size == pool->max_per_host)
		manager_entry_dequeue_delete(entry);
	struct pool_value *temp_con = pemalloc(sizeof(struct pool_value), 1);
	temp_con->persistent_id = obj->persistent_id;
	obj->persistent_id = NULL;
	temp_con->greeting = obj->greeting;
	obj->greeting = NULL;
	temp_con->schema_hash = obj->schema_hash;
	obj->schema_hash = NULL;
	temp_con->next = NULL;
	entry->size++;
	if (entry->value.begin == NULL)
		entry->value.begin = entry->value.end = temp_con;
	else
		entry->value.end = entry->value.end->next = temp_con;

	return 0;
}

int pool_manager_find_apply (
		struct pool_manager *pool,
		struct tarantool_object *obj
) {
	if (!pool->persistent)
		return 1;
	char *key = tarantool_tostr(obj, pool);
	mh_int_t pos = mh_manager_find(pool->pool, key, NULL);
	pefree(key, 1);
	if (pos == mh_end(pool->pool))
		return 1;
	struct manager_entry *t = *mh_manager_node(pool->pool, pos);
	return manager_entry_pop_apply(pool, t, obj);
}

int pool_manager_push_assure (
		struct pool_manager *pool,
		struct tarantool_object *obj
) {
	if (!pool->persistent)
		return 1;
	char *key = tarantool_tostr(obj, pool);
	mh_int_t pos = mh_manager_find(pool->pool, key, NULL);
	pefree(key, 1);
	struct manager_entry *t = NULL;
	if (pos == mh_end(pool->pool)) {
		t = manager_entry_create(pool, obj);
		mh_manager_put(pool->pool, &t, NULL, NULL);
	} else {
		t = *mh_manager_node(pool->pool, pos);
	}
	return manager_entry_enqueue_assure(pool, t, obj);
}
