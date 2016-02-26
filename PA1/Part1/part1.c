/*
 * 20130185
 * Tae-kyeom, Kim
 * part1.c -- guess password regardless of delayed or bogus packet
 * For part1 in assignment1
 */

#include "guess.h"

int main(int argc, char* argv[]){
  int sockfd;
  struct hostent *hp;
  struct sockaddr_in daddr;
  socklen_t addr_len;
  req_form sndpkt;
  rcv_form rcvpkt;
  int numbytes;
  FILE *fptr;
  uint32_t low = 0, high = INT_MAX;

  /* Check the number of argument */
  if (argc !=3) {
    fprintf(stderr, "guess_password: ./guess_password hostname port\n");
    exit(1);
  }

  /* Check port number is valid */
  if ((atoi(argv[2]) > 0xffff) || (atoi(argv[2]) < 0)) {
    fprintf(stderr, "guess_password: port number is positive 4bytes number\n");
    exit(1);
  }

  /* Get host entry of destination */
  hp = GetHost(argv[1]);

  /* Create client socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    perror("socket");
    exit(1);
  }

  /* Make packet for send packet form sndpkt */
  sndpkt.sndh.magic = htonl(MAGIC);
  sndpkt.sndh.version = htons(VERSION);
  sndpkt.sndh.command = htons(command_t(REQUEST));
  sndpkt.sndq.id = htonl(USER_ID);

  /* Set a destination address */
  daddr.sin_family = AF_INET;
  memcpy(&daddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  daddr.sin_port = htons(uint16_t(atoi(argv[2])));

  /* Guess password using binary search */
  while (low <= high) {
    /* Set updated password */
    sndpkt.sndq.passwd = htonl((low + high)/2);

    /* Send packet */
    if ((numbytes = sendto(sockfd, (void *)&sndpkt, sizeof(sndpkt), 0,
            (struct sockaddr *)&daddr, sizeof(struct sockaddr))) == -1) {
      perror("sendto");
      exit(1);
    }

    /* For test */
    printf("\n--------------------------------------------------\n");
    printf("**send_pkt, magic: 0x%0x, version: %d, command: %d\n",
        ntohl(sndpkt.sndh.magic), ntohs(sndpkt.sndh.version),
        ntohs(sndpkt.sndh.command));
    printf("**send_pkt, my id: %d, passwd: %u\n",
        ntohl(sndpkt.sndq.id), ntohl(sndpkt.sndq.passwd));
    printf("==================================================\n");

    /* Sleep for 1second */
    sleep(1);

    /* Blocking for receiving packet */
    addr_len = sizeof daddr;
    if ((numbytes = recvfrom(sockfd, (void *)&rcvpkt, sizeof(rcvpkt), 0,
            (struct sockaddr *)&daddr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }
    
    /* Recevied packet */
    printf("--received %d bytes\n", numbytes);
    printf("--rcvd_pkt, magic: 0x%0x, version: %d, command: %d\n",
        ntohl(rcvpkt.resh.magic), ntohs(rcvpkt.resh.version),
        ntohs(rcvpkt.resh.command));
    printf("--rcvd_pkt, my_id: %d, passwd: %u\n",
        ntohl(rcvpkt.resp.id), ntohl(rcvpkt.resp.passwd));
    printf("--rcvd_pkt, result is %d\n", rcvpkt.resp.ret);

    /* Check ret entry and adjust password */
    if (rcvpkt.resp.ret == 1) {
      printf("guess_passowrd: real passwd < guess passwd\n");
      high = ntohl(rcvpkt.resp.passwd) - 1;
    } else if (rcvpkt.resp.ret == 2) {
      break;
    } else {
      printf("guess_password: real passwd > guess passwd\n");
      low = ntohl(rcvpkt.resp.passwd) + 1;
    }
  }

  /* Correctly received packet, save and terminate program */
  printf("\n*****************Correct password*****************\n");
  printf("guess_password: received from %s: %d\n",
      inet_ntoa(daddr.sin_addr), ntohs(daddr.sin_port));
  printf("guess_password: received %d bytes\n", numbytes);
  printf("my query, magic: 0x%0x, version: %d, command %d\n",
      ntohl(rcvpkt.resh.magic), ntohs(rcvpkt.resh.version),
      ntohs(rcvpkt.resh.command));
  printf("my id: %d, passwd: %u\n",
      ntohl(rcvpkt.resp.id), ntohl(rcvpkt.resp.passwd));
  printf("result is %d\n", rcvpkt.resp.ret);

  /* Save password */
  fptr = fopen("password.txt", "w");
  if (fptr == NULL) {
    perror("File open");
    exit(1);
  }
  fprintf(fptr, "%d %d", ntohl(rcvpkt.resp.id), ntohl(rcvpkt.resp.passwd));
  fclose(fptr);

  /* close client socket */

  return 0;
}
