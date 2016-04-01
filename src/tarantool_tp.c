#include <stddef.h>

#include "php_tarantool.h"

#include "tarantool_tp.h"

struct tp_wrapper {
	smart_string *str;
	int is_persistent;
};

char *tarantool_tp_reserve(struct tp *p, size_t req, size_t *size) {
	struct tp_wrapper *wrap = (struct tp_wrapper *)p->obj;
	smart_string *str = wrap->str;
	if (str->a > str->len + req)
		return str->c;
	size_t needed = str->a * 2;
	if (str->len + req > needed)
		needed = str->len + req;
	register size_t __n1;
	smart_string_alloc4(str, needed, wrap->is_persistent, __n1);
	return str->c;
}

struct tp *tarantool_tp_new(smart_string *s, int is_persistent) {
	struct tp *tps = pecalloc(1, sizeof(struct tp), is_persistent);
	struct tp_wrapper *wrap = pecalloc(1, sizeof(struct tp_wrapper),
					   is_persistent);
	wrap->str = s;
	wrap->is_persistent = is_persistent;
	tp_init(tps, s->c, s->a, tarantool_tp_reserve, wrap);
	return tps;
}

void tarantool_tp_free(struct tp* tps, int is_persistent) {
	pefree(tps->obj, is_persistent);
	pefree(tps, is_persistent);
}

void tarantool_tp_flush(struct tp* tps) {
	tps->p = tps->s;
	tps->size = NULL;
	tps->sync = NULL;
}

void tarantool_tp_update(struct tp* tps) {
	tps->s = ((struct tp_wrapper *)(tps->obj))->str->c;
	tps->p = tps->s;
	tps->e = tps->s + ((struct tp_wrapper *)(tps->obj))->str->a;
}
