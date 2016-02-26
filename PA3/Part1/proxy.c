#define __USE_XOPEN
#define _GNU_SOURCE

#include "libevent_proxy.h"

//#define DEBUG
#ifdef DEBUG
#define dbg_printf printf
#else
#define dbg_printf dummy_printf
#endif

#define LOG "proxy.log"

void dummy_printf(const char *format, ...) {
  return;
}

/* global variables for cache header */
cache_entry *c_header;

/* Event callbacks */
void client_proxy_write(evutil_socket_t fd, short events, void *arg);
void client_proxy_read(evutil_socket_t fd, short events, void *arg);
void client_proxy_write_cache_content(evutil_socket_t fd, short events, void *args);
void proxy_server_read(evutil_socket_t fd, short events, void *arg);
void proxy_server_body_read(evutil_socket_t fd, short events, void *arg);
void free_fd_state(struct fd_state *state);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
    char *url, int size, short is_cached)
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
  c = (host >> 8) & 0xff;
  d = host & 0xff;


  /* Cache hit */
  if (is_cached == 1) 
    sprintf(logstring, "%s %d.%d.%d.%d %s %d Hit\n",
        time_str, a, b, c, d, url, size);
  else if(is_cached == 2)
    sprintf(logstring, "%s %d.%d.%d.%d %s %d Miss\n", 
        time_str, a, b, c, d, url, size);
  else 
    sprintf(logstring, "%s %d.%d.%d.%d %s %d No-cache\n",
        time_str, a, b, c, d, url, size);
}

void save_log(fd_state *state, short is_cached) {
  FILE *log = Fopen(LOG, "a");
  char log_string[MAXLINE];
  
  if (is_cached == 1)
    format_log_entry(log_string, &state->caddr, state->path,
        state->target->content_len, is_cached);
  else
    format_log_entry(log_string, &state->caddr, state->path,
        state->size, is_cached);
    

  fprintf(log, "%s", log_string);
  fflush(log);
  Fclose(log);
}






/* Allocate when accepted client socket */
fd_state* alloc_fd_state(struct event_base *base, evutil_socket_t fd, struct sockaddr_in *caddr)
{
  fd_state *state = calloc(1, sizeof(fd_state));
  if (!state)
    return NULL;
  state->cp_read = NULL;
  state->ps_read = NULL;
  state->connfd = fd;
  state->target = NULL;
  state->NoCache_Flag = 0;
  memcpy(&state->caddr, caddr, sizeof(struct sockaddr_in));
  state->cp_read = event_new(base, fd, EV_READ|EV_PERSIST, client_proxy_read, state);
  state->header = NULL;
  state->last = NULL;
  if (!state->cp_read) {
    free_fd_state(state);
    return NULL;
  }
 return state;
}

/* Update when proxy connected to endserver */
void update_fd_state(struct event_base *base, evutil_socket_t fd, fd_state *state)
{
  state->endserverfd = fd;
  state->ps_read = event_new(base, fd,
      EV_READ|EV_PERSIST, proxy_server_read, state);
  if (!state->ps_read)
    free_fd_state(state);
  state->ps_body_read = event_new(base, fd, EV_READ|EV_PERSIST,
      proxy_server_body_read, state);
}


void free_fd_state(fd_state *state)
{
  if (state->cp_read) 
    event_free(state->cp_read);
  if (state->ps_read)
    event_free(state->ps_read);
  if (state->ps_body_read)
    event_free(state->ps_body_read);
  if (state->cp_write)
    event_free(state->cp_write);
  free(state);
}

