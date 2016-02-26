/*
 * proxy.c - Proxy with buffered event
 *  - 20130185 Tae-kyeom, kim
 */

#define __USE_XOPEN
#define _GNU_SOURCE

/* Sometimes we want to clear all previously logged data 
 * So if we define CLEAR, clear all logged text */
#define CLEAR


#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "cache.h"

#define LOG "proxy.log"
/*
 * 'struct bufferevent' is too long, so 
 * breifely denote as bufev
 */
typedef struct bufferevent bufev;

/* 
 * When cache hit or Not modified response,
 * we passes cache_hit_t argument, which consists
 * response header and found cache entry
 */

typedef struct Cachehit_arg_t {
  char *header;  /*response header */
  cache_entry *cached_data;  /* found cached data */
  struct sockaddr_in caddr;
} cache_hit_t;

/* 
 * When cache missed we use cache_buf_t argument
 * which consists buffer and path name, size, and for 
 * some variables used to save cache entry 
 */
typedef struct Cachemiss_arg_t {
  char buf[MAX_OBJ_SIZE];  /* Buffer for saving responsed data */
  char path[MAXLINE];   /* Path name for indexing cache entries */
  size_t size;  /* size of responsed data */
  /* Expires time for cache(if there is no expires tag, 
   * default period is 1 hour*/
  time_t expires;  
  /* Some additional meta information such as content encoding, type... */
  char Meta[3][MAXLINE]; 
  struct sockaddr_in caddr;  /* address of client */
  bufev *partner;  /* Partner buffer event(client-proxy connection) */
  /* If response has pragma: no-cache, set this falg to indicate no cache */
  char NoCache_Flag; 
  /* If we find cache entry but expired, temporarily memorize it 
   * because we check it is not modified we just use it*/
  cache_entry *target;
} cache_buf_t;

cache_entry *c_header;



/* Call back function prototypes */
void proxy_server_headercb(bufev *bev, void *ctx);
void proxy_server_bodycb(bufev *bev, void *ctx);
void cached_data_writecb(bufev *bev, void *ctx);
void client_proxy_readcb(bufev *bev, void *ctx);
void close_on_finished_writecb(bufev *bev, void *ctx);
void event_cb(bufev *bev, short what, void *ctx); 
/* End callback prototypes */

/* Logging functions */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
    char *url, int size)
{
  time_t now;
  char time_str[MAXLINE];
  unsigned long host;
  unsigned char a, b, c, d;

  now = time(NULL);
  strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

  host = ntohl(sockaddr->sin_addr.s_addr);
  a = host >> 24;
  b = (host >> 16) & 0xff;
  c = (host>> 8) & 0xff;
  d = host & 0xff;

  /* Save log string */
  sprintf(logstring, "%s %d.%d.%d.%d %s %d",
      time_str, a, b, c, d, url, size);
}

void Save_log_miss(cache_buf_t *ctx)
{
  char log_string[MAXLINE];
  FILE *log = Fopen(LOG, "a");
  format_log_entry(log_string, &ctx->caddr, ctx->path, ctx->size);
  fprintf(log, "%s Miss\n", log_string);
  fflush(log);
  Fclose(log);
}

void Save_log_hit(cache_hit_t *ctx)
{
  char log_string[MAXLINE];
  FILE *log = Fopen(LOG, "a");
  format_log_entry(log_string, &ctx->caddr,
      ctx->cached_data->path, ctx->cached_data->content_len);
  fprintf(log, "%s Hit\n", log_string);
  fflush(log);
  Fclose(log);
}

void Save_log_No_cache(cache_buf_t *ctx)
{
  char log_string[MAXLINE];
  FILE *log = Fopen(LOG, "a");
  format_log_entry(log_string, &ctx->caddr, ctx->path, ctx->size);
  fprintf(log, "%s No-cache\n", log_string);
  fflush(log);
  Fclose(log);
}



/* Call back function implementations */

/*
 * Write cached data to client 
 * If successfully added to client's output buffer,
 * set close_on_finished_writecb to wait for flushing 
 * and free buffered event
 * passed argument is cache_hit_t
 */
