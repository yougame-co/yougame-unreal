/* yougame_net.h: online play for a native YouGame build through the YouGame app's network broker
 * (yougame-native.json "net": "yougame"), with no sockets of its own. The app opens YouGame-only
 * WebSocket connections for the game over the stdio bridge: netOpen, netSend and netClose requests
 * on stdout, `net` pushes on stdin (or the same lines over yougame.h's local channel).
 * https://yougame.co/sdk.md, "Going online through YouGame".
 *
 * C99 or C++, the C runtime only. In exactly one source file:
 *
 *     #define YGNET_IMPLEMENTATION
 *     #include "yougame_net.h"
 *
 * Initializing this client retains the ASCII marker "YGNET/1" in the binary for YouGame's upload scan.
 *
 * Not thread-safe: make every call, ygnet_feed and ygnet_dispatch included, on one thread (or
 * under one lock). Callbacks run inside ygnet_feed / ygnet_dispatch / ygnet_eof. Everything the
 * game itself writes to stdout must end each line with "\n", or the next request joins it.
 */
#ifndef YOUGAME_NET_H
#define YOUGAME_NET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest stdin line kept, in bytes (the bridge allows 48 MB a line); longer ones are dropped. */
#define YGNET_MAX_LINE ((size_t)64 << 20)

enum {
  YGNET_EINVAL = -1,
  YGNET_ENOMEM = -2,
  YGNET_EWRITE = -3,
  YGNET_ENOBROKER = -4,
  YGNET_ETICKET = -5,
  YGNET_EUNSUPPORTED = -6,
  YGNET_ELIMIT = -7,
  YGNET_EBUSY = -8,
  YGNET_ENOCONN = -9,
  YGNET_ENOTOPEN = -10,
  YGNET_ETOOBIG = -11,
  YGNET_EUTF8 = -12,
  YGNET_EENDED = -13,
  YGNET_EVERSION = -14
};

typedef struct ygnet ygnet;

/* hello's netBroker: its version and mode, and its limits. */
typedef struct ygnet_limits {
  int version;
  int internet; /* mode "internet": the build may also open sockets itself */
  long max_connections;
  long opens_per_second; /* netOpen: open_burst at once, then this many a second */
  long open_burst;
  long max_message_bytes;
  long max_world_message_bytes; /* a relay world's (a world ticket without a server) */
  long max_server_inbound_bytes;
  long max_relay_inbound_bytes;
  long messages_per_second;
  long bytes_per_second;
} ygnet_limits;

typedef struct ygnet_config {
  void *user;
  /* Writes one whole line ("\n" included) to the app. Default: stdout, flushed. Nonzero: failed. */
  int (*write)(void *user, const char *line, size_t len);
  /* The id of each request this client writes, within +-2^53. Default: its own counter, ygnet_next_id. */
  long long (*next_id)(void *user);
  /* A stdin line that is not the client's: the game's own answers and pushes, "embed ..." lines.
   * NUL-terminated, without its newline; the game may change it in place. */
  void (*on_line)(void *user, char *line, size_t len);
  void (*on_open)(void *user, int conn);
  /* One text message, UTF-8, NUL-terminated (data[len] == 0; it may also hold NULs). Only while
   * open: none before on_open or after ygnet_close, as a browser's socket. */
  void (*on_message)(void *user, int conn, const char *data, size_t len);
  /* `code` is the broker's (too_large, rate_messages, rate_opens, refused, unreachable,
   * game_not_reading, ended, ...), "denied" (the app refused netOpen without one), "failed" (a send
   * or a close refused without one), or "line_dropped" with conn 0. */
  void (*on_error)(void *user, int conn, const char *code, const char *message);
  /* The last event of a connection, exactly once. */
  void (*on_close)(void *user, int conn, int code, const char *reason);
  size_t max_line;  /* 0: YGNET_MAX_LINE; at most 1 GB */
  int max_waiting;  /* this client's requests unanswered at once, 5 to 32; 0: 24 (the bridge takes
                       32 in all). Opens and sends leave 4 of them to closes. */
} ygnet_config;

typedef struct ygnet_open_opts {
  const char *room; /* a queue ticket: the room its connection's {"t":"matched","room":...} named */
  const char *pass; /* a private room's code, at most 24 characters */
  const char *name; /* a relay world's display name, at most 40 characters */
} ygnet_open_opts;

ygnet *ygnet_create(const ygnet_config *cfg);
void ygnet_destroy(ygnet *net);
/* The shared request counter: take the game's own bridge request ids from here (or set next_id). */
long long ygnet_next_id(ygnet *net);
/* Sends hello and reads netBroker from its answer, which still goes to on_line. Its id, or < 0. */
long long ygnet_hello(ygnet *net);
/* The game sent hello itself under this id. */
void ygnet_watch_hello(ygnet *net, long long id);
/* A hello result already consumed by another client (yougame.h: yg_hello_json() in on_ready /
 * on_player). Does not write or mutate the result; stdio hello/watch remain unchanged. */
int ygnet_hello_result(ygnet *net, const char *json, size_t len);
/* 0: no hello answer yet; 1: the broker is there; YGNET_ENOBROKER: not in this run, play offline;
 * YGNET_EVERSION: a broker this client does not speak. */
int ygnet_status(const ygnet *net);
const ygnet_limits *ygnet_broker(const ygnet *net); /* NULL unless ygnet_status is 1 */
/* `ticket`: an mpTicket answer as received (the line, or its result object), or the bare ticket.
 * A connection handle > 0 (on_open follows, or on_error then on_close), or < 0. */
int ygnet_open(ygnet *net, const char *ticket, const ygnet_open_opts *opts);
int ygnet_send(ygnet *net, int conn, const char *text);
int ygnet_send_n(ygnet *net, int conn, const char *data, size_t len);
/* code: 0 (1000), 1000 or 3000 to 4999; reason: NULL or at most 123 bytes. on_close follows; a
 * close the app refuses is an on_error, and the connection stays as it was. */
