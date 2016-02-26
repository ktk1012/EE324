#include "cache.h"


/* 
 * cache_init - Initialize cache list.
 * First initialize rw lock, 
 * and initialze header for doubly linked list
 */
cache_entry* cache_init(void)
{
  cache_entry* header = (cache_entry *)Malloc(sizeof(cache_entry));
  memset(header, 0, sizeof(cache_entry));
  return header;
}

/* 
 * find_cache - Find cache entry, if find corresponding path, 
 * return its address, else return NULL.
 */
cache_entry* find_cache(cache_entry* header, char* path)
{
  cache_entry* temp = header->next;
  while (temp != NULL) {
    if(!strcmp(temp->path, path))
      return temp;
    temp = temp->next;
  }
  /* Cache miss */
  return NULL;
}

/*
 * addCache - Add cache entry. If total size is larget than MAX_CACHE_SIZE,
 * evict some entries wity LRU policy
 */
void addCache(cache_entry* header, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir)
{
  printf("Add cache\n");
  int i;
  /* Create cache entry object for caching */
  cache_entry* temp = calloc(1, sizeof(cache_entry));
  /* Allocate content area and save it */
  temp->content = calloc(1, obj_size);
  memcpy(temp->content, data, obj_size);

  /* Update timestamp increasing manner */
  temp->timestamp = ++header->timestamp;;

  /* Save uri for search identifier */
  temp->path = calloc(1, MAXLINE);
  strcpy(temp->path, path);

  /* Save Meta data option */
  for (i = 0; i < 3; i++){
    temp->Meta[i] = calloc(1, MAXLINE);
    strcpy(temp->Meta[i], meta_option[i]);
  }

  temp->expires = expir;

  /* Write object size of each cache and total cache */
  temp->content_len = obj_size;
  header->content_len += obj_size;
  
  /* Insert cached node infront of header node.
   * This policy is quite heuristic, i thought that
   * recently saved cache is hit more likely than older ones*/
  if (header->next != NULL)
    header->next->prev = temp;
  temp->next = header->next;
  temp->prev = header;
  header->next = temp;

  /* If size exceeds MAX_CACHE_SIZE, evict some */
  while (header->content_len > MAX_CACHE_SIZE) {
    evictCache(header);
  }

}

/*
 * updateCache - Update cache entry, when 200 OK response is arrived
 * with existing object. It frees all dynamically allocated memeries
 * (path, data, meta data)
 */
void updateCache(cache_entry* header, cache_entry* target, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir)
{
  int i;
  printf("Update cache %s\n", target->path);
  cache_entry* temp = target;
  /* Create cache entry object for caching */
  /* Allocate content area and save it */
  free(temp->content);
  temp->content = calloc(1, obj_size);
  memcpy(temp->content, data, obj_size);

  /* Update timestamp increasing manner */
  temp->timestamp = ++header->timestamp;

  /* Save uri for search identifier */
  free(temp->path);
  temp->path = calloc(1, MAXLINE);
  strcpy(temp->path, path);

  /* Update meta options, it updates when Meta option is changed */
  for (i=0; i < 3; i++) {
    if (strcpy(temp->Meta[i], meta_option[i]) == 0) {
      free(temp->Meta[i]);
      temp->Meta[i] = calloc(1, MAXLINE);
      strcpy(temp->Meta[i], meta_option[i]);
    }
  }

  temp->expires = expir;

  /* Write object size of each cache and total cache */
  header->content_len -= temp->content_len;
  temp->content_len = obj_size;
  header->content_len += obj_size;

  /* If size exceeds MAX_CACHE_SIZE, evict some */
  while (header->content_len > MAX_CACHE_SIZE) {
    evictCache(header);
  }

}
/* 
 * evictCache - Evict one cache entry,
 * Find minimum timestamp and delete it.
 */
void evictCache(cache_entry* header)
{
  printf("Evict!, current total size: %d \n", header->content_len);

  unsigned int LRU_time = header->timestamp;
  int i;

  cache_entry* temp = header->next;
  cache_entry* min_entry = header->next;

  /* 
   * Find minimum stamped object
   * as I updated strictly increasing manner, we always find 
   * the smallest one
   */
  while (temp != NULL)
  {
    if (temp->timestamp < LRU_time) {
      LRU_time = temp->timestamp;
      min_entry = temp;
    }
    temp = temp->next;
  }

  /* 
   * Evict minimum timestamp entry. 
   * This procedure is almost smae as delete node
   * from doubly linked list.
   */
  min_entry->prev->next = min_entry->next;
  if (min_entry->next != NULL) {
    min_entry->next->prev = min_entry->prev;
  } else {
    min_entry->prev->next = NULL;
  }
  /* Update total size of cached objects */
  header->content_len -= min_entry->content_len;
  free(min_entry->path);
  free(min_entry->content);
  for (i = 0; i < 3; i++) {
    free(min_entry->Meta[i]);
  }
  free(min_entry);
}

char *make_response_header(cache_entry *target)
{
  int i;
  char time_str[MAXLINE];
  strftime(time_str, MAXLINE, "%a, %d %b %Y %H:%M:%S %Z",
      gmtime(&target->expires));
  char *header = (char *)malloc(MAXLINE);
  memset(header, 0, MAXLINE);
  sprintf(header, "HTTP/1.0 200 OK\r\n");
  sprintf(header, "%sContent-Length: %d\r\n", header, target->content_len);
  sprintf(header, "%sAccept-Ranges: bytes\r\n", header);
  for (i = 0; i < 3; i++)
    sprintf(header, "%s%s", header, target->Meta[i]);
  sprintf(header, "%sExpires: %s\r\n", header, time_str);
  sprintf(header, "%s\r\n", header);
  return header;
}
 
/* End caching */


/* Parse uri */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    /*char *hostend;
    char *pathbegin;
    int len;*/
    
    char *temp;
    char *saveptr;

    if (strncasecmp(uri, "http://", 7) != 0) {
      hostname[0] = '\0';
      return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
   
    /* Extract path name */
    temp = strtok_r(hostbegin, "/", &saveptr);
    strcpy(hostbegin, temp);
    strcpy(pathname, saveptr);
    /* Extract port number and host name */
    temp = strtok_r(hostbegin, ":", &saveptr);
    strcpy(hostname, temp);
  
    temp = strtok_r(NULL, ":", &saveptr);
    *port = 80;
    if (temp)
      *port = atoi(temp);

    return 0;
}