void client_proxy_read(evutil_socket_t fd, short events, void *arg)
{
  fd_state *state = arg;
  struct event_base *base = event_get_base(state->cp_read);
  char method[10];
  char uri[MAXLINE];
  char http_v[20];

  int d_port;
  char d_host[MAXLINE];
  char d_path[MAXLINE];
  
  cache_entry *target;

  char buf[MAXLINE];
  int n;

  time_t current;
  buf_t *temp_buf;
  int i;

  evutil_socket_t endserverfd;

  dbg_printf("start client_proxy_read from fd %d\n", fd);

  Rio_readinitb(&state->rio, fd);

  if ((n = Rio_readlineb_w(&state->rio, (void *)buf, MAXLINE)) < 0) {
    dbg_printf("Proxy: bad request\n");
    Close(fd);
    dbg_printf("asdfasdf\n");
    free_fd_state(state);
    return;
  }

  dbg_printf("cp_read: %s", buf);
  
  sscanf(buf, "%s %s %s", method, uri, http_v);
  if (strcasecmp(method, "GET")) {
    printf("Proxy: Invalid method\n");
    Close(fd);
    free_fd_state(state);
    return;
  }

  if ((strcmp(http_v, "HTTP/1.0")) && (strcmp(http_v, "HTTP/1.1"))) {
    printf("Proxy: Version mismatch\n");
    Close(fd);
    free_fd_state(state);
    return;
  }


  /* Check cached entry */
  target = find_cache(c_header, uri);
  /* Find cache */
  if (target) {
    state->path = calloc(1, MAXLINE);
    strcpy(state->path, target->path);
    state->target = target;
    current = time(NULL);
    if (target->expires > current) 
    {
      printf("Cache hit\n");
      /* cache hit update time stamp */
      target->timestamp = time(NULL);
      state->cp_write = event_new(base, fd,
          EV_WRITE|EV_PERSIST, client_proxy_write_cache_content, state);
      /* Make write buffer queues */
      sprintf(buf, "HTTP/1.0 200 OK\r\n");
      Rio_writen_w(fd, buf, strlen(buf));
      memset(buf, 0, MAXLINE);
      sprintf(buf, "Content-Length: %d\r\n", target->content_len);
      Rio_writen_w(fd, buf, strlen(buf));
      memset(buf, 0, MAXLINE);
      sprintf(buf, "Accept-Ranges: bytes\r\n");
      Rio_writen_w(fd, buf, strlen(buf));
      for (i = 0; i < 3; i++) {
        Rio_writen_w(fd, target->Meta[i], strlen(target->Meta[i]));
      }
      /* Add queue to content body */
      temp_buf = malloc(sizeof(buf_t));
      temp_buf->buf = target->content;
      temp_buf->offset = 0;
      temp_buf->nleft = target->content_len;
      buf_queue_insert(temp_buf, state);
      event_add(state->cp_write, NULL);
      event_del(state->cp_read);
      return;
    }
  }
  
  state->cp_write = event_new(base, fd, EV_WRITE|EV_PERSIST, client_proxy_write, state);
  if (!state->cp_write) {
    printf("Proxy: can not allocate event\n");
    Close(fd);
    free_fd_state(state);
    return;
  }

  if (parse_uri(uri, d_host, d_path, &d_port) < 0) {
    printf("Proxy: Parse failed\n");
    Close(fd);
    free_fd_state(state);
    return;
  }

  if ((endserverfd = open_clientfd(d_host, d_port)) < 0) {
    perror("Connect to endserver");
    Close(fd);
    free_fd_state(state);
    return;
  }
  evutil_make_socket_nonblocking(endserverfd);

  sprintf(buf, "%s /%s HTTP/1.0\r\n", method, d_path);
  dbg_printf("%s", buf);
  Rio_writen_w(endserverfd, buf, strlen(buf));
  /* If expires is before than current, use conditional get */
  if (state->target) {
    strftime(buf, MAXLINE, 
        "If-Modified-Since: %a, %d %b %Y %H:%M:%S %Z\r\n", gmtime(&current));
    Rio_writen_w(endserverfd, buf, strlen(buf));
  }
  /* Cache miss create cache */
  else {
    state->path = calloc(1, MAXLINE);
    sprintf(state->path, "%s/%s", uri, d_path);
  }

  while ((n = Rio_readlineb_w(&state->rio, buf, MAXLINE)) > 0) {
    dbg_printf("%s", buf);
    if (strcmp(buf, "\r\n")) {
      if (strncmp(buf, "Connection: keep-alive", strlen("Connection: keep-alive")) == 0) {
        Rio_writen_w(endserverfd, "Connection: close\r\n", strlen("Connection: close\r\n"));
      } else {
        Rio_writen_w(endserverfd, buf, n);
      }
    } else {
      Rio_writen_w(endserverfd, "\r\n", strlen("\r\n"));
      break;
    }
  }

  update_fd_state(base, endserverfd, state);
  event_add(state->ps_read, NULL);
  event_del(state->cp_read);
}