int ygnet_close(ygnet *net, int conn, int code, const char *reason);
/* Bytes read from stdin: every whole line is handled, and those not the client's go to on_line. */
int ygnet_feed(ygnet *net, const void *bytes, size_t n);
/* For a game that splits stdin into lines itself: 1 when the line was the client's. May change it. */
int ygnet_dispatch(ygnet *net, char *line, size_t len);
/* stdin ended (the app is gone): every connection gets on_error "ended" and on_close 1006, and
 * every later call is refused (YGNET_EENDED). */
void ygnet_eof(ygnet *net);
const char *ygnet_strerror(int err);

/* JSON helpers for the game's own frames. A top-level string field of an object, unescaped into
 * out (NUL-terminated): its length, -1 when absent or not a string, -2 when cap is too small. */
long ygnet_json_str(const char *json, size_t len, const char *key, char *out, size_t cap);
/* s as a JSON string with its quotes: the length it needs (without the NUL), written with a NUL
 * when cap is larger than that. */
size_t ygnet_json_quote(char *out, size_t cap, const char *s, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* YOUGAME_NET_H */

#ifdef YGNET_IMPLEMENTATION
#ifndef YGNET_IMPLEMENTED
#define YGNET_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { YGNET__SLOTS = 8, YGNET__WAIT = 32, YGNET__TICKET = 4096, YGNET__CLOSES = 4 };
enum { YGNET__OPEN = 1, YGNET__SEND, YGNET__CLOSE };
enum { YGNET__OPENING = 1, YGNET__UP, YGNET__CLOSING };
#define YGNET__LINE_CAP ((size_t)1 << 30)

typedef struct {
  long long id;
  int kind, conn;
} ygnet__wait;

typedef struct {
  int handle, state, up, told, relay_world, want_close, close_code;
  long long app;      /* the app's connection id, 0 until netOpen is answered */
  long long close_id; /* the netClose waiting for its answer */
  char reason[124];
} ygnet__conn;

/* What the pushes of a connection said before netOpen's answer named it. */
typedef struct {
  long long app;
  int opened, failed, closed, close_code;
  char code[32], message[256], reason[124];
} ygnet__early;

struct ygnet {
  ygnet_config cfg;
  ygnet_limits lim;
  long long counter, hello_id;
  int hello_watch, status, handles, nwait, dropping, ended;
  ygnet__wait wait[YGNET__WAIT];
  ygnet__conn conn[YGNET__SLOTS];
  ygnet__early early[YGNET__SLOTS];
  char *in, *out;
  size_t in_len, in_cap, out_cap;
};

typedef struct {
  ygnet *net;
  size_t n;
  int err;
} ygnet__buf;

static int ygnet__stdout(void *user, const char *line, size_t len) {
  (void)user;
  return fwrite(line, 1, len, stdout) == len && fflush(stdout) == 0 ? 0 : -1;
}

/* ---- JSON in: bounded by `e`, never past it; values are found, not validated. */

static const char *ygnet__ws(const char *p, const char *e) {
  while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  return p;
}

static const char *ygnet__skip_str(const char *p, const char *e) {
  for (p++; p < e; p++) {
    if (*p == '\\') {
      p++;
      if (p >= e) return NULL;
    } else if (*p == '"') {
      return p + 1;
    }
  }
  return NULL;
}

static const char *ygnet__skip(const char *p, const char *e) {
  const char *s;
  p = ygnet__ws(p, e);
  if (p >= e) return NULL;
  if (*p == '"') return ygnet__skip_str(p, e);
  if (*p == '{' || *p == '[') {
    long depth = 0;
    while (p < e) {
      if (*p == '"') {
        p = ygnet__skip_str(p, e);
        if (!p) return NULL;
        continue;
      }
      if (*p == '{' || *p == '[') {
        depth++;
      } else if (*p == '}' || *p == ']') {
        depth--;
        if (depth == 0) return p + 1;
      }
      p++;
    }
    return NULL;
  }
  s = p;
  while (p < e && (unsigned char)*p > ' ' && *p != ',' && *p != '}' && *p != ']' && *p != ':') p++;
  return p > s ? p : NULL;
}

/* One pass over the object at p: vals[i] is where keys[i]'s value starts (before e), or NULL.
 * Stops once `need` keys are found. 0, with every vals[i] NULL, when p is not an object. */
static int ygnet__obj(const char *p, const char *e, const char **keys, const char **vals, int n, int need) {
  int i, found = 0;
  for (i = 0; i < n; i++) vals[i] = NULL;
  if (!p || !e) return 0;
  p = ygnet__ws(p, e);
  if (p >= e || *p != '{') return 0;
  p = ygnet__ws(p + 1, e);
  if (p < e && *p == '}') return 1;
  while (p < e && *p == '"') {
    const char *k = p + 1, *q = ygnet__skip_str(p, e);
    size_t kl;
    if (!q) goto bad;
    kl = (size_t)(q - 1 - k);
    p = ygnet__ws(q, e);
    if (p >= e || *p != ':') goto bad;
    p = ygnet__ws(p + 1, e);
    if (p >= e) goto bad;
    for (i = 0; i < n; i++) {
      if (!vals[i] && strlen(keys[i]) == kl && memcmp(keys[i], k, kl) == 0) {
        vals[i] = p;
        found++;
        if (found >= need) return 1;
      }
    }
    p = ygnet__skip(p, e);
    if (!p) goto bad;
    p = ygnet__ws(p, e);
    if (p < e && *p == ',') {
      p = ygnet__ws(p + 1, e);
      continue;
    }
    if (p < e && *p == '}') return 1;
    goto bad;
  }
bad:
  for (i = 0; i < n; i++) vals[i] = NULL;
  return 0;
}

static int ygnet__int(const char *p, const char *e, long long *out) {
  long long v = 0;
  int neg = 0, digits = 0;
  if (!p || p >= e) return 0;
  if (*p == '-') {
    neg = 1;
    p++;
  }
  for (; p < e && *p >= '0' && *p <= '9'; p++) {
    digits++;
    if (digits > 17) return 0;
    v = v * 10 + (*p - '0');
  }
  if (!digits || (p < e && (*p == '.' || *p == 'e' || *p == 'E'))) return 0;
  *out = neg ? -v : v;
  return 1;
}

/* The JSON string at p is exactly `s`, as written (no escapes). */
static int ygnet__is(const char *p, const char *e, const char *s) {
  size_t n = strlen(s);
  return p && p < e && (size_t)(e - p) >= n + 2 && p[0] == '"' && memcmp(p + 1, s, n) == 0 && p[n + 1] == '"';
}

static int ygnet__true(const char *p, const char *e) {
  return p && p < e && e - p >= 4 && memcmp(p, "true", 4) == 0;
}

static int ygnet__hex4(const char *p, const char *e, unsigned long *u) {
  unsigned long v = 0;
  int i;
  if (e - p < 4) return 0;
  for (i = 0; i < 4; i++) {
    char c = p[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0) return 0;
    v = v * 16 + (unsigned long)d;
  }
  *u = v;
  return 1;
}

static int ygnet__put(char *o, size_t cap, size_t *n, unsigned long ch) {
  if (*n >= cap) return -1;
  o[*n] = (char)(unsigned char)ch;
  *n += 1;
  return 0;
}

static int ygnet__put_utf8(char *o, size_t cap, size_t *n, unsigned long u) {
  if (u < 0x80) return ygnet__put(o, cap, n, u);
  if (u < 0x800) return ygnet__put(o, cap, n, 0xC0 | (u >> 6)) || ygnet__put(o, cap, n, 0x80 | (u & 0x3F));
  if (u < 0x10000)
    return ygnet__put(o, cap, n, 0xE0 | (u >> 12)) || ygnet__put(o, cap, n, 0x80 | ((u >> 6) & 0x3F)) || ygnet__put(o, cap, n, 0x80 | (u & 0x3F));
  return ygnet__put(o, cap, n, 0xF0 | (u >> 18)) || ygnet__put(o, cap, n, 0x80 | ((u >> 12) & 0x3F)) ||
         ygnet__put(o, cap, n, 0x80 | ((u >> 6) & 0x3F)) || ygnet__put(o, cap, n, 0x80 | (u & 0x3F));
}

/* The JSON string at p, unescaped into o as UTF-8 and NUL-terminated (a lone surrogate becomes
 * U+FFFD). o may be p + 1: nothing is written ahead of what was read. Length, -1 bad, -2 small. */
static long ygnet__unescape(const char *p, const char *e, char *o, size_t cap) {
  size_t n = 0;
  if (!p || p >= e || *p != '"') return -1;
  for (p++; p < e;) {
    unsigned long u = 0, lo = 0;
    unsigned char c = (unsigned char)*p++;
    if (c == '"') {
      if (n >= cap) return -2;
      o[n] = 0;
      return (long)n;
    }
    if (c != '\\') {
      if (ygnet__put(o, cap, &n, c)) return -2;
      continue;
    }
    if (p >= e) return -1;
    c = (unsigned char)*p++;
    switch (c) {
      case 'b': u = 8; break;
      case 'f': u = 12; break;
      case 'n': u = 10; break;
      case 'r': u = 13; break;
      case 't': u = 9; break;
      case 'u':
        if (!ygnet__hex4(p, e, &u)) return -1;
        p += 4;
        if (u >= 0xD800 && u <= 0xDBFF && e - p >= 6 && p[0] == '\\' && p[1] == 'u' && ygnet__hex4(p + 2, e, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
          u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
          p += 6;
        } else if (u >= 0xD800 && u <= 0xDFFF) {
          u = 0xFFFD;
        }
        break;
      default: u = c; break;
    }
    if (ygnet__put_utf8(o, cap, &n, u)) return -2;
  }
  return -1;
}

/* Unescapes the string at p where it lies (the line is the client's); "" when absent. */
static const char *ygnet__here(const char *p, const char *e, size_t *len) {
  long n = p && p < e && *p == '"' ? ygnet__unescape(p, e, (char *)p + 1, (size_t)(e - p - 1)) : -1;
  if (len) *len = n < 0 ? 0 : (size_t)n;
  return n < 0 ? "" : p + 1;
}

/* s into out, cut at a character boundary when it does not fit. */
static void ygnet__copy(char *out, size_t cap, const char *s) {
  size_t n = strlen(s);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  }
  memcpy(out, s, n);
  out[n] = 0;
}

/* ---- JSON out */

static int ygnet__utf8(const char *str, size_t n) {
  const unsigned char *s = (const unsigned char *)str;
  size_t i = 0, j, k;
  while (i < n) {
    unsigned long c = s[i], min;
    if (c < 0x80) {
      i++;
      continue;
    }
    if (c >= 0xC2 && c <= 0xDF) {
      k = 1;
      c &= 0x1F;
      min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      k = 2;
      c &= 0x0F;
      min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      k = 3;
      c &= 0x07;
      min = 0x10000;
    } else {
      return 0;
    }
    if (n - i <= k) return 0;
    for (j = 1; j <= k; j++) {
      if ((s[i + j] & 0xC0) != 0x80) return 0;
      c = (c << 6) | (s[i + j] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    i += k + 1;
  }
  return 1;
}

/* A pass or a world name the broker takes: UTF-8, no control characters, at most `max` UTF-16 units. */
static int ygnet__text_ok(const char *s, size_t max) {
  size_t n = strlen(s), i, units = 0;
  if (n > 4 * max) return YGNET_EINVAL;
  if (!ygnet__utf8(s, n)) return YGNET_EUTF8;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7F) return YGNET_EINVAL;
    if ((c & 0xC0) != 0x80) units += (size_t)(c >= 0xF0 ? 2 : 1);
  }
  return units <= max ? 0 : YGNET_EINVAL;
}

static void ygnet__q(char *out, size_t cap, size_t *n, char ch) {
  if (*n < cap) out[*n] = ch;
  *n += 1;
}

size_t ygnet_json_quote(char *out, size_t cap, const char *s, size_t len) {
  static const char hex[] = "0123456789abcdef";
  size_t n = 0, i;
  ygnet__q(out, cap, &n, '"');
  for (i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') {
      ygnet__q(out, cap, &n, '\\');
      ygnet__q(out, cap, &n, (char)c);
    } else if (c >= 0x20) {
      ygnet__q(out, cap, &n, (char)c);
    } else if (c == '\n' || c == '\r' || c == '\t' || c == '\b' || c == '\f') {
      ygnet__q(out, cap, &n, '\\');
      ygnet__q(out, cap, &n, c == '\n' ? 'n' : c == '\r' ? 'r' : c == '\t' ? 't' : c == '\b' ? 'b' : 'f');
    } else {
      ygnet__q(out, cap, &n, '\\');
      ygnet__q(out, cap, &n, 'u');
      ygnet__q(out, cap, &n, '0');
      ygnet__q(out, cap, &n, '0');
      ygnet__q(out, cap, &n, hex[c >> 4]);
      ygnet__q(out, cap, &n, hex[c & 15]);
    }
  }
  ygnet__q(out, cap, &n, '"');
  if (n < cap) out[n] = 0;
  return n;
}

