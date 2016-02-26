/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *    Tae-kyeom, Kim (id: 20130185), kimtkyeom@kaist.ac.kr
 *  - Please read README
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

/* Use pre processor definition for using strptime function */
#define __USE_XOPEN
#define _GNU_SOURCE


#include "csapp.h"

/* Define MAX_CACHE_SIZE (5Mbytes),
 * and MAX_OBJ_SIZE (512 Kbytes) */
#define MAX_CACHE_SIZE 5 * 1000 * 1000
#define MAX_OBJ_SIZE 512 * 1000

#define LOG "proxy.log"

/*
 * structure for caching ogject
 * cache_entry is for cache entry
 * Use doubly linked list
 */
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

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void save_log(struct sockaddr_in *sockaddr,
    char* uri, int size, sem_t* sem);
/* Rio wrappers */
ssize_t Rio_readnb_w(rio_t *rp, void *buf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void* buf, size_t n);
void Rio_writen_w(int fd, void *buf, size_t n);
/* thread function */
void *Request_handler(void *arg);
/* thread safe client socket creation function */
int Open_clientfd_ts(char *host, int port, sem_t *semp);
/* Functions for caching */
cache_entry* cache_init();
cache_entry* find_cache(cache_entry* list, char* path);
void addCache(cache_entry* list, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void updateCache(cache_entry* header, cache_entry* target, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir);
void evictCache(cache_entry* list);

/* Semaphore for mutual exclusion */
sem_t sem;

/* Global counter for time stamp */
unsigned int stamp = 0;

/* Global variable for total size */
unsigned int TOT_SIZE = 0;

/* rw lock for cache list */
pthread_rwlock_t lock;

/* Cache_list */
cache_entry* c_header;

/* argument structre for thread */
typedef struct arg {
  struct sockaddr_in caddr;   /* Client address */
  int connfd; /* connection file descriptor */
} arg_t;


/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
  int port;  // port number for proxy
  int listenfd;  // listen sockdt file descriptor

  socklen_t addr_len;  // socket length type
  
  pthread_t tid;  // Use for thread creation

  arg_t *arg_tp = NULL;  // Pointer to argument type


  Signal(SIGPIPE, SIG_IGN);

  /* Check arguments */
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
    exit(0);
  }


  /* Create listen file descriptor */
  port = atoi(argv[1]);

  /* Check port number is valid */
  if ((port < 1024) || (port > 65536)) {
    fprintf(stderr, "Port number is not proper\n");
    exit(0);
  }
  listenfd = Open_listenfd(port);

  /* Initialize semaphore */
  Sem_init(&sem, 0, 1);
  
  /* Initialize cache_header */
  c_header = cache_init();

  while (1) {
    /* 
     * Prevent race condition dynamically allocate argument type for each thread
     */
    arg_tp = (arg_t *)Malloc(sizeof(arg_t));
    addr_len = sizeof(arg_tp -> caddr);
    printf("Proxy: Listening\n");
    /* Accept client request and set connection fd for thread */
    arg_tp -> connfd = 
      Accept(listenfd, (struct sockaddr *)&arg_tp->caddr, &addr_len);

    /* Create thread handling each request */
    Pthread_create(&tid, NULL, Request_handler, (void *)arg_tp);
  }
  
  /* Close listen socket and file pointer */
  close(listenfd);
  exit(0);
}



/*
 * Open_clientfd_ts - thread safe Open_clientfd function 
 */
int Open_clientfd_ts(char *host, int port, sem_t *semp)
{
  int clientfd;
  struct hostent *hp;
  struct sockaddr_in daddr;

  if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return -1;   /* socket creation failed */

  /* gethostby name is thread unsafe, use semaphore */
  P(semp);
  if ((hp = gethostbyname(host)) == NULL)
    return -2;  /* get hostby name failed */
  V(semp);

  /* Initailize destination addr structure */
  bzero((char *) &daddr, sizeof(daddr));
  daddr.sin_family = AF_INET;
  bcopy((char *) hp->h_addr,
      (char *)&daddr.sin_addr.s_addr, hp->h_length);
  /* set port number */
  daddr.sin_port = htons(port);

  /* Establish connection */
  if (connect(clientfd, (struct sockaddr *)&daddr, sizeof(daddr)) < 0)
    return -1;

  return clientfd;
}


