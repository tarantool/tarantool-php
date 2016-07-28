#ifndef   PHP_TNT_INTERNAL_H
#define   PHP_TNT_INTERNAL_H

#define TNT_INIT_CLASS_ENTRY(ce, name, name_ns, methods)			\
	if (TARANTOOL_G(use_namespace)) {					\
		printf("%s", name_ns);						\
		INIT_CLASS_ENTRY(ce, name_ns, methods);				\
	} else {								\
		printf("%s", name);						\
		INIT_CLASS_ENTRY(ce, name, methods);				\
	}

#endif /* PHP_TNT_INTERNAL_H */