void client_proxy_write_cache_content(evutil_socket_t fd, short events, void *arg)
{
  fd_state *state = arg;
  ssize_t nwritten;
  buf_t *temp = state->header;
  while (temp->nleft > 0) {
    nwritten = write(fd, temp->buf + temp->offset, temp->nleft);
    if (nwritten <= 0) {
      if (errno == EAGAIN)
        return;
      else {
        free(temp);
        perror("write");
        Close(fd);
        free_fd_state(state);
        return;
      }
    }
    temp->nleft -= nwritten;
    temp->offset += nwritten;
  }
  /* Save log, with cache hit */
  save_log(state, 1);
  /* finish send cached object */
  Close(fd);
  free_fd_state(state);
}


void proxy_server_read(evutil_socket_t fd, short events, void *arg)
{
  fd_state *state = arg;
  evutil_socket_t connfd = state->connfd;
  int n, i;
  int content_len = 0;
  char buf[MAXLINE];
  char Expires[MAXLINE];
  char http_v[20], response_mes[100];
  struct tm Expires_t;

  buf_t *temp_buf;

  Rio_readinitb(&state->rio, fd);
  n = Rio_readlineb_w(&state->rio, buf, MAXLINE);
  sscanf(buf, "%s %d %s\r\n", http_v, &state->status_code, response_mes);
  if (state->status_code == 304) {
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen_w(connfd, buf, strlen(buf));
    for (i = 0; i < 3; i++)
      Rio_writen_w(connfd, state->target->Meta[i], strlen(state->target->Meta[i]));
  }
  else {
    sprintf(buf, "HTTP/1.0 %s", buf + strlen("HTTP/1.0 "));
    Rio_writen_w(connfd, buf, strlen(buf));
  }
  while ((n = Rio_readlineb_w(&state->rio, buf, MAXLINE)) > 0) {
    dbg_printf("%s", buf);
    if (strcmp(buf, "\r\n")) {
      Rio_writen_w(connfd, buf, n);
      if (strncmp(buf, "Content-Length", strlen("Content-Length")) == 0)
        content_len = atoi(buf + strlen("Content-Length: "));
      else if (strncmp(buf, "Last-Modified", strlen("Last-Modified")) == 0)
        strcpy(state->Meta[0], buf);
      else if (strncmp(buf, "Content-Type", strlen("Content-Type")) == 0)
        strcpy(state->Meta[1], buf);
      else if (strncmp(buf, "Content-Encoding", strlen("Content-Encoding")) == 0)
        strcpy(state->Meta[2], buf);
      else if (strncmp(buf, "Expires", strlen("Expires")) == 0)
        strcpy(Expires, buf + strlen("Expires: "));
      /* If header contains no-cache pragma, set NoCache_Flag to 1 */
      else if (strstr(buf, "no-cache"))
        state->NoCache_Flag = 1;
    } else {
      Rio_writen_w(connfd, "\r\n", strlen("\r\n"));
      break;
    }
  }
  /* If resposne is not modified, go to cache_content write callback */
  if (state->status_code == 304) {
    printf("Not modified\n");
    /* Update time stamp */
    state->target->timestamp = time(NULL);
    event_free(state->cp_write);
    state->cp_write = event_new(event_get_base(state->ps_read), connfd,
        EV_WRITE|EV_PERSIST, client_proxy_write_cache_content, state);
    temp_buf = malloc(sizeof(buf_t));
    temp_buf->buf = state->target->content;
    temp_buf->offset = 0;
    temp_buf->nleft = state->target->content_len;
  }
  /* Expires time exists */
  if (strlen(Expires)) {
    strptime(Expires, "%a, %d %b %Y %H:%M:%S %Z\r\n", &Expires_t);
    state->expires = mktime(&Expires_t) + 30600;
  }
  else {
    state->expires = time(NULL) + 3600;
  }
  state->content_len = content_len;
  state->size = 0;
  event_del(state->ps_read);
  event_add(state->ps_body_read, NULL);
}


void proxy_server_body_read(evutil_socket_t fd, short events, void *arg)
{
  fd_state *state = arg;
  evutil_socket_t connfd = state->connfd;
  int n;
  int content_len = state->content_len;
  char buf[MAXLINE];
  if (content_len == 0)
    content_len = -1;

  while (1) {
    memset(buf, 0, MAXLINE);
    n = Rio_readnb_w(&state->rio, buf, MAXLINE);
    if ((n < 0) && (errno != EAGAIN)) {
      perror("Rio_readnb");
      return;
    }
    else if (n == 0) {
      if (errno != EAGAIN) {
        if (state->size == content_len || content_len == -1) { /* EOF */
          Close(connfd);
          Close(fd);
          free_fd_state(state);
          return;
        }
      }
      event_del(state->ps_body_read);
      event_add(state->cp_write, NULL);
      return;
    }
    else if (n > 0) {
      buf_t *buf_q = malloc(sizeof(buf_t));
      buf_q->buf = calloc(1, n);
      memcpy(buf_q->buf, buf, n);
      buf_q->nleft = n;
      buf_q->offset = 0;
      buf_q->next = NULL;
      buf_queue_insert(buf_q, state);
    }
  }
}