void cached_data_writecb(bufev *bev, void *ctx)
{
  struct evbuffer *client = bufferevent_get_output(bev);
  cache_hit_t *cache = ctx;
  /* Check member is valid (i.e: not NULL) and add it to client's
   * output buffer */
  if (cache->header) 
    evbuffer_add(client, cache->header, strlen(cache->header));
  if (cache->cached_data)
    evbuffer_add(client, cache->cached_data->content,
        cache->cached_data->content_len);

  /* Set write callback to close_on_finished_writecb */
  bufferevent_setcb(bev, NULL, close_on_finished_writecb, NULL, ctx);
}


/* 
 * Read client's request header and find cache
 * if find cached object and within expire time, immediately send it
 * setting call back to cached_data_writecb.
 * otherwise, if target is found later than expire time, 
 * send If modified since field 
 * else just send it changing some header.
 * - HTTP/1.1 -> HTTP/1.0
 * - Connection: open -> Connection: close 
 * And pass argument as cache_buf_t to save data to be cached.
 */
void client_proxy_readcb(bufev *bev, void *ctx)
{
  /* For parsing HTTP request header */
  char method[10];
  char uri[MAXLINE];
  char http_v[20];

  /* General purpose header */
  char buf[MAXLINE];

  /* Parsed data(host name, path name, and port number) */
  int d_port;
  char d_host[MAXLINE];
  char d_path[MAXLINE];

  /* For making argument for cache hit or miss cases */
  cache_buf_t *arg;
  cache_hit_t *hit_arg;
 
  /* For read line by line from client's input buffer */
  char *line;
  size_t len;

  /* time_t for conditional get */
  time_t curr_t;

  /* For finding cached data */
  cache_entry *target;

  /* Passed argument is client address(use for logging) */
  struct sockaddr_in *caddr = (struct sockaddr_in *)ctx;


  /* For make proxy-endserver buffer event */
  struct event_base *base = bufferevent_get_base(bev);
  evutil_socket_t endserverfd;
  bufev *partner;
  struct evbuffer *dst;
 
  /* Get input buffer from client */
  struct evbuffer *input = bufferevent_get_input(bev);
  
  /* Read first line of client's request */
  line = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF_STRICT);

  /* Parsed into method, uri, and http version */
  sscanf(line, "%s %s %s", method, uri, http_v); 
 
  /* If method is not 'GET', return */
  if (strcasecmp(method, "GET")) {
    printf("Proxy: Invalid method\n");
    bufferevent_free(bev);
    return;
  }

  /* If unvalid http version, return */
  if ((strcmp(http_v, "HTTP/1.0")) && (strcmp(http_v, "HTTP/1.1"))) {
    printf("Proxy: Version mismatch\n");
    bufferevent_free(bev);
    return;
  }

  /* Using uri, find cached data */
  target = find_cache(c_header, uri);
  if (target) {
    curr_t = time(NULL);
    /* If cached data exists and before than expires time,
     * immediately write to client */
    if (target->expires > curr_t) {
      printf("Cache hit\n");
      /* Update time stamp */
      target->timestamp = ++c_header->timestamp;
      /* Make passing argument cache_hit_t */
      hit_arg = (cache_hit_t *)malloc(sizeof(cache_hit_t));
      hit_arg->header = make_response_header(target);
      hit_arg->cached_data = target;
      /* Set callback cached_data_writecb with cache_hit_t argument */
      bufferevent_setcb(bev, NULL, cached_data_writecb, NULL, hit_arg);
      cached_data_writecb(bev, hit_arg);
      /* terminate read function */
      return;
    }
  }


  /* When cache miss or expired*/
  
  /* Parse uri into path, host, and port */
  if (parse_uri(uri, d_host, d_path, &d_port) < 0) {
    printf("Proxy: Parse failed\n");
    bufferevent_free(bev);
    return;
  }

  /* Open endserver connection */
  if ((endserverfd = open_clientfd(d_host, d_port)) < 0) {
    perror("Connect to endserver");
    bufferevent_free(bev);
    return;
  }
  /* Make passing argument cache_buf_t */
  arg = malloc(sizeof(cache_buf_t));
  memset(arg->buf, 0, MAXLINE);
  /* Write path for save cache */
  sprintf(arg->path, "http://%s/%s", d_host, d_path);
  /* Coppy address for logging later */
  memcpy(&arg->caddr, caddr, sizeof(struct sockaddr_in));
  /* Size of received body content (do not count response header size) */
  arg->size = 0;
  arg->partner = bev;
  arg->NoCache_Flag = 0;
  /* Fof distinguishing cache miss and other cases(Not modified or updata)
   * set cache entry (if not found, set NULL) */
  arg->target = target;

  /* Set proxy - endserver buffer event */
  partner = bufferevent_socket_new(base,
      endserverfd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

  /* Set callback and enable read and write */
  bufferevent_setcb(partner, proxy_server_headercb, NULL, event_cb, arg);
  bufferevent_enable(partner, EV_READ|EV_WRITE);

  /* Get endserver's output buffer */
  dst = bufferevent_get_output(partner);
 
  /* Add modified request header */
  evbuffer_add_printf(dst, "GET /%s HTTP/1.0\r\n", d_path);
  free(line);
  /* If target is not null it means its expires tag is expired.
   * So send conditional get */
  if (target) {
    strftime(buf, MAXLINE, "If-Modified-Since: %a, %d %b %Y %H:%M:%S %Z", gmtime(&curr_t));
    evbuffer_add_printf(dst, "%s\r\n", buf);
  }
  /* Send remained reqeust messages */
  while((line = evbuffer_readln(input, &len, EVBUFFER_EOL_CRLF_STRICT))) {
    /* If connection is keep alive, change to non persistant option(close) */
    if (!strncmp(line, "Connection: keep-alive",
          strlen("Connection: keep-alive"))) {
      evbuffer_add_printf(dst, "%s\r\n", "Connection: close");
    } else {
      evbuffer_add_printf(dst, "%s\r\n", line);
    }
    free(line);
    /* zero length means it reached \r\n, so break this loop */
    if (len == 0) {
      break;
    }
  }
}

