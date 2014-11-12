#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>

#include "php_tarantool.h"
#include "tarantool_manager.h"

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
		while (*pos_val_p->value->tqh_last != NULL)
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
	entry->value = pemalloc(sizeof(struct pool_list), 1);
	TAILQ_INIT(entry->value);
	return entry;
}

int manager_entry_dequeue_delete (struct manager_entry *entry) {
	struct pool_value *pval = *entry->value->tqh_last;
	TSRMLS_FETCH();

	php_stream_close(pval->connection);
	pefree(pval->salt, 1);
	zval_ptr_dtor(&pval->schema_hash);
	TAILQ_REMOVE(entry->value, *entry->value->tqh_last, list_int);
	--entry->size;
	pefree(pval, 1);
}

int manager_entry_pop_apply (
		struct pool_manager *pool,
		struct manager_entry *entry,
		struct tarantool_object *obj
) {
	if (*entry->value->tqh_last == NULL)
		return 1;
	struct pool_value *pval = *entry->value->tqh_last;
	TAILQ_REMOVE(entry->value, *entry->value->tqh_last, list_int);
	obj->stream = pval->connection;
	obj->salt = pval->salt;
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
	temp_con->connection = obj->stream;
	temp_con->salt = obj->salt;
	temp_con->schema_hash = obj->schema_hash;
	entry->size++;
	TAILQ_INSERT_HEAD(entry->value, temp_con, list_int);
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
