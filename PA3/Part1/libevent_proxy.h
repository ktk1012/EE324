/*
 * libevent_proxy.h - some helper functions for proxy, include caching objects
 */

#include "csapp.h"
#include <event2/event.h>
#include <event2/util.h>

#define MAX_CACHE_SIZE 5 * 1000 * 1000
#define MAX_OBJ_SIZE 512 * 1000


/* Cache entry object */
typedef struct cache_entry_t {
  struct cache_entry_t *next;
  struct cache_entry_t *prev;
  void *content;
  unsigned int content_len;
  time_t timestamp;
  char *path;
  char *Meta[3];
  time_t expires;
} cache_entry;


/* Chunks of read date to be written, managed by buf_queue of each fd state */
typedef struct buf_t {
  char *buf;
  int offset;
  int nleft;
  struct buf_t *next;
} buf_t;


/* Sttucture for state information of connection - endserver fd */
typedef struct fd_state {
  /* buffer for caching */
  char cache_buf[MAX_OBJ_SIZE];
  rio_t rio;
  evutil_socket_t connfd, endserverfd;
  int content_len;  /* Content length of response header */
  unsigned int size;  /* size of written data */
  struct sockaddr_in caddr;  /* client address */
  struct event *cp_read;  /* event pointer for client - proxy read */
  struct event *ps_read;  /* event pointer for proxy - server read */
  struct event *ps_body_read;  /* event pointer for response body entity read */
  struct event* cp_write;  /* event for client - proxy write */
  /* FIFO queue for chunked data */
  buf_t *header; 
  buf_t *last;

  /* Save state for cached object */
  cache_entry *target;
  /* For caching */
  char *path;
  char Meta[3][MAXLINE];
  time_t expires;
  
  /* For check No cache pragma, and state code */
  int status_code;
  char NoCache_Flag;
} fd_state;


/* Rio_package */
ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *buf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *buf, size_t n);
void Rio_writen_w(int fd, void *buf, size_t n);

/* Cache managements */
cache_entry* cache_init();
cache_entry* find_cache(cache_entry* list, char* path);
void addCache(cache_entry* list, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void updateCache(cache_entry* header, cache_entry* target, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void evictCache(cache_entry* list);

/* buffer queue insert function */
void buf_queue_insert(buf_t *buf, fd_state *state);

/* Parsing uri */
int parse_uri(char *uri, char *hostname, char* pathname, int *port);


