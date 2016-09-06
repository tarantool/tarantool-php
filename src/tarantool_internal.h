#ifndef   PHP_TNT_INTERNAL_H
#define   PHP_TNT_INTERNAL_H

#define TNT_INIT_CLASS_ENTRY(ce, name, name_ns, methods)			\
	if (TARANTOOL_G(use_namespace)) {					\
		INIT_CLASS_ENTRY(ce, name_ns, methods);				\
	} else {								\
		INIT_CLASS_ENTRY(ce, name, methods);				\
	}

#endif /* PHP_TNT_INTERNAL_H */