/*
 * Request_handler - thread based request handler 
 * argument is passed (void *)arg_t
 */
void *Request_handler(void *arg)
{
  /* for socket communication */
  struct sockaddr_in caddr;  // Client address
  int connfd;  // connection fd, which is communication with client
  int endserverfd;  // fd for end server communication

  int i;

  /* 
   * Use for check method, parsing uri,
   * and check version of request header, respectively
   */
  char method[10];
  char uri[MAXLINE];
  char http_v[20];

  /*
   * For response header 
   */
  char response_method[10];
  int status_code;
  char response_mes[40];

  /*
   * After parsed uri, connection to end server with
   * d_host and d_port. And get requested file using d_path
   */
  char d_host[MAXLINE];
  char d_path[MAXLINE];
  int d_port;

  /* Value for memorize response header length */
  int resp_len;
  /* Value for content length(size except response header) */
  int content_len;

  /* Use for read/written bytes of rio */
  int n;
 
  /* Rio type and buffer for read/writting */
  rio_t rio;
  char path[MAXLINE];
  char buf[MAXLINE];
  
  /* Cache buffer */
  char cache_buf[MAX_OBJ_SIZE];
  cache_entry* target = NULL;
  char NotMod_Flag = 0;  /* Flag for not modified response */
  char NoCache_Flag = 0;  /* Flag for no cache header */
 
  /* string for save expires tag */
  char Expires[MAXLINE];

  /*
   * Meta datas for http response.
   * I will save some optional meta data,
   * LastModified, ContentType, Content-Encoding 
   */
  char Meta[3][MAXLINE];

  /* For time entry */
  struct tm Expires_t;
  time_t curr_t, expires_t;  /* time_t variable for current time and expires time */
  char current_time_s[MAXLINE];

  /* Detach the thread */
  Pthread_detach(Pthread_self());

  /* copy passed argument */
  //arg_t ar = *(arg_t *)arg;
  connfd = ((arg_t *)arg)-> connfd;
  caddr = ((arg_t *)arg)->caddr;
  free(arg);

  /* Initialize connfd with rio_t */
  rio_readinitb(&rio, connfd);
  /*
   * Read first line of http request header.
   * Note that method and uri, and version is in this line.
   * And also note that buffered rio functions are thread safe
   */
  if((n = Rio_readlineb(&rio, (void *)buf, MAXLINE)) < 0) {
    printf("Proxy: bad request\n");
    close(connfd);
    return NULL;
  }
  
  /* Parse into method, uri, and http version */
  sscanf(buf, "%s %s %s", method, uri, http_v);

  /* Check method, we only accept GET method */
  if (strcasecmp(method, "GET")) {
    printf("Proxy: Invalid method\n");
    close(connfd);
    return NULL;
  }

  /* Check version of http request, valid version is 1.0 and 1.1 */
  if ((strcmp(http_v, "HTTP/1.0")) && (strcmp(http_v, "HTTP/1.1"))) {
    printf("Proxy: Version mismatch\n");
    close(connfd);
    return NULL;
  }

  /* Parse uri into hostname, hostpath, and port number */
  if (parse_uri(uri, d_host, d_path, &d_port) < 0) {
    printf("Proxy: parse failed\n");
    close(connfd);
    return NULL;
  }
  
  /* Concatenate strings to find cache using path, and 
   * also used for save cache 
   */
  sprintf(path, "%s/%s", uri, d_path);

  /* Find cache and compare between current time and expries time */
  /* Get current time */
  P(&sem);
  curr_t = time(NULL);
  strftime(current_time_s, MAXLINE, "%a, %d %b %Y %H:%M:%S %Z", gmtime(&curr_t));
  V(&sem);
  
  /* Find cached object */
  pthread_rwlock_wrlock(&lock);
  target = find_cache(c_header, path);

  /* 
   * If we find cached object and it is less than expires time,
   * Just send cached object and finish 
   */
  if (target) {
    if (target->expires > curr_t)
    {
      /* Send cached object and update time stamp */
      sprintf(buf, "HTTP/1.0 200 OK\r\n");
      Rio_writen_w(connfd, buf, strlen(buf));
      memset(buf, 0, MAXLINE);
      sprintf(buf, "Content-Lenght: %d\r\n", target->content_len);
      Rio_writen_w(connfd, buf, strlen(buf));
      memset(buf, 0, MAXLINE);
      sprintf(buf, "Accept-Ranges: bytes\r\n");
      Rio_writen_w(connfd, buf, strlen(buf));
      /* send meta data */
      for (i = 0; i < 3; i++)
        Rio_writen_w(connfd, target->Meta[i], strlen(target->Meta[i]));
      memset(buf, 0, MAXLINE);
      sprintf(buf, "\r\n");
      Rio_writen_w(connfd, buf, strlen(buf));
      /* Send pay load data of http */
      Rio_writen_w(connfd, target->content, target->content_len);
      printf("Cache hits\n");
      /* save log */
      save_log(&caddr, uri, 0, &sem); 
      /* Update time stamp */
      target->timestamp = ++stamp;
      pthread_rwlock_unlock(&lock);
      close(connfd);
      return NULL;
    }
  }
  pthread_rwlock_unlock(&lock);

  /* Connect to endserver with parsed host name */
  if ((endserverfd = Open_clientfd_ts(d_host, d_port, &sem)) < 0) {
    printf("Proxy: connection failed\n");
    close(connfd);
    return NULL;
  }



  /* 
   * Send http request to endserver 
   * Note that we send all received request to endserver.
   * request is terminated when reach \r\n message. so send request
   * until reaching \r\n. Note that we do not consider persistant connection,
   * so if we meet keep-alive request, change to close request. and 
   * change version to HTTP/1.1
   */

  sprintf(buf, "%s /%s HTTP/1.0\r\n", method, d_path);
  Rio_writen_w(endserverfd, buf, strlen(buf));

  /* If find cache entry send conditional get */
  if (target != NULL) {
    sprintf(buf, "If-Modified-Since: %s\r\n", current_time_s);
    Rio_writen(endserverfd, buf, strlen(buf));
  }

  while ((Rio_readlineb_w(&rio, (void *)buf, MAXLINE)) > 0) {
    if (strcmp(buf, "\r\n")) {
      /* If connection is persistant, change to non persistant connection */
      if(strncmp(buf, "Connection: keep-alive", strlen("Connection: keep-alive")) == 0) {
        Rio_writen_w(endserverfd, "Connection: close\r\n",
            strlen("Connection: close\r\n"));
      } else {
        Rio_writen_w(endserverfd, buf, strlen(buf));
      }
    } else {
      /* Termination */
      Rio_writen_w(endserverfd, "\r\n", strlen("\r\n"));
      break;
    }
  }

  /* Initialize rio_t for endserverfd for receiving response */
  Rio_readinitb(&rio, endserverfd);
  resp_len = 0;
  content_len = 0;

  /* Read first line of response header */
  n = Rio_readlineb_w(&rio, (void *)buf, MAXLINE);
  resp_len += n;
  sscanf(buf, "%s %d %s\r\n", response_method, &status_code, response_mes);

  /* If not modified, set Not mod flag to 1 */
  if (status_code == 304) {
    NotMod_Flag = 1;
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
  }
 
  /* Send first line of HTTP response header */
  sprintf(buf, "HTTP/1.0 %s", buf + strlen("HTTP/1.0 "));
  Rio_writen_w(connfd, buf, strlen(buf));
  memset(buf, 0, MAXLINE);

  /* If 304 response is received, send cached data */
  if (NotMod_Flag == 1) {
    pthread_rwlock_rdlock(&lock);
    sprintf(buf, "Content-Lenght: %d\r\n", target->content_len);
    Rio_writen_w(connfd, buf, strlen(buf));
    memset(buf, 0, MAXLINE);
    sprintf(buf, "Accept-Ranges: bytes\r\n");
    Rio_writen_w(connfd, buf, strlen(buf));
    /* send meta data */
    for (i = 0; i < 3; i++)
      Rio_writen_w(connfd, target->Meta[i], strlen(target->Meta[i]));
    memset(buf, 0, MAXLINE);
    pthread_rwlock_unlock(&lock);
  }

  /* Send remains of respons header */
  memset(Expires, 0, MAXLINE);
  while ((n = Rio_readlineb_w(&rio, (void *)buf, MAXLINE)) > 0) {
    resp_len += n;
    if (strcmp(buf, "\r\n")) {
      /* Save some Meta data */
      if (strncmp(buf, "Last-Modified", strlen("Last-Modified")) == 0)
        strcpy(Meta[0], buf);
      else if (strncmp(buf, "Content-Type", strlen("Content-Type")) == 0)
        strcpy(Meta[1], buf);
      else if (strncmp(buf, "Content-Encoding", strlen("Content-Encoding")) == 0)
        strcpy(Meta[2], buf);
      else if (strncmp(buf, "Expires", strlen("Expires")) == 0)
        strcpy(Expires, buf + strlen("Expires: "));
      /* If header contains no-cache pragma, set NoCache_Flag to 1 */
      else if (strstr(buf, "no-cache"))
        NoCache_Flag = 1;
      /* End of saving meta data */
      Rio_writen_w(connfd, buf, n);
    }
    else {
      Rio_writen_w(connfd, "\r\n", strlen("\r\n"));
      break;
    }
  }
  bzero(buf, MAXLINE);

  /* Parse Expires time string to time_t, as 
   * strptime is not thread safe, use lock*/
  P(&sem);
  memset(&Expires_t, 0, sizeof(struct tm));
  if (strlen(Expires)) {
    strptime(Expires, "%a, %d %b %Y %H:%M:%S %Z\r\n", &Expires_t);
    /* Because of some timezone issues in strptime, calibrates to UTC time,
     * * (this treatment is quite locale specific, as strptime parses time into
     * * KST timezone.*/
    expires_t =  mktime(&Expires_t) + 30600;
  }
  /* If there is no Expires in response header, set default expires time
   * (I decided that default is 1 hours) */
  else {
    expires_t = time(NULL) + 3600;
  }
  V(&sem);
  
  content_len = 0;

  /* If response status is 304, send cached data */
  if (NotMod_Flag == 1) {
    pthread_rwlock_wrlock(&lock);
    printf("Not modified\n");
    Rio_writen_w(connfd, target->content, target->content_len);
    /* Update expires time */
    target->expires = expires_t;
    pthread_rwlock_unlock(&lock);
  }
  /* Receive response and forward to client immediately */
  else {
    while ((n = Rio_readnb_w(&rio, (void *)buf, MAXLINE)) > 0) {
      Rio_writen_w(connfd, buf, n);
      /* Copy to cache buffer until full */
      if (content_len + n <= MAX_OBJ_SIZE) {
        memcpy(cache_buf + content_len, buf, n);
      }
      bzero(buf, MAXLINE);
      content_len += n;
    }
    /* If it does not have no-cache pragma and code is 20x or 304(Update expires time),
     * and also size is less than MAX_OBJ_SIZE, save it
     */
    if (content_len <= MAX_OBJ_SIZE && (status_code / 100 == 2)) {
      if (!NoCache_Flag) {
        /* If cache object is exists update it */
        if (target)
          updateCache(c_header, target, (void *)cache_buf, path,
              content_len, Meta, expires_t); 
        /* Newly add to cache object */
        else
          addCache(c_header, (void *)cache_buf, path,
            content_len, Meta, expires_t);
      }
      else {
        printf("Pragma: no-cache\n");
      }
    }
  }

  /*
   * Write log entry to log file. Note that writting file is not thread safe
   * (Use file stream) 
   * Note that total size of response is resp_len + content_len
   */
  save_log(&caddr, uri, resp_len + content_len, &sem);

  /* Close the connection, since we assumed connection is non-persistant */
  close(connfd);
  close(endserverfd);

  return NULL;
}




