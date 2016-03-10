#include <stddef.h>

#include "php_tarantool.h"

#include "tarantool_tp.h"

char *tarantool_tp_reserve(struct tp *p, size_t req, size_t *size) {
	smart_string *str = (smart_string *)p->obj;
	if (str->a > str->len + req)
		return str->c;
	size_t needed = str->a * 2;
	if (str->len + req > needed)
		needed = str->len + req;
	register size_t __n1;
	smart_string_alloc4(str, needed, 1, __n1);
	return str->c;
}

struct tp *tarantool_tp_new(smart_string *s) {
	struct tp *tps = pemalloc(sizeof(struct tp), 1);
	tp_init(tps, s->c, s->a, tarantool_tp_reserve, s);
	return tps;
}

void tarantool_tp_free(struct tp* tps) {
	pefree(tps, 1);
}

void tarantool_tp_flush(struct tp* tps) {
	tps->p = tps->s;
	tps->size = NULL;
	tps->sync = NULL;
}

void tarantool_tp_update(struct tp* tps) {
	tps->s = ((smart_string *)(tps->obj))->c;
	tps->p = tps->s;
	tps->e = tps->s + ((smart_string *)(tps->obj))->a;
}