long ygnet_json_str(const char *json, size_t len, const char *key, char *out, size_t cap) {
  const char *v;
  if (!json || !key || !out) return -1;
  if (!ygnet__obj(json, json + len, &key, &v, 1, 1) || !v || *v != '"') return -1;
  return ygnet__unescape(v, json + len, out, cap);
}

static int ygnet__grow(char **buf, size_t *cap, size_t need, size_t limit) {
  size_t c = *cap ? *cap : 4096;
  char *p;
  if (need <= *cap) return 0;
  while (c < need) c = c > limit / 2 ? limit : c * 2;
  if (c < need) c = need;
  p = (char *)realloc(*buf, c);
  if (!p) return -1;
  *buf = p;
  *cap = c;
  return 0;
}

static void ygnet__raw(ygnet__buf *b, const char *s, size_t len) {
  if (b->err || ygnet__grow(&b->net->out, &b->net->out_cap, b->n + len, (size_t)-1)) {
    b->err = 1;
    return;
  }
  memcpy(b->net->out + b->n, s, len);
  b->n += len;
}

static void ygnet__num(ygnet__buf *b, long long v) {
  char t[24];
  int i = (int)sizeof t;
  unsigned long long u = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
  do {
    t[--i] = (char)('0' + (int)(u % 10));
    u /= 10;
  } while (u != 0);
  if (v < 0) t[--i] = '-';
  ygnet__raw(b, t + i, sizeof t - (size_t)i);
}

