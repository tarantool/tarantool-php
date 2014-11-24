#ifndef   PHP_MANAGER_H
#define   PHP_MANAGER_H

#include <php.h>
#include <php_network.h>
#include <zend_API.h>
#include <ext/standard/php_smart_str.h>

#include <sys/queue.h>

#include "third_party/PMurHash.h"
#define MUR_SEED 13

TAILQ_HEAD(pool_list, pool_value);

struct pool_value {
	php_stream *connection;
	char *greeting;
	zval *schema_hash;
	TAILQ_ENTRY(pool_value) list_int;
};

struct manager_entry {
	char *prep_line;
	uint16_t size;
	struct pool_list *value;
};


struct mh_manager_t;

struct pool_manager {
	zend_bool persistent;
	int max_per_host;
	zend_bool deauthorize;
	struct mh_manager_t *pool;
};

struct tarantool_object;

struct pool_manager *pool_manager_create (zend_bool, int, zend_bool);
int                  pool_manager_free   (struct pool_manager *);

int pool_manager_find_apply  (struct pool_manager *, struct tarantool_object *);
int pool_manager_push_assure (struct pool_manager *, struct tarantool_object *);

#endif /* PHP_MANAGER_H */