/* 
 * Read response header and send to client 
 */
void proxy_server_headercb(bufev *bev, void *ctx)
{
  char NotMod_Flag = 0;  /* If 304 Not modified response, set tihs flag */
  cache_buf_t *cache = ctx;  /* passed cache_buf_t type argument */
  bufev *partner = cache->partner;  /* get partner(client-size) bufferevent */
  struct evbuffer *src, *dst;  /* source and destintion buffer */
  /* For read line from src buffer */
  size_t len;
  char *line;

  /* Get source buffer, which is endserver's input buffer */
  src = bufferevent_get_input(bev);
  len = evbuffer_get_length(src);
  
  /* Local buffer */
  char buf[MAXLINE];
  /* Use for Not modified case */
  cache_hit_t *hit_arg;
  /* For calculating expires time */
  struct tm Expires;

  int i;

  /* If there is no clinet side pipe, remove all received data and return */
  if (!partner) {
    evbuffer_drain(src, len);
    return;
  }

  /* Else get client's output buffer as destination */
  dst = bufferevent_get_output(partner);
  memset(buf, 0, MAXLINE);
  while ((line = evbuffer_readln(src, &len, EVBUFFER_EOL_CRLF_STRICT))) {
    /* If response is 304 Not modified, changed it to 200 OK, 
     * and send additional cached meta information 
     * (such as content type, encoding, .. ) */
    if (strstr(line, "Not Modified")) {
      NotMod_Flag = 1;  /* Set Not modified flag */
      evbuffer_add_printf(dst, "HTTP/1.0 200 OK\r\n");
      /* Send some additional meta data */
      for (i = 0; i < 3; i++) {
        if (strlen(cache->target->Meta[i])) 
          evbuffer_add_printf(dst, "%s", cache->target->Meta[i]);
      }
    }
    /* Else send it directly to client */
    else 
      evbuffer_add_printf(dst, "%s\r\n", line);
    if (len == 0) {
      break;
    }
    /* Save some useful informations */
    if (!strncmp(line, "Last-Modified", strlen("Last-Modified")))
      sprintf(cache->Meta[0], "%s\r\n", line);
    else if (!strncmp(line, "Content-Type", strlen("Content-Type"))) 
      sprintf(cache->Meta[1], "%s\r\n", line);
    else if (!strncmp(line, "Content-Encoding", strlen("Content-Encoding")))
      sprintf(cache->Meta[2], "%s\r\n", line);
    else if (!strncmp(line, "Expires", strlen("Expires")))
      strcpy(buf, line + strlen("Expires: "));
    else if (strstr(line, "no-cache")) {
      cache->NoCache_Flag =1;
    }
    free(line);
  }
  
  
  /* Check expires tag */
  memset(&Expires, 0, sizeof(struct tm));
  if (strlen(buf)) {
    strptime(buf, "%a, %d %b %Y %H:%M:%S %Z", &Expires);
    /* strptime has some error in get timezone, manually optimized
     * (!important: this optimized value is quite machine specific,
     * so it may chage test in other environmet */
    cache->expires = mktime(&Expires) + 30600;
    /* For debugging */
  }
  /* If there is no expires tag and no nocache tag
   * default expire period is 1 hour */
  else if (!cache->NoCache_Flag)
    cache->expires = time(NULL) + 3600;

  /* If not modified, free cahe_buf_t argument and
   * create new cache_hit_t argument and send cache data */
  if (NotMod_Flag) {
    printf("Not modified\n");
    /* Update time stamp */
    cache->target->timestamp = ++c_header->timestamp;
    /* Update expires tag */
    cache->target->expires = cache->expires;
    /* Make cache hit argument */
    hit_arg = (cache_hit_t *)malloc(sizeof(cache_hit_t));
    hit_arg->header = NULL;
    hit_arg->cached_data = cache->target;
    bufferevent_setcb(partner, NULL, cached_data_writecb, NULL, hit_arg);
    bufferevent_enable(partner, EV_WRITE);
    free(cache);
    bufferevent_free(bev);
    cached_data_writecb(bev, hit_arg);
    return;
  }
  /* Except 304 case */
  else {
    /* Set read callback to body readcb and excute it */
    bufferevent_setcb(bev, proxy_server_bodycb, NULL, event_cb, ctx);
    proxy_server_bodycb(bev, ctx);
  }
}