static void ygnet__str(ygnet__buf *b, const char *s, size_t len) {
  if (b->err || ygnet__grow(&b->net->out, &b->net->out_cap, b->n + 6 * len + 2, (size_t)-1)) {
    b->err = 1;
    return;
  }
  b->n += ygnet_json_quote(b->net->out + b->n, b->net->out_cap - b->n, s, len);
}

static void ygnet__field(ygnet__buf *b, const char *key, const char *s) {
  ygnet__raw(b, ",\"", 2);
  ygnet__raw(b, key, strlen(key));
  ygnet__raw(b, "\":", 2);
  ygnet__str(b, s, strlen(s));
}

static void ygnet__begin(ygnet__buf *b, ygnet *net, long long id, const char *type) {
  b->net = net;
  b->n = 0;
  b->err = 0;
  ygnet__raw(b, "yg {\"id\":", 9);
  ygnet__num(b, id);
  ygnet__raw(b, ",\"type\":\"", 9);
  ygnet__raw(b, type, strlen(type));
  ygnet__raw(b, "\"", 1);
}

static int ygnet__end(ygnet__buf *b) {
  ygnet__raw(b, "}\n", 2);
  if (b->err) return YGNET_ENOMEM;
  return b->net->cfg.write(b->net->cfg.user, b->net->out, b->n) ? YGNET_EWRITE : 0;
}

/* ---- the client */

static void ygnet_init(ygnet *net, const ygnet_config *cfg) {
  static const char marker[] = "YGNET/1";
  const volatile char *keep = marker;
  size_t i;
  /* Volatile reads keep the complete marker even in optimized, dead-stripped builds. */
  for (i = 0; i < sizeof marker; i++) (void)keep[i];
  if (cfg) net->cfg = *cfg;
  if (!net->cfg.write) net->cfg.write = ygnet__stdout;
  if (!net->cfg.max_line) net->cfg.max_line = YGNET_MAX_LINE;
  if (net->cfg.max_line > YGNET__LINE_CAP) net->cfg.max_line = YGNET__LINE_CAP;
  if (net->cfg.max_waiting <= 0) net->cfg.max_waiting = 24;
  if (net->cfg.max_waiting <= YGNET__CLOSES) net->cfg.max_waiting = YGNET__CLOSES + 1;
  if (net->cfg.max_waiting > YGNET__WAIT) net->cfg.max_waiting = YGNET__WAIT;
  /* Protocol v1's numbers, until hello says otherwise. */
  net->lim.version = 1;
  net->lim.max_connections = 4;
  net->lim.opens_per_second = 1;
  net->lim.open_burst = 8;
  net->lim.max_message_bytes = 16384;
  net->lim.max_world_message_bytes = 32768;
  net->lim.max_server_inbound_bytes = 65536;
  net->lim.max_relay_inbound_bytes = 1048576;
  net->lim.messages_per_second = 120;
  net->lim.bytes_per_second = 262144;
}

ygnet *ygnet_create(const ygnet_config *cfg) {
  ygnet *net = (ygnet *)calloc(1, sizeof *net);
  if (!net) return NULL;
  ygnet_init(net, cfg);
  return net;
}

void ygnet_destroy(ygnet *net) {
  if (!net) return;
  free(net->in);
  free(net->out);
  free(net);
}

long long ygnet_next_id(ygnet *net) {
  if (net->cfg.next_id) return net->cfg.next_id(net->cfg.user);
  net->counter++;
  return net->counter;
}

void ygnet_watch_hello(ygnet *net, long long id) {
  net->hello_id = id;
  net->hello_watch = 1;
}

long long ygnet_hello(ygnet *net) {
  ygnet__buf b;
  long long id;
  int rc;
  if (!net) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  id = ygnet_next_id(net);
  ygnet__begin(&b, net, id, "hello");
  rc = ygnet__end(&b);
  if (rc) return rc;
  ygnet_watch_hello(net, id);
  return id;
}

int ygnet_status(const ygnet *net) {
  return net ? net->status : YGNET_EINVAL;
}

const ygnet_limits *ygnet_broker(const ygnet *net) {
  return net && net->status == 1 ? &net->lim : NULL;
}