/* Except cache hit and Not modifed case */
void client_proxy_write(evutil_socket_t fd, short events, void *arg)
{
  ssize_t nwritten;
  fd_state *state = arg;
  buf_t *temp;
  for (temp = state->header; temp != NULL; ) {
    while(temp->nleft > 0) {
      nwritten = write(fd, temp->buf + temp->offset, temp->nleft);
      if (nwritten <= 0) {
        if (errno == EAGAIN)
          return;
        else {
          state->header = temp->next;
          free(temp->buf);
          free(temp);
          perror("write");
          event_del(state->cp_write);
          event_add(state->ps_body_read, NULL);
          return;
        }
      }
      temp->nleft -= nwritten;
      temp->offset += nwritten;
      state->size += nwritten;
    }
    /* add cache object to data */
    if (state->size < MAX_OBJ_SIZE && !state->NoCache_Flag)
      memcpy(state->cache_buf + state->size - temp->offset, temp->buf, temp->offset);
    state->header = temp->next;
    free(temp->buf);
    free(temp);
    temp = state->header;
  }
  if (state->size == state->content_len) {
    /* If total size is less than MAX_OBJ_SIZE, add cache */
    if (!state->NoCache_Flag && state->size < MAX_OBJ_SIZE) {
      /* Cache miss */
      if (!state->target) {
        addCache(c_header, (void *)state->cache_buf, state->path,
            state->size, state->Meta, state->expires);
        save_log(state, 2);
      }
      /* Update cache entry, we may assume that this is cache hit case,
       * when logging */
      else {
        updateCache(c_header, state->target, (void *)state->cache_buf, 
            state->path, state->size, state->Meta, state->expires);
        save_log(state, 1);
      }
    }
    /* No cache logging */
    if (state->NoCache_Flag)
      save_log(state, 3);
    Close(fd);
    Close(state->endserverfd);
    free_fd_state(state);
  }
  else {
    event_del(state->cp_write);
    event_add(state->ps_body_read, NULL);
  }
}


void do_accept(evutil_socket_t listener, short events, void *arg)
{
  dbg_printf("accept\n");
  struct event_base *base = arg;
  struct sockaddr_in caddr;
  socklen_t slen = sizeof(caddr);
  int fd = accept(listener, (struct sockaddr*)&caddr, &slen);
  if (fd < 0) { // XXXX eagain??
    perror("accept");
  } else if (fd > FD_SETSIZE) {
    close(fd); // XXX replace all closes with EVUTIL_CLOSESOCKET */
  } else {
    fd_state *state;
    evutil_make_socket_nonblocking(fd);
    state = alloc_fd_state(base, fd, &caddr);
    event_add(state->cp_read, NULL);
  }
}


int main(int argc, char **argv)
{
  evutil_socket_t listener;
  struct event_base *base;
  struct event *listener_event;
  int port;

  signal(SIGPIPE, SIG_IGN);

  /* Check arguments */
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
    exit(0);
  }

  port = atoi(argv[1]);

  /* Check port number is valid */
  if ((port < 1024) || (port > 65536)) {
    fprintf(stderr, "Port number is not proper\n");
    exit(0);
  }

  base = event_base_new();
  if (!base) {
    fprintf(stderr, "Fail to create event base\n");
    exit(0);
  }
    
  c_header = cache_init();

  listener = Open_listenfd(port);
  evutil_make_socket_nonblocking(listener);
  evutil_make_listen_socket_reuseable(listener);
  fcntl(listener, F_SETFL, SOCK_CLOEXEC);

  listener_event = event_new(base, listener, EV_READ|EV_PERSIST, do_accept, (void*)base);
  dbg_printf("start!\n");
  event_add(listener_event, NULL);

  event_base_dispatch(base);
  close(listener);

  return 0;
}

