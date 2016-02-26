/*
 * 20130185
 * Tae-kyeom, Kim
 * Functions for part1 and part2 assginment
 */

#include "guess.h"


/*
 * Get host address entry.
 * argument can host name or dottes decimal number.
 * It returns pointer to host entry, NULL if error occurrs
 */
struct hostent *GetHost(char *host) {
  struct in_addr addr;
  struct hostent *hp;
  if (inet_aton(host, &addr) != 0) {
    hp = gethostbyaddr((const char *)&addr, sizeof(addr), AF_INET);
    if (hp == NULL) {
      fprintf(stderr, "GetHosst: DNS error in gethostbyaddr, %s\n",
          hstrerror(h_errno));
      exit(1);
    }
  } else {
    hp = gethostbyname(host);
    if (hp == NULL) {
      fprintf(stderr, "GetHost: DNS error in gethostbyname, %s\n",
          hstrerror(h_errno));
      exit(1);
    }
  }
  return hp;
}

/*
 * Add new password entry that just sent
 * It uses after sendto() function, 
 * check field is set to NOT_RECEIVED.
 * Entry is linked list, and every new packet linked to 
 * after the header.
 */
void AddEntry(checkEntry* header, uint32_t passwd) {
  checkEntry *temp;
  temp = (checkEntry *)malloc(sizeof(checkEntry));
  temp->passwd = passwd;
  temp->check = check_t(NOT_RECEIVED);
  if (header->next == NULL) {
    temp->next = NULL;
    header->next = temp;
  } else {
    temp->next = header->next;
    header->next = temp;
  }
}

/* 
 * Use after recvfrom() function. If received packet is not wrong,
 * i.e neither delayed or bogus, It updates check field corresponding
 * received password.
 */
void UpdateEntry(checkEntry* header, rcv_form* rcv) {
  checkEntry *temp;
  for (temp=header->next; temp != NULL; temp=temp->next) {
    if (temp->passwd == rcv->resp.passwd) {
      temp->check = check_t(rcv->resp.ret);
      break;
    }
  }
}

/*
 * Check packet is bogus or delayed.
 * if return -2 it means delayed packet, -1 means bogus packet
 * else return 1
 */
int CheckPacket(req_form* req, rcv_form* rcv, checkEntry* header) {
  checkEntry *temp;
  /* Compare between recv & sent packet */
  if (req->sndh.magic != rcv->resh.magic)
    return -1;
  if (req->sndh.version != rcv->resh.version)
    return -1;
  if (ntohs(rcv->resh.command) != 1)
    return -1;
  if (req->sndq.id != rcv->resp.id)
    return -1;
  
  /* Compare between sent packets before and received packet */
  for (temp=header->next; temp != NULL; temp=temp->next) {
    if (temp->passwd == rcv->resp.passwd) {
      /* Normal packet */
      if (temp->check == 0)
        return 1;
      /* Delayed packet (It has been received) */
      else if (temp->check == rcv->resp.ret)
        return -2;
      /* Same password pair but wrong response result */
      else
        return -1;
    }
  }
  
  if (req->sndq.passwd != rcv->resp.passwd)
    return -1;
}