static ygnet__conn *ygnet__by_handle(ygnet *net, int h) {
  int i;
  for (i = 0; h > 0 && i < YGNET__SLOTS; i++)
    if (net->conn[i].handle == h) return &net->conn[i];
  return NULL;
}

static ygnet__conn *ygnet__by_app(ygnet *net, long long app) {
  int i;
  for (i = 0; app > 0 && i < YGNET__SLOTS; i++)
    if (net->conn[i].handle && net->conn[i].app == app) return &net->conn[i];
  return NULL;
}

/* Room for one more request of this kind: closes may use the whole of max_waiting. */
static int ygnet__room(const ygnet *net, int kind) {
  return net->nwait < (kind == YGNET__CLOSE ? net->cfg.max_waiting : net->cfg.max_waiting - YGNET__CLOSES);
}

static int ygnet__opens_waiting(const ygnet *net) {
  int i;
  for (i = 0; i < net->nwait; i++)
    if (net->wait[i].kind == YGNET__OPEN) return 1;
  return 0;
}

static void ygnet__waiting(ygnet *net, long long id, int kind, int conn) {
  net->wait[net->nwait].id = id;
  net->wait[net->nwait].kind = kind;
  net->wait[net->nwait].conn = conn;
  net->nwait++;
}

static void ygnet__error(ygnet *net, int h, const char *code, const char *message) {
  if (net->cfg.on_error) net->cfg.on_error(net->cfg.user, h, code, message);
}

/* Frees the slot first, so on_close may open the next connection. */
static void ygnet__closed(ygnet *net, ygnet__conn *c, int code, const char *reason) {
  int h = c->handle;
  memset(c, 0, sizeof *c);
  if (net->cfg.on_close) net->cfg.on_close(net->cfg.user, h, code, reason);
}

static void ygnet__up(ygnet *net, ygnet__conn *c) {
  if (c->up) return;
  c->up = 1;
  if (c->state != YGNET__OPENING) return;
  c->state = YGNET__UP;
  c->told = 1;
  if (net->cfg.on_open) net->cfg.on_open(net->cfg.user, c->handle);
}

static int ygnet__send_close(ygnet *net, ygnet__conn *c) {
  ygnet__buf b;
  long long id;
  int rc;
  if (!ygnet__room(net, YGNET__CLOSE)) return YGNET_EBUSY;
  id = ygnet_next_id(net);
  ygnet__begin(&b, net, id, "netClose");
  ygnet__raw(&b, ",\"conn\":", 8);
  ygnet__num(&b, c->app);
  if (c->close_code != 1000) {
    ygnet__raw(&b, ",\"code\":", 8);
    ygnet__num(&b, c->close_code);
  }
  if (c->reason[0]) ygnet__field(&b, "reason", c->reason);
  rc = ygnet__end(&b);
  if (rc) return rc;
  c->close_id = id;
  ygnet__waiting(net, id, YGNET__CLOSE, c->handle);
  return 0;
}

/* Closes asked for before netOpen was answered, or that could not go out then. */
static void ygnet__retry_closes(ygnet *net) {
  int i;
  for (i = 0; i < YGNET__SLOTS; i++) {
    ygnet__conn *c = &net->conn[i];
    if (c->handle && c->want_close && c->app && !c->close_id && ygnet__send_close(net, c) == 0) c->want_close = 0;
  }
}

static void ygnet__limit(const char *v, const char *e, long *out) {
  long long x;
  if (v && ygnet__int(v, e, &x) && x > 0 && x <= 0x7fffffffL) *out = (long)x;
}

static void ygnet__hello_result(ygnet *net, const char *p, const char *e) {
  static const char *in[] = {"netBroker"};
  static const char *head[] = {"version", "mode", "limits"};
  static const char *keys[] = {"maxConnections", "opensPerSecond", "openBurst", "maxMessageBytes", "maxWorldMessageBytes",
                               "maxServerInboundBytes", "maxRelayInboundBytes", "messagesPerSecond", "bytesPerSecond"};
  const char *nb, *h[3], *v[9];
  long version = 0;
  net->status = YGNET_ENOBROKER;
  if (!ygnet__obj(p, e, in, &nb, 1, 1) || !nb) return;
  if (!ygnet__obj(nb, e, head, h, 3, 3)) return;
  ygnet__limit(h[0], e, &version);
  if (version != 1) {
    net->status = YGNET_EVERSION;
    return;
  }
  net->lim.internet = ygnet__is(h[1], e, "internet");
  if (ygnet__obj(h[2], e, keys, v, 9, 9)) {
    ygnet__limit(v[0], e, &net->lim.max_connections);
    ygnet__limit(v[1], e, &net->lim.opens_per_second);
    ygnet__limit(v[2], e, &net->lim.open_burst);
    ygnet__limit(v[3], e, &net->lim.max_message_bytes);
    ygnet__limit(v[4], e, &net->lim.max_world_message_bytes);
    ygnet__limit(v[5], e, &net->lim.max_server_inbound_bytes);
    ygnet__limit(v[6], e, &net->lim.max_relay_inbound_bytes);
    ygnet__limit(v[7], e, &net->lim.messages_per_second);
    ygnet__limit(v[8], e, &net->lim.bytes_per_second);
  }
  net->status = 1;
}

int ygnet_hello_result(ygnet *net, const char *json, size_t len) {
  int previous, i;
  if (!net || !json) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  previous = net->status;
  ygnet__hello_result(net, json, json + len);
  /* A later on_player hello may omit netBroker; established connections still belong to us. */
  if (previous == 1 && net->status == YGNET_ENOBROKER)
    for (i = 0; i < YGNET__SLOTS; i++)
      if (net->conn[i].handle) { net->status = previous; break; }
  return net->status;
}

static void ygnet__hello_answer(ygnet *net, const char *p, const char *e) {
  static const char *top[] = {"ok", "result"};
  const char *t[2];
  net->status = YGNET_ENOBROKER;
  if (ygnet__obj(p, e, top, t, 2, 2) && ygnet__true(t[0], e) && t[1])
    ygnet__hello_result(net, t[1], e);
}

