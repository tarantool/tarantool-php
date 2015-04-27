#include "src/third_party/tp.h"

struct tp *tarantool_tp_new(smart_str *s);
void tarantool_tp_free(struct tp* tps);
void tarantool_tp_flush(struct tp* tps);
void tarantool_tp_update(struct tp* tps);
