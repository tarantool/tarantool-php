#include "src/third_party/tp.h"

struct tp *tarantool_tp_new(smart_string *s, int is_persistent);
void tarantool_tp_free(struct tp* tps, int is_persistent);
void tarantool_tp_flush(struct tp* tps);
void tarantool_tp_update(struct tp* tps);