static ygnet__early *ygnet__early_for(ygnet *net, long long app) {
  int i;
  for (i = 0; i < YGNET__SLOTS; i++)
    if (net->early[i].app == app) return &net->early[i];
  return NULL;
}

/* A push for a connection whose netOpen is not answered yet (the app's answers and pushes may
 * come in either order): kept until the answer names it. 1 open, 2 error, 3 close. */
static void ygnet__early_note(ygnet *net, long long app, int what, const char *a, const char *b, int code) {
  ygnet__early *x = ygnet__early_for(net, app);
  int i;
  if (!x) {
    if (!ygnet__opens_waiting(net)) return;
    for (i = 0; i < YGNET__SLOTS && net->early[i].app; i++) {}
    if (i == YGNET__SLOTS) return;
    x = &net->early[i];
    memset(x, 0, sizeof *x);
    x->app = app;
  }
  if (what == 1) {
    x->opened = 1;
  } else if (what == 2 && !x->failed) {
    x->failed = 1;
    ygnet__copy(x->code, sizeof x->code, a);
    ygnet__copy(x->message, sizeof x->message, b);
  } else if (what == 3) {
    x->closed = 1;
    x->close_code = code;
    ygnet__copy(x->reason, sizeof x->reason, a);
  }
}

static void ygnet__opened(ygnet *net, ygnet__conn *c, int ok, const char *result, const char *e, const char *code, const char *error) {
  static const char *in[] = {"conn"};
  const char *conn;
  long long app = 0;
  int h = c->handle;
  ygnet__early *x, early;
  if (!ok || !ygnet__obj(result, e, in, &conn, 1, 1) || !ygnet__int(conn, e, &app) || app <= 0 || ygnet__by_app(net, app)) {
    ygnet__error(net, h, !ok && *code ? code : "denied", ok ? "The app's netOpen answer had no connection id." : error);
    c = ygnet__by_handle(net, h);
    if (c) ygnet__closed(net, c, 1006, "");
    return;
  }
  c->app = app;
  x = ygnet__early_for(net, app);
  if (x) {
    early = *x;
    memset(x, 0, sizeof *x);
    if (early.opened) ygnet__up(net, c);
    if (early.failed) ygnet__error(net, h, early.code, early.message);
    c = ygnet__by_handle(net, h);
    if (c && early.closed) ygnet__closed(net, c, early.close_code, early.reason);
  }
}

/* A netClose the app refused: the connection is as it was, and ygnet_close may try again. */
static void ygnet__close_refused(ygnet *net, ygnet__conn *c, const char *code, const char *error) {
  int h = c->handle;
  if (c->state != YGNET__CLOSING) return;
  c->state = c->up ? YGNET__UP : YGNET__OPENING;
  ygnet__error(net, h, *code ? code : "failed", error);
  c = ygnet__by_handle(net, h);
  if (c && c->state == YGNET__UP && !c->told) {
    c->told = 1;
    if (net->cfg.on_open) net->cfg.on_open(net->cfg.user, h);
  }
}

static int ygnet__answer(ygnet *net, long long id, char *p, char *e) {
  static const char *keys[] = {"ok", "result", "error", "code"};
  const char *v[4], *code, *error;
  ygnet__wait w;
  ygnet__conn *c;
  int i, ok;
  if (net->hello_watch && id == net->hello_id) {
    net->hello_watch = 0;
    ygnet__hello_answer(net, p, e);
    return 0; /* hello is the game's too */
  }
  for (i = 0; i < net->nwait && net->wait[i].id != id; i++) {}
  if (i == net->nwait) return 0;
  w = net->wait[i];
  net->nwait--;
  net->wait[i] = net->wait[net->nwait];
  ygnet__obj(p, e, keys, v, 4, 4);
  ok = ygnet__true(v[0], e);
  /* Both found before either is unescaped where it lies. */
  code = ygnet__here(v[3], e, NULL);
  error = ygnet__here(v[2], e, NULL);
  c = ygnet__by_handle(net, w.conn);
  if (c && w.kind == YGNET__OPEN) {
    ygnet__opened(net, c, ok, v[1], e, code, error);
  } else if (c && w.kind == YGNET__SEND && !ok) {
    /* A send's refusal is reported from its answer, never from the error push beside it. */
    ygnet__error(net, c->handle, *code ? code : "failed", error);
  } else if (c && w.kind == YGNET__CLOSE && c->close_id == id) {
    c->close_id = 0;
    if (!ok) ygnet__close_refused(net, c, code, error);
  }
  ygnet__retry_closes(net);
  if (!ygnet__opens_waiting(net)) memset(net->early, 0, sizeof net->early);
  return 1;
}

static int ygnet__push(ygnet *net, const char *p, char *e) {
  static const char *first[] = {"type"};
  static const char *keys[] = {"conn", "event", "data", "code", "message", "reason"};
  static const char *sends[] = {"too_large", "rate_messages", "rate_bytes", "backed_up", "not_open"};
  const char *type, *v[6];
  ygnet__conn *c;
  long long app = 0, code = 1006;
  int i;
  if (!ygnet__obj(p, e, first, &type, 1, 1) || !ygnet__is(type, e, "net")) return 0;
  if (!ygnet__obj(p, e, keys, v, 6, 6) || !ygnet__int(v[0], e, &app) || app <= 0) return 1;
  c = ygnet__by_app(net, app);
  if (ygnet__is(v[1], e, "open")) {
    if (c) ygnet__up(net, c);
    else ygnet__early_note(net, app, 1, NULL, NULL, 0);
  } else if (ygnet__is(v[1], e, "message")) {
    size_t len;
    const char *data;
    if (!c || c->state != YGNET__UP) return 1;
    data = ygnet__here(v[2], e, &len);
    if (net->cfg.on_message) net->cfg.on_message(net->cfg.user, c->handle, data, len);
  } else if (ygnet__is(v[1], e, "error")) {
    const char *what, *message;
    for (i = 0; i < 5; i++)
      if (ygnet__is(v[3], e, sends[i])) return 1; /* its netSend's answer says it */
    what = ygnet__here(v[3], e, NULL);
    message = ygnet__here(v[4], e, NULL);
    if (c) ygnet__error(net, c->handle, what, message);
    else ygnet__early_note(net, app, 2, what, message, 0);
  } else if (ygnet__is(v[1], e, "close")) {
    const char *reason;
    if (!ygnet__int(v[3], e, &code) || code < 0 || code > 65535) code = 1006;
    reason = ygnet__here(v[5], e, NULL);
    if (c) ygnet__closed(net, c, (int)code, reason);
    else ygnet__early_note(net, app, 3, reason, NULL, (int)code);
  }
  return 1;
}

