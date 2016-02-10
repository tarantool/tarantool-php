#ifndef   PHP_NETWORK_H
#define   PHP_NETWORK_H

struct timeval;
struct timespec;

void double_to_tv(double tm, struct timeval *tv);
void double_to_ts(double tm, struct timespec *ts);

void tntll_stream_close(php_stream *stream, const char *pid);
/*
 * Simple php_stream_from_persistent_id
 */
int  tntll_stream_fpid2(const char *pid, php_stream **ostream);
/*
 * php_stream_from_persistent_id if persistent.
 * If exists or if not persistent, then open new connection.
 */
int  tntll_stream_fpid (const char *host, int port, const char *pid,
			php_stream **ostream, char **err);
int  tntll_stream_open (const char *host, int port, const char *pid,
			php_stream **ostream, char **err);
/*
 * Read size bytes once
 */
int  tntll_stream_read (php_stream *stream, char *buf, size_t size);
/*
 * Read size bytes exactly (if not error)
 */
int  tntll_stream_read2(php_stream *stream, char *buf, size_t size);
int  tntll_stream_send (php_stream *stream, char *buf, size_t size);

#endif /* PHP_NETWORK_H */
