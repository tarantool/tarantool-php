#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "php_tarantool.h"
#include "tarantool_network.h"

void double_to_tv(double tm, struct timeval *tv) {
	tv->tv_sec = floor(tm);
	tv->tv_usec = floor((tm - floor(tm)) * pow(10, 6));
}

void double_to_ts(double tm, struct timespec *ts) {
	ts->tv_sec = floor(tm);
	ts->tv_nsec = floor((tm - floor(tm)) * pow(10, 9));
}

/* `pid` means persistent_id */
// void tntll_stream_close(php_stream *stream, const char *pid) {
void tntll_stream_close(php_stream *stream, zend_string *pid) {
	int rv = PHP_STREAM_PERSISTENT_SUCCESS;
	if (stream == NULL) {
		rv = tntll_stream_fpid2(pid, &stream);
	}
	if (rv == PHP_STREAM_PERSISTENT_SUCCESS && stream != NULL) {
		if (pid) {
			php_stream_free(stream, PHP_STREAM_FREE_CLOSE_PERSISTENT);
		} else {
			php_stream_close(stream);
		}
	}
}

int tntll_stream_fpid2(zend_string *pid, php_stream **ostream) {
	TSRMLS_FETCH();
	return php_stream_from_persistent_id(pid->val, ostream TSRMLS_CC);
}

php_stream *tntll_stream_open(const char *url, zend_string *pid, char **err) {
	TSRMLS_FETCH();
	php_stream *stream = NULL;
	zend_string *errstr = NULL;

	int options = REPORT_ERRORS;
	if (pid) {
		options |= STREAM_OPEN_PERSISTENT;
	}
	int flags = STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT;

	struct timeval connect_tv = {0};
	double_to_tv(TARANTOOL_G(timeout), &connect_tv);

	int errcode = 0;
	const char *pid_str = (pid == NULL ? NULL : pid->val);
	stream = php_stream_xport_create(url, strlen(url), options, flags,
					 pid_str, &connect_tv, NULL, &errstr,
					 &errcode);

	if (errcode || !stream) {
		spprintf(err, 0, "Failed to connect [%d]: %s", errcode,
			 ZSTR_VAL(errstr));
		goto error;
	}

	/* Set READ_TIMEOUT */
	struct timeval read_tv = {0};
	double_to_tv(TARANTOOL_G(request_timeout), &read_tv);

	if (read_tv.tv_sec != 0 || read_tv.tv_usec != 0) {
		php_stream_set_option(stream, PHP_STREAM_OPTION_READ_TIMEOUT,
				      0, &read_tv);
	}

	/* Set TCP_NODELAY in case of TCP socket */
	if (memcmp(url, "tcp://", sizeof("tcp://") - 1) == 0) {
		int socketd = ((php_netstream_data_t* )stream->abstract)->socket;
		int flags = 1;
		if (setsockopt(socketd, IPPROTO_TCP, TCP_NODELAY, (char *) &flags,
			       sizeof(int))) {
			spprintf(err, 0, "Failed setsockopt [%d]: %s", errno,
				 strerror(errno));
			goto error;
		}
	}

	return stream;
error:
	if (errstr) zend_string_release(errstr);
	if (stream) tntll_stream_close(NULL, pid);
	return NULL;
}

php_stream *tntll_stream_open_unix(const char *path, zend_string *pid,
				   char **err) {
	char *url = NULL;
	size_t url_len = spprintf(&url, 0, "unix://%s", path);
	if (url == NULL)
		return NULL;
	php_stream *out = tntll_stream_open(url, pid, err);
	efree(url);
	return out;
}

php_stream *tntll_stream_open_tcp(const char *host, int port, zend_string *pid,
				  char **err) {
	char *url = NULL;
	size_t url_len = spprintf(&url, 0, "tcp://%s:%d", host, port);
	if (url == NULL)
		return NULL;
	php_stream *out = tntll_stream_open(url, pid, err);
	efree(url);
	return out;
}

/*
 * Legacy rtsisyk code, php_stream_read made right
 * See https://bugs.launchpad.net/tarantool/+bug/1182474
 */
size_t tntll_stream_read2(php_stream *stream, char *buf, size_t size) {
	TSRMLS_FETCH();
	size_t total_size = 0;
	ssize_t read_size = 0;

	while (total_size < size) {
		read_size = php_stream_read(stream, buf + total_size,
					    size - total_size);
		assert(read_size + total_size <= size);
		if (read_size <= 0)
			break;
		total_size += read_size;
	}

	return total_size;
}

bool tntll_stream_is_timedout() {
	int err = php_socket_errno();
	if (err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS)
		return 1;
	return 0;
}

int tntll_stream_read(php_stream *stream, char *buf, size_t size) {
	TSRMLS_FETCH();
	return php_stream_read(stream, buf, size);
}

int tntll_stream_send(php_stream *stream, char *buf, size_t size) {
	TSRMLS_FETCH();
	if (php_stream_write(stream, buf, size) != size ||
	    php_stream_flush(stream))
		return -1;
	return 0;
}