int ygnet_dispatch(ygnet *net, char *line, size_t len) {
  static const char *keys[] = {"re", "push"};
  const char *v[2];
  char *e = line + len;
  long long id;
  if (!net || !line || net->ended || len < 5 || memcmp(line, "yg ", 3) != 0) return 0;
  if (!ygnet__obj(line + 3, e, keys, v, 2, 1)) return 0;
  if (v[0]) return ygnet__int(v[0], e, &id) ? ygnet__answer(net, id, line + 3, e) : 0;
  return v[1] ? ygnet__push(net, v[1], e) : 0;
}

static void ygnet__drop(ygnet *net, const char *why) {
  net->dropping = 1;
  net->in_len = 0;
  ygnet__error(net, 0, "line_dropped", why);
}

int ygnet_feed(ygnet *net, const void *bytes, size_t n) {
  const char *b = (const char *)bytes;
  int rc = 0;
  if (!net || (!b && n)) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  while (n) {
    const char *nl = (const char *)memchr(b, '\n', n);
    size_t take = nl ? (size_t)(nl - b) : n, len;
    if (!net->dropping) {
      if (take > net->cfg.max_line - net->in_len) {
        ygnet__drop(net, "A line from the YouGame app was over the client's line limit.");
      } else if (ygnet__grow(&net->in, &net->in_cap, net->in_len + take + 1, net->cfg.max_line + 1)) {
        ygnet__drop(net, "Out of memory for a line from the YouGame app.");
        rc = YGNET_ENOMEM;
      } else {
        memcpy(net->in + net->in_len, b, take);
        net->in_len += take;
      }
    }
    if (!nl) break;
    b = nl + 1;
    n -= take + 1;
    if (net->dropping) {
      net->dropping = 0;
      continue;
    }
    len = net->in_len;
    net->in_len = 0;
    if (len && net->in[len - 1] == '\r') len--;
    net->in[len] = 0;
    if (!ygnet_dispatch(net, net->in, len) && net->cfg.on_line) net->cfg.on_line(net->cfg.user, net->in, len);
    if (net->ended) return YGNET_EENDED;
  }
  /* A big line (a save file) is not kept around. */
  if (!net->in_len && net->in_cap > ((size_t)4 << 20)) {
    free(net->in);
    net->in = NULL;
    net->in_cap = 0;
  }
  return rc;
}

void ygnet_eof(ygnet *net) {
  int i;
  if (!net || net->ended) return;
  net->ended = 1;
  net->nwait = 0;
  net->hello_watch = 0;
  net->in_len = 0;
  net->dropping = 0;
  memset(net->early, 0, sizeof net->early);
  for (i = 0; i < YGNET__SLOTS; i++) {
    if (!net->conn[i].handle) continue;
    ygnet__error(net, net->conn[i].handle, "ended", "The YouGame app closed the bridge.");
    if (net->conn[i].handle) ygnet__closed(net, &net->conn[i], 1006, "");
  }
}

/* The ticket in what the game passed: an mpTicket answer line, its result, or the ticket itself. */
static long ygnet__ticket(const char *s, char *out, size_t cap) {
  static const char *keys[] = {"ticket", "result", "server"};
  const char *e = s + strlen(s), *v[3], *r[3];
  long n = 0;
  s = ygnet__ws(s, e);
  if (e - s >= 3 && memcmp(s, "yg ", 3) == 0) s = ygnet__ws(s + 3, e);
  if (s < e && *s == '{') {
    if (!ygnet__obj(s, e, keys, v, 3, 3)) return YGNET_ETICKET;
    if (!v[0] && v[1] && ygnet__obj(v[1], e, keys, r, 3, 3)) {
      v[0] = r[0];
      v[2] = r[2];
    }
    if (ygnet__is(v[2], e, "engine")) return YGNET_EUNSUPPORTED;
    n = ygnet__unescape(v[0], e, out, cap);
    return n < 0 ? YGNET_ETICKET : n;
  }
  while (s < e && (unsigned char)*s > ' ') {
    if ((size_t)n + 1 >= cap) return YGNET_ETICKET;
    out[n++] = *s++;
  }
  out[n] = 0;
  return ygnet__ws(s, e) == e ? n : YGNET_ETICKET;
}

static long ygnet__b64url(const char *s, size_t n, unsigned char *out, size_t cap) {
  unsigned long acc = 0;
  size_t i, o = 0;
  int bits = 0;
  for (i = 0; i < n && s[i] != '='; i++) {
    char c = s[i];
    int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '-' ? 62 : c == '_' ? 63 : -1;
    if (v < 0) return -1;
    acc = ((acc << 6) | (unsigned long)v) & 0xFFFFFFUL;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= cap) return -1;
      out[o++] = (unsigned char)(acc >> bits);
    }
  }
  return (long)o;
}

static int ygnet__room_code(const char *s) {
  size_t n = strlen(s), i;
  if (n < 6 || n > 12) return 0;
  for (i = 0; i < n; i++)
    if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9'))) return 0;
  return 1;
}