/* Add received responsed data to client output buffer */
void proxy_server_bodycb(bufev *bev, void *ctx)
{
  cache_buf_t *cache = ctx;
  bufev *partner = cache->partner;
  struct evbuffer *src, *dst;
  size_t len;
  char buf[MAXLINE];

  /* Source is endserver's input buffer.
   * Destination is client's output buffer */
  src = bufferevent_get_input(bev);
  len = evbuffer_get_length(src);
  /* If client side pipe is broken, remove all received data */
  if (!partner) {
    evbuffer_drain(src, len);
    return;
  }

  /* Set destination buffer */
  dst = bufferevent_get_output(partner);

  /* Send until EOF */
  while (1) {
    len = evbuffer_remove(src, buf, MAXLINE);
    if (len <= 0) 
      return;
    evbuffer_add(dst, buf, len);
    if (cache->size + len < MAX_OBJ_SIZE) {
      memcpy(cache->buf + cache->size, buf, len);
      cache->size += len;
    }
  }
}


/* 
 * Wait for finishing write cb for client - proxy connection
 * If there is some argument (such as cached data) free it
 */
void close_on_finished_writecb(bufev *bev, void *ctx)
{
  cache_hit_t *cache = ctx;
  struct evbuffer *b = bufferevent_get_output(bev);
  if (evbuffer_get_length(b) == 0) {
    bufferevent_free(bev);
    if (cache) {
      /* In this case, it does not call event_cb so we save log
       * int this line (cache hit case) */
      Save_log_hit(cache);
      free(cache->header);
      free(cache);
    }
  }
}


/* 
 * When event occurs, check event and response it,
 * if there is some error, print error message,
 * otherwise if proxy - endserver connection has EOF
 * (which means readed all response data), free proxy-endserver event
 * and set cb to close_on_finished_writecb
 */
