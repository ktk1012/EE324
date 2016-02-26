#ifndef GUESS_H_
#define GUESS_H_
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "msg.h"

#define INT_MAX 0xFFFFFFFF
#define USER_ID 20130185


/* check type for packet password */
enum check_t {
  NOT_RECEIVED = 0,
  RECEIVED_LT,
  RECEIVED_EQ,
  RECEIVED_GT
};


/* Check entry structure for determine delayed packet */
typedef struct checkEntry_t {
  uint32_t passwd;
  uint16_t check;
  struct checkEntry_t * next;
} checkEntry;


/* Wrapped structure of header and query for send data */
typedef struct request {
  struct header_t sndh;
  struct query_t sndq;
} req_form;


/* Wrapped structre for received data */
typedef struct response {
  struct header_t resh;
  struct response_t resp;
} rcv_form;

/* Function for get host address */
struct hostent *GetHost(char *host);

/* Functions for Check Entries handling */
void AddEntry(checkEntry* header, uint32_t passwd);
void UpdateEntry(checkEntry* header, rcv_form* rcv);

/* Function for check bogus, delayed pakcet */
int CheckPacket(req_form* req, rcv_form* rcv, checkEntry* header);
#endif  // GUESS_H_
