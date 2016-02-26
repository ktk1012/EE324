/*
 * libevent_proxy.c - function implementations in libevent_proxy.h
 */

#include "libevent_proxy.h"

unsigned int TOT_SIZE = 0;


/* Rio_package */
ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
  int cnt;

  while (rp->rio_cnt <= 0) {
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
        sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0) {
      if (errno != EINTR)
        return -1;
    } 
    else if (rp->rio_cnt == 0)
      return 0;
    else
      rp->rio_bufptr = rp->rio_buf;
  }

  cnt = n;
  if (rp->rio_cnt < n)
    cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr +=  cnt;
  rp->rio_cnt -= cnt;

  return cnt;
}
ssize_t Rio_readnb_w(rio_t *rp, void *buf, size_t n)
{
  size_t nleft = n;
  ssize_t nread;
  char *bufp = buf;
  errno = 0;
  while (nleft > 0) {
    if ((nread = rio_read(rp, bufp, nleft)) < 0) {
      if (errno == EINTR)
        nread = 0;
      else if (errno == EAGAIN) {
        break;
      }
      else
        return -1;
    }
    else if (nread == 0) {
      break;
    }
    nleft -= nread;
    bufp += nread;
  }
  return n - nleft;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *buf, size_t n)
{
  ssize_t temp;
  if ((temp = rio_readlineb(rp, buf, n)) < 0) {
    if (errno == EAGAIN) 
      return temp;
    perror("rio_readlineb failed");
  }
  return temp;
}

void Rio_writen_w(int fd, void *buf, size_t n)
{
  errno = 0;
  ssize_t written = rio_writen(fd, buf, n);
  if (written != n) {
    perror("rio_writen");
  }
}
/* buffer queue insert function */
void buf_queue_insert(buf_t *buf, fd_state *state)
{
  if (state->header == NULL) {
    state->header = buf;
    state->last = buf;
  }
  else {
    state->last->next = buf;
    state->last = buf;
  }
}

/* Uri parsing */

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
/* 
 * cache_init - Initialize cache list.
 * First initialize rw lock, 
 * and initialze header for doubly linked list
 */
cache_entry* cache_init()
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
  temp->timestamp = time(NULL);

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
  TOT_SIZE += obj_size;
  
  /* Insert cached node infront of header node.
   * This policy is quite heuristic, i thought that
   * recently saved cache is hit more likely than older ones*/
  if (header->next != NULL)
    header->next->prev = temp;
  temp->next = header->next;
  temp->prev = header;
  header->next = temp;

  /* If size exceeds MAX_CACHE_SIZE, evict some */
  while (TOT_SIZE > MAX_CACHE_SIZE) {
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
  temp->timestamp = time(NULL);

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
  TOT_SIZE -= temp->content_len;
  temp->content_len = obj_size;
  TOT_SIZE += obj_size;

  /* If size exceeds MAX_CACHE_SIZE, evict some */
  while (TOT_SIZE > MAX_CACHE_SIZE) {
    evictCache(header);
  }

}
/* 
 * evictCache - Evict one cache entry,
 * Find minimum timestamp and delete it.
 */
void evictCache(cache_entry* header)
{
  printf("Evict!, current total size: %d \n", TOT_SIZE);

  unsigned int LRU_time = time(NULL);
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
  TOT_SIZE -= min_entry->content_len;
  free(min_entry->path);
  free(min_entry->content);
  for (i = 0; i < 3; i++) {
    free(min_entry->Meta[i]);
  }
  free(min_entry);
}
