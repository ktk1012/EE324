/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *    Tae-kyeom, Kim (id: 20130185), kimtkyeom@kaist.ac.kr
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

#define LOG "proxy.log"

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
/* Rio wrappers */
ssize_t Rio_readnb_w(rio_t *rp, void *buf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void* buf, size_t n);
void Rio_writen_w(int fd, void *buf, size_t n);
/* thread function */
void *Request_handler(void *arg);
/* thread safe client socket creation function */
int Open_clientfd_ts(char *host, int port, sem_t *semp);

/* Semaphore for mutual exclusion */
sem_t sem;

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
  /* memcpy is not thread safe, so use bcopy instead */
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

  /* File pointer for write log entry */
  FILE *log;
  char log_entry[MAXLINE];

  /* 
   * Use for check method, parsing uri,
   * and check version of request header, respectively
   */
  char method[10];
  char uri[MAXLINE];
  char http_v[20];

  /*
   * After parsed uri, connection to end server with
   * d_host and d_port. And get requested file using d_path
   */
  char d_host[MAXLINE];
  char d_path[MAXLINE];
  int d_port;

  /* Value for memorize response length */
  int resp_len;

  /* Use for read/written bytes of rio */
  int n;
 
  /* Rio type and buffer for read/writting */
  rio_t rio;
  char buf[MAXLINE];
  
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
  /* Receive response and forward to client immediately */
  while ((n = Rio_readnb_w(&rio, (void *)buf, MAXLINE)) > 0) {
    resp_len += n;
    Rio_writen_w(connfd, buf, n);
    bzero(buf, MAXLINE);
  }

  /*
   * Write log entry to log file. Note that writting file is not thread safe
   * (Use same stdout stream(
   */
  P(&sem);
  format_log_entry(log_entry, &caddr, uri, resp_len);
  log = Fopen(LOG, "a");
  fprintf(log, "%s %d\n", log_entry, resp_len);
  fflush(stdout);
  Fclose(log);
  V(&sem);

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