/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
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
 * Rio wrappers
 */

ssize_t Rio_readnb_w(rio_t *rp, void *buf, size_t n)
{
  ssize_t temp;
  if ((temp = rio_readnb(rp, buf, n)) < 0) {
    printf("Rio_readn failed\n");
    return 0;
  }
  return temp;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *buf, size_t n)
{
  ssize_t temp;
  if ((temp = rio_readlineb(rp, buf, n)) < 0) {
    printf("rio_readlineb failed\n");
    return 0;
  }
  return temp;
}

void Rio_writen_w(int fd, void *buf, size_t n)
{
  if (rio_writen(fd, buf, n) != n) {
    printf("rio_writen failed\n");
  }
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}

/*
 * save_log - save log to proxy.log
 */
void save_log(struct sockaddr_in *sockaddr,
    char* uri, int size, sem_t* sem)
{
  FILE *log;
  char logstring[MAXLINE];
  P(sem);
  format_log_entry(logstring, sockaddr, uri, size);
  log = Fopen(LOG, "a");
  fprintf(log, "%s %d\n", logstring, size);
  fflush(log);
  Fclose(log);
  V(sem);
}

/* 
 * cache_init - Initialize cache list.
 * First initialize rw lock, 
 * and initialze header for doubly linked list
 */
