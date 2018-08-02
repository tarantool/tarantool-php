#include "php_tarantool.h"

#include "tarantool_url.h"

#include "third_party/uri.h"

static const char *
url_part_copy(const char *part, size_t part_len, int persistent) {
	if (part == NULL || part_len == 0)
		return NULL;
	char *rv = pecalloc(sizeof(char), part_len + 1, persistent);
	rv[part_len] = '\0';
	memcpy(rv, part, part_len);
	return (const char *)rv;
}

struct tarantool_url *
tarantool_url_parse(const char *url, int persistent) {
	struct uri parsed; memset(&parsed, 0, sizeof(struct uri));

	if (uri_parse(&parsed, url) == -1)
		return NULL;

	/* Normalization of URI */
	enum tarantool_url_type tp = TARANTOOL_URL_NONE;
	const char *user = NULL,
		   *pass = NULL,
		   *host = NULL,
		   *port = NULL,
		   *path = NULL;
	size_t user_len = 0,
	       pass_len = 0,
	       host_len = 0,
	       port_len = 0,
	       path_len = 0;

	if ((parsed.host != NULL && strncmp(parsed.host, "unix/", sizeof("unix/") - 1) == 0) ||
	    (parsed.scheme == NULL || strncmp(parsed.scheme, "unix", sizeof("unix") - 1) == 0)) {
		if (parsed.query != NULL || parsed.fragment != NULL)
			return NULL;

		if (parsed.service_len == 0) parsed.service = NULL;
		if (parsed.path_len == 0)    parsed.path    = NULL;

		/* If both are set or not then throw error */
		if ((parsed.service == NULL) == (parsed.path == NULL))
			return NULL;

		path     = parsed.service ? parsed.service     : parsed.path;
		path_len = parsed.service ? parsed.service_len : parsed.path_len;

		while (path[0] == '/' && path[1] == '/') {
			path = path + 1;
			path_len = path_len - 1;
		}

		tp = TARANTOOL_URL_UNIX;
	} else if (parsed.scheme == NULL || strncmp(parsed.scheme, "tcp", sizeof("tcp") - 1) == 0) {
		if (parsed.query != NULL || parsed.fragment != NULL)
			return NULL;

		if (parsed.host == NULL) {
			host     = "localhost";
			host_len = sizeof("localhost") - 1;
		} else {
			host     = parsed.host;
			host_len = parsed.host_len;
		}
		if (parsed.service == NULL) {
			port     = "3303";
			port_len = sizeof("3303") - 1;
		} else {
			port     = parsed.service;
			port_len = parsed.service_len;
		}
		tp = TARANTOOL_URL_TCP;
	} else {
		return NULL;
	}

	if (parsed.login == NULL || parsed.login_len == 0) {
		user     = "guest";
		user_len = sizeof("guest") - 1;
	} else {
		user     = parsed.login;
		user_len = parsed.login_len;
	}

	if (parsed.login != NULL && parsed.login_len != 0) {
		pass     = parsed.password;
		pass_len = parsed.password_len;
	}

	struct tarantool_url *uri_parsed = NULL;
	uri_parsed = pecalloc(sizeof(struct tarantool_url), 1, persistent);
	if (uri_parsed == NULL)
		return NULL;

	uri_parsed->type = tp;
	uri_parsed->host = url_part_copy(host, host_len, persistent);
	uri_parsed->user = url_part_copy(user, user_len, persistent);
	uri_parsed->pass = url_part_copy(pass, pass_len, persistent);
	uri_parsed->path = url_part_copy(path, path_len, persistent);
	if (port)
		uri_parsed->port = atoi(port);

	// tarantool_dbg_url_print(uri_parsed);

	return uri_parsed;
}

inline static void
tarantool_url_free_component(char *url, int persistent) {
	if (url)
		pefree(url, persistent);
}

void tarantool_url_free(struct tarantool_url *url, int persistent) {
	if (url) {
		tarantool_url_free_component((void *)url->user, persistent);
		tarantool_url_free_component((void *)url->pass, persistent);
		tarantool_url_free_component((void *)url->host, persistent);
		tarantool_url_free_component((void *)url->path, persistent);
	}
	tarantool_url_free_component((void *)url, persistent);
}

const char *
tarantool_url_write_php_format(struct tarantool_url *url, int persistent) {
	char *rv = NULL;

	if (url->type == TARANTOOL_URL_TCP) {
		spprintf(&rv, 0, "tcp://%s:%d", url->host, url->port);
	} else if (url->type == TARANTOOL_URL_UNIX) {
		spprintf(&rv, 0, "unix://%s", url->path);
	} else {
		assert(false);
	}

	return rv;
}

void tarantool_dbg_uri_print(struct uri *inp) {
	printf("debug output of URI\n");
	printf("scheme:   '%.*s'\n", (int )inp->scheme_len,   inp->scheme);
	printf("login:    '%.*s'\n", (int )inp->login_len,    inp->login);
	printf("password: '%.*s'\n", (int )inp->password_len, inp->password);
	printf("host:     '%.*s'\n", (int )inp->host_len,     inp->host);
	printf("service:  '%.*s'\n", (int )inp->service_len,  inp->service);
	printf("path:     '%.*s'\n", (int )inp->path_len,     inp->path);
	printf("query:    '%.*s'\n", (int )inp->query_len,    inp->query);
	printf("fragment: '%.*s'\n", (int )inp->fragment_len, inp->fragment);
}

void tarantool_dbg_url_print(struct tarantool_url *inp) {
	char *type = NULL;

	switch (inp->type) {
	case TARANTOOL_URL_NONE:
		type = "UNDEF"; break;
	case TARANTOOL_URL_TCP:
		type = "TCP"; break;
	case TARANTOOL_URL_UNIX:
		type = "UNIX"; break;
	default:
		type = "BAD"; break;
	}

	printf("debug output of URL\n");
	printf("type: '%s'\n", type);
	printf("user: '%s'\n", inp->user);
	printf("pass: '%s'\n", inp->pass);
	printf("host: '%s'\n", inp->host);
	printf("path: '%s'\n", inp->path);
	printf("port: %d\n",   inp->port);
}

