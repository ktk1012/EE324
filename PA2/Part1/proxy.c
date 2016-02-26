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

/* 
 * main - Main routine for the proxy program 
 */


int main(int argc, char **argv)
{
  struct sockaddr_in caddr;  // client addr
  socklen_t addr_len;  // client addr length
  int port;  // port number for proxy
  int listenfd;  // listen sockdt file descriptor
  int connfd;  // connection socket file descriptor
  int endserverfd;  // End server file descriptor

  FILE *log;  // File pointer for save log entry in proxy.log
  char log_entry[MAXLINE];  // string for log entry
  
  /* For parse uri and http version and method */
  char method[10];  // Method of HTTP
  char uri[MAXLINE];  // uri
  char http_v[20];  // version of HTTP

  /* For URI parsing */
  char d_host[MAXLINE];  // request host name
  char d_path[MAXLINE];  // path of request
  int d_port;  // port number of end host

  /* For calculating received response */
  int resp_len;  // For indicate the size of response

  /* Use for received size of rio_read */
  int n;
  
  /* rio buffer */
  rio_t rio;
  char buf[MAXLINE];

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

  log = Fopen(LOG, "a");

  while (1) {
    printf("Proxy: Listening\n");
    addr_len = sizeof(caddr);
    connfd = Accept(listenfd, (struct sockaddr *)&caddr, &addr_len);

    /* Connect start */
    printf("Proxy: Connected\n");

    /* Initialize rio for connection fd */
    Rio_readinitb(&rio, connfd);
    if((n = Rio_readlineb_w(&rio, (void *)buf, MAXLINE)) < 0) {
      printf("Proxy: bad request\n");
      close(connfd);
      exit(0);
    }

    printf("Proxy: request headr, %s", buf); 

    /* Parse method, uri, and http version */
    sscanf(buf, "%s %s %s", method, uri, http_v);

    /* Check method of request, only accept GET method */
    if (strcmp(method, "GET")) {
      printf("Proxy: Invalid method\n");
      close(connfd);
      continue;
    }

    /* Check version of HTTP, HTTP/1.0 or HTTP/1.1 is acceptable */
    if ((strcmp(http_v, "HTTP/1.0")) && (strcmp(http_v, "HTTP/1.1"))) {
      printf("Proxy: Version mismatch\n");
      close(connfd);
      continue;
    }
    
    /* Parse URI */
    if (parse_uri(uri, d_host, d_path, &d_port) < 0) {
      printf("Proxy: parse failed\n");
      close(connfd);
      continue;
    }
    

    /* Forwarding request to end server */
    endserverfd = Open_clientfd(d_host, d_port);

    /* 
     * Send first line of HTTP request header.
     * Note that the version of HTTP protocol is 1.1
     */
    printf("------- send http request ------------\n");
    sprintf(buf, "%s /%s HTTP/1.0\r\n", method, d_path);
    Rio_writen_w(endserverfd, buf, strlen(buf));
    printf("%s", buf);
    /* Send rest of request header */
    while((Rio_readlineb_w(&rio, (void *)buf, MAXLINE)) > 0) {
      /* 
       * Http request end until reacinh \r\n,
       * and we do not assumed persistant connection,
       * change the Connection status to close if exists
       */
      if (strcmp(buf, "\r\n")) {
        /* If connection is persistant, change to non persistant connection */
        if(strncmp(buf, "Connection: keep-alive", strlen("Connection: keep-alive")) == 0) {
          Rio_writen_w(endserverfd, "Connection: close\r\n",
              strlen("Connection: close\r\n"));
          printf("Connection: close\r\n");
        } else {
          printf("%s", buf);
          Rio_writen_w(endserverfd, buf, strlen(buf));
        }
      } else {
        /* Termination */
        Rio_writen_w(endserverfd, "\r\n", strlen("\r\n"));
        break;
      }
    }
    printf("------------------------------------\n");

    Rio_readinitb(&rio, endserverfd);
    /* Send to response of end server */
    resp_len = 0;
    while((n = Rio_readnb_w(&rio, (void *)buf, MAXLINE)) > 0) {
      resp_len += n;
      Rio_writen_w(connfd, buf, n);
      bzero(buf, MAXLINE);
    }
    
    /* Save log entry for proxy.log */
    format_log_entry(log_entry, &caddr, uri, resp_len);
    fprintf(log, "%s %d\n", log_entry, resp_len);
    fflush(log);
   
    /* Close connection end end server socket */
    close(endserverfd);
    close(connfd);
  }
  
  /* Close listen socket and file pointer */
  close(listenfd);
  Fclose(log);
  exit(0);
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
    printf("%s\n", temp);
    strcpy(hostbegin, temp);
    strcpy(pathname, saveptr);
    
    /* Extract port number and host name */
    temp = strtok_r(hostbegin, ":", &saveptr);
    strcpy(hostname, temp);
   
    temp = strtok_r(NULL, ":", &saveptr);
    printf("debuf: port, %s, %d\n", saveptr, strlen(saveptr));
    *port = 80;
    if (temp) {
      printf(" asdfasdfasdf\n");
      *port = atoi(temp);
    }

    printf("*----------------parse_uri---------------*\n");
    printf("hostname: %s\n", hostname);
    printf("pathname: %s\n", pathname);
    printf("port: %d\n", *port);

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


