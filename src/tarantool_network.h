#ifndef   PHP_TNT_NETWORK_H
#define   PHP_TNT_NETWORK_H

struct timeval;
struct timespec;

void double_to_tv(double tm, struct timeval *tv);
void double_to_ts(double tm, struct timespec *ts);

void tntll_stream_close(php_stream *stream, zend_string *pid);
/*
 * Simple php_stream_from_persistent_id
 */
int  tntll_stream_fpid2(zend_string *pid, php_stream **ostream);

php_stream *
tntll_stream_open(const char *url, zend_string *pid, char **err);
/*
 * Read size bytes once
 */
int  tntll_stream_read (php_stream *stream, char *buf, size_t size);
/*
 * Read size bytes exactly (if not error)
 */
size_t tntll_stream_read2(php_stream *stream, char *buf, size_t size);
int  tntll_stream_send (php_stream *stream, char *buf, size_t size);

bool tntll_stream_is_timedout();

#endif /* PHP_TNT_NETWORK_H */
