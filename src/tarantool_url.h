#ifndef   PHP_TNT_URL_EXT_H
#define   PHP_TNT_URL_EXT_H

struct uri;

enum tarantool_url_type {
	TARANTOOL_URL_NONE = 0,
	TARANTOOL_URL_UNIX = 1,
	TARANTOOL_URL_TCP  = 2,
	TARANTOOL_URL_MAX  = 3,
};

struct tarantool_url {
	enum tarantool_url_type  type;
	const char              *user;
	const char              *pass;
	const char              *host;
	const char              *path;
	int                      port;
};

struct tarantool_url *
tarantool_url_parse(const char *url, int persistent);

void
tarantool_url_free(struct tarantool_url *url, int persistent);

const char *
tarantool_url_write_php_format(struct tarantool_url *url, int persistent);

/* Debug routines for printing different URI structs */
void tarantool_dbg_uri_print(struct uri *inp);
void tarantool_dbg_url_print(struct tarantool_url *inp);

#endif /* PHP_TNT_URL_EXT_H */