int ygnet_open(ygnet *net, const char *ticket, const ygnet_open_opts *opts) {
  static const char *keys[] = {"w", "sv", "rw", "pd"};
  static const char *engine[] = {"e"};
  char t[YGNET__TICKET + 1];
  unsigned char pay[YGNET__TICKET];
  const char *v[4], *ev = NULL, *end, *dot = NULL;
  const char *room = opts ? opts->room : NULL, *pass = opts ? opts->pass : NULL, *name = opts ? opts->name : NULL;
  long tl, pl, i, max;
  int slot = -1, live = 0, rc;
  long long id;
  ygnet__buf b;
  ygnet__conn *c;
  if (!net || !ticket) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  if (net->status < 0) return net->status;
  tl = ygnet__ticket(ticket, t, sizeof t);
  if (tl < 0) return (int)tl;
  for (i = 0; i < tl; i++) {
    char ch = t[i];
    if (ch == '.' && !dot) dot = t + i;
    else if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return YGNET_ETICKET;
  }
  if (!dot || dot == t || dot == t + tl - 1) return YGNET_ETICKET;
  pl = ygnet__b64url(t, (size_t)(dot - t), pay, sizeof pay);
  if (pl <= 0) return YGNET_ETICKET;
  end = (const char *)pay + pl;
  if (!ygnet__obj((const char *)pay, end, keys, v, 4, 4)) return YGNET_ETICKET;
  if (v[1] && (*v[1] != '{' || !ygnet__obj(v[1], end, engine, &ev, 1, 1))) return YGNET_ETICKET;
  /* Engine servers (their containers have the internet), watch and pad tickets: never brokered. */
  if (v[2] || v[3] || ev) return YGNET_EUNSUPPORTED;
  if (room && !ygnet__room_code(room)) return YGNET_EINVAL;
  if (pass) {
    rc = ygnet__text_ok(pass, 24);
    if (rc) return rc;
  }
  if (name) {
    rc = ygnet__text_ok(name, 40);
    if (rc) return rc;
  }
  for (i = 0; i < YGNET__SLOTS; i++) {
    if (net->conn[i].handle) live++;
    else if (slot < 0) slot = (int)i;
  }
  max = net->lim.max_connections < YGNET__SLOTS ? net->lim.max_connections : YGNET__SLOTS;
  if (live >= max || slot < 0) return YGNET_ELIMIT;
  if (!ygnet__room(net, YGNET__OPEN)) return YGNET_EBUSY;
  id = ygnet_next_id(net);
  ygnet__begin(&b, net, id, "netOpen");
  ygnet__field(&b, "ticket", t);
  if (room) ygnet__field(&b, "room", room);
  if (pass) ygnet__field(&b, "pass", pass);
  if (name) ygnet__field(&b, "name", name);
  rc = ygnet__end(&b);
  if (rc) return rc;
  c = &net->conn[slot];
  memset(c, 0, sizeof *c);
  net->handles = net->handles == 0x7fffffff ? 1 : net->handles + 1;
  c->handle = net->handles;
  c->state = YGNET__OPENING;
  c->relay_world = v[0] && *v[0] == '"' && !v[1];
  ygnet__waiting(net, id, YGNET__OPEN, c->handle);
  return c->handle;
}

int ygnet_send_n(ygnet *net, int conn, const char *data, size_t len) {
  ygnet__conn *c;
  ygnet__buf b;
  long long id;
  int rc;
  if (!net || (!data && len)) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  c = ygnet__by_handle(net, conn);
  if (!c) return YGNET_ENOCONN;
  if (c->state != YGNET__UP) return YGNET_ENOTOPEN;
  if (len > (size_t)(c->relay_world ? net->lim.max_world_message_bytes : net->lim.max_message_bytes)) return YGNET_ETOOBIG;
  if (!data) data = "";
  if (!ygnet__utf8(data, len)) return YGNET_EUTF8;
  if (!ygnet__room(net, YGNET__SEND)) return YGNET_EBUSY;
  id = ygnet_next_id(net);
  ygnet__begin(&b, net, id, "netSend");
  ygnet__raw(&b, ",\"conn\":", 8);
  ygnet__num(&b, c->app);
  ygnet__raw(&b, ",\"data\":", 8);
  ygnet__str(&b, data, len);
  rc = ygnet__end(&b);
  if (rc) return rc;
  ygnet__waiting(net, id, YGNET__SEND, conn);
  return 0;
}

int ygnet_send(ygnet *net, int conn, const char *text) {
  return text ? ygnet_send_n(net, conn, text, strlen(text)) : YGNET_EINVAL;
}

int ygnet_close(ygnet *net, int conn, int code, const char *reason) {
  ygnet__conn *c;
  size_t rl;
  int rc;
  if (!net) return YGNET_EINVAL;
  if (net->ended) return YGNET_EENDED;
  c = ygnet__by_handle(net, conn);
  if (!c) return YGNET_ENOCONN;
  if (!code) code = 1000;
  if (!reason) reason = "";
  rl = strlen(reason);
  if ((code != 1000 && (code < 3000 || code > 4999)) || rl > 123) return YGNET_EINVAL;
  if (!ygnet__utf8(reason, rl)) return YGNET_EUTF8;
  if (c->state == YGNET__CLOSING) return 0;
  c->close_code = code;
  memcpy(c->reason, reason, rl + 1);
  if (!c->app) {
    /* netOpen is not answered yet: the close goes once it is. */
    c->want_close = 1;
  } else {
    rc = ygnet__send_close(net, c);
    if (rc) return rc;
  }
  c->state = YGNET__CLOSING;
  return 0;
}

const char *ygnet_strerror(int err) {
  switch (err) {
    case 0: return "ok";
    case YGNET_EINVAL: return "bad argument";
    case YGNET_ENOMEM: return "out of memory";
    case YGNET_EWRITE: return "couldn't write to the YouGame app";
    case YGNET_ENOBROKER: return "this run can't go online through YouGame (hello has no netBroker)";
    case YGNET_ETICKET: return "not a ticket from an mpTicket answer";
    case YGNET_EUNSUPPORTED: return "engine servers, watch and pad tickets don't go through YouGame's network";
    case YGNET_ELIMIT: return "too many connections open at once";
    case YGNET_EBUSY: return "too many requests waiting for the app: read stdin, then try again";
    case YGNET_ENOCONN: return "no such connection (it closed)";
    case YGNET_ENOTOPEN: return "the connection is not open (not yet, or closing)";
    case YGNET_ETOOBIG: return "message over the connection's size limit";
    case YGNET_EUTF8: return "not UTF-8: connections carry text";
    case YGNET_EENDED: return "the YouGame app is gone (stdin ended)";
    case YGNET_EVERSION: return "the app's network broker speaks a protocol version this client doesn't";
    default: return "unknown error";
  }
}

#ifdef __cplusplus
}
#endif
#endif /* YGNET_IMPLEMENTED */
#endif /* YGNET_IMPLEMENTATION */