void event_cb(bufev *bev, short what, void *ctx) 
{
  cache_buf_t *cache = ctx;
  bufev *partner;
  if (cache != NULL) {
    partner = cache->partner;
  }

  if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
    /* Error occurs */
    if (what & BEV_EVENT_ERROR) {
      if (errno)
        perror("Connection error");
    }
    /* When EOF reached (in this case, read all received data */
    if (what & BEV_EVENT_EOF) {
      /* If there is no no-cache pragma save or update cache */
      if (!cache->NoCache_Flag) {
        /* If target exists and not NotModified, it means cached data is
         * modified. So update it and log it
         * - In handout, updating case does not explicitly described,
         * i decided to save it cache miss*/
        if (cache->target) {
          updateCache(c_header, cache->target, cache->buf, cache->path,
              cache->size, cache->Meta, cache->expires);
          Save_log_miss(cache);
        /* Otherwise, newly create cache object */
        }
        else {
          addCache(c_header, cache->buf, cache->path, cache->size,
              cache->Meta, cache->expires);
          Save_log_miss(cache);
        }
      }
      else {
        printf("No- cache\n");
        Save_log_No_cache(cache);
      }
    }
    /* If client-side connection exits */
    if (partner) {
      /* Flush received data */
      proxy_server_bodycb(bev, ctx);

      /* If some data remained in output buffer,
       * set callback to close on finished writecb */
      if (evbuffer_get_length(
            bufferevent_get_output(partner))) {
        bufferevent_setcb(partner,
            NULL, close_on_finished_writecb, NULL, NULL);
        bufferevent_disable(partner, EV_READ);
      } else {
        /* else terminate it */
        bufferevent_free(partner);
      }
    }
    /* Free cache_buf_t argument and buffer event 
     * between proxy and client */
    bufferevent_free(bev);
    free(cache);
  }
  return;
}


/* Event occurs when listning socket accepts some request */
void accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx)
{
  struct event_base *base = evconnlistener_get_base(listener);
  /* create new bufferevent between clinet and proxy */
  struct bufferevent *bev = bufferevent_socket_new(base,
      fd, BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

  /* Set callback function to read clients reqeust,
   * and passes clients address */
  bufferevent_setcb(bev, client_proxy_readcb, NULL, NULL, address);
  bufferevent_enable(bev, EV_READ|EV_WRITE);
}


/* When some error detected while accepting */
void accept_error_cb(struct evconnlistener *listener, void *ctx)
{
  struct event_base *base = evconnlistener_get_base(listener);
  int err = EVUTIL_SOCKET_ERROR();
  fprintf(stderr, "Got an error %d (%s) on the listener. "
      "Shutting down.\n", err, evutil_socket_error_to_string(err));
  event_base_loopexit(base, NULL);
}


int main(int argc, char **argv)
{
  struct event_base *base;
  struct evconnlistener *listener;
  struct sockaddr_in sin;

  /* Ignore broken pipe */
  Signal(SIGPIPE, SIG_IGN);

  /* Check valid excution */
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);
  /* Check valid port number */
  if (1024 > port || port > 65536) {
    fprintf(stderr, "Port number is not proper\n");
    return 1;
  }

  /* Establish listner address */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl(0);
  sin.sin_port = htons(port);

  base = event_base_new();
  /* Make new listner */
  listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
      LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
      (struct sockaddr*)&sin, sizeof(sin));

  if (!listener) {
    perror("Couldn't create listener");
    return 1;
  }
  evconnlistener_set_error_cb(listener, accept_error_cb);

  /* Init cache lists */
  c_header = cache_init();

  /* Remove all logged data previously, optional (see #define CLEAR part) */
#ifdef CLEAR
#pragma GCC diagnostic ignored "-Wformat-zero-length"
  FILE *log = Fopen(LOG, "w");
  fprintf(log, "");
  fflush(log);
  Fclose(log);
#pragma GCC diagnostic warning "-Wformat-zero-length"
#endif


  /* Run!! */
  event_base_dispatch(base);
  return 0;
}