cache_entry* cache_init()
{
  pthread_rwlock_init(&lock, NULL);
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
  /* 
   * This function is called by thread, and before calling,
   * thread locks writing lock, so this function does not
   * contain any rw lock
   */
  cache_entry* temp = header->next;

  while (temp != NULL) {
    /*
     * When cache hit occurs, send to client.
     * return 1(success) and update timestamp
     */

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
  /* 
   * It modifes cache list, which is shared by all proxy threads,
   * we should wrlock for this procedure
   */
  int i;
  pthread_rwlock_wrlock(&lock);
  printf("cache miss, add cache\n");
  /* Create cache entry object for caching */
  cache_entry* temp = calloc(1, sizeof(cache_entry));
  /* Allocate content area and save it */
  temp->content = calloc(1, obj_size);
  memcpy(temp->content, data, obj_size);

  /* Update timestamp increasing manner */
  temp->timestamp = ++stamp;

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

  pthread_rwlock_unlock(&lock);
}

/*
 * updateCache - Update cache entry, when 200 OK response is arrived
 * with existing object. It frees all dynamically allocated memeries
 * (path, data, meta data)
 */
void updateCache(cache_entry* header, cache_entry* target, void* data, char* path,
    unsigned int obj_size, char meta_option[3][MAXLINE], time_t expir)
{
  /* 
   * It modifes cache list, which is shared by all proxy threads,
   * we should wrlock for this procedure and update new expires time
   */
  int i;
  pthread_rwlock_wrlock(&lock);
  printf("Update cache %s\n", target->path);
  cache_entry* temp = target;
  /* Create cache entry object for caching */
  /* Allocate content area and save it */
  free(temp->content);
  temp->content = calloc(1, obj_size);
  memcpy(temp->content, data, obj_size);

  /* Update timestamp increasing manner */
  temp->timestamp = ++stamp;

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

  pthread_rwlock_unlock(&lock);
}
/* 
 * evictCache - Evict one cache entry,
 * Find minimum timestamp and delete it.
 * Note that this function is called only from addCache,
 * which has already wrlocked one, so we do not concern about
 * locks.
 */
void evictCache(cache_entry* header)
{
  printf("Evict!, current total size: %d \n", TOT_SIZE);

  unsigned int LRU_time = stamp;
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
