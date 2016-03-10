#ifndef   PHP_MANAGER_H
#define   PHP_MANAGER_H

#include <php_tarantool.h>

#include "third_party/PMurHash.h"
#define MUR_SEED 13

struct pool_value {
	char *persistent_id;
	char *greeting;
	struct tarantool_schema *schema;
	struct pool_value *next;
};

struct manager_entry {
	char *prep_line;
	uint16_t size;
	struct {
		struct pool_value *begin;
		struct pool_value *end;
	} value;
};

struct mh_manager_t;

struct pool_manager {
	zend_bool persistent;
	int max_per_host;
	struct mh_manager_t *pool;
#ifdef    ZTS
	MUTEX_T mutex;
#endif /* ZTS */
};

struct tarantool_object;

char *tarantool_stream_pid(struct tarantool_object *);

struct pool_manager *pool_manager_create (zend_bool, int);
int                  pool_manager_free   (struct pool_manager *);

int pool_manager_find_apply  (struct pool_manager *, struct tarantool_object *);
int pool_manager_push_assure (struct pool_manager *, struct tarantool_object *);

#endif /* PHP_MANAGER_H */
