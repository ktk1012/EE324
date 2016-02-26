/*
 * cache.h - Functions for cache management 
 *  20130185 - Tae-kyeom, Kim
 */

#include "csapp.h"

#define MAX_OBJ_SIZE 512 * 1000
#define MAX_CACHE_SIZE 5 * 1000 * 1000

/* Cache entry object */
typedef struct cache_entry_t {
  struct cache_entry_t *next;
  struct cache_entry_t *prev;
  void *content;
  unsigned int content_len;
  unsigned int timestamp;
  char *path;
  char *Meta[3];
  time_t expires;
} cache_entry;

/* Function prototypes for caching */
cache_entry *cache_init(void);
cache_entry *find_cache(cache_entry *header, char *path);
void addCache(cache_entry* list, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void updateCache(cache_entry* header, cache_entry* target, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void evictCache(cache_entry* list);
char *make_response_header(cache_entry *target);

/* Parsing reqeust */
int parse_uri(char *uri, char *hostname, char *pathname, int *port);
