/*
 * 20130185
 * Tae-kyeom, Kim
 * part3.c -- guess password concern about bogus and delayed packet
 */
#include "guess.h"



int main(int argc, char* argv[]) {
  int sockfd;
  struct hostent *hp;
  struct sockaddr_in daddr;
  /* struct sockaddr_in myaddr; by bind error,
   * do not use client-side sockaddr for binding */
  socklen_t addr_len;
  req_form sndpkt;
  rcv_form rcvpkt;  // buffer of receive packet
  int numbytes;
  /* For select */
  fd_set reads, temps;
  struct timeval timeout;
  int result;
  int cnt = 0;
  /* File saving */
  FILE *fptr;
  /* For guessing password using binary search */
  uint32_t low = 0, high = INT_MAX;
  /* check entry */
  checkEntry header;  //check entry list header for check delayed packet
  int i;
  int bogus_or_delayed;

  /* check the number of argument */
  if (argc != 3) {
    fprintf(stderr, "guess_password: ./guess_password hostname port\n");
    exit(1);
  }

  /* check port number is valid */
  if ((atoi(argv[2]) > 0xffff) || (atoi(argv[2]) < 0)) {
    fprintf(stderr, "guess_password: port number is positive 4bytes number\n");
    exit(1);
  }

  /* get host entry of server */
  hp = GetHost(argv[1]);

  /* create a socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockfd < 0) {
    perror("socket");
    exit(1);
  }

  /* 
   * Set my address for bind. Since after call bind()
   * bind: Address family not supported by protocol error occurrs
   * So i do not bind explicitly 
   *
   *
   * zero(&myaddr, sizeof(myaddr));
   * myaddr.sin_family = AF_INET;
   * myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
   * myaddr.sin_port = htons(30185);
   * 
   * if ((bind(sockfd, (struct sockaddr *)&daddr, sizeof(daddr)) == -1)) {
   * perror("bind");
   * exit(1);
   * }
   */

  /* SET sockfd to fd set */
  FD_ZERO(&reads);
  FD_SET(sockfd, &reads);

  /* initialize check entry header */
  header.next = NULL;

  /* make pakcet for send packet */
  sndpkt.sndh.magic = htonl(MAGIC);
  sndpkt.sndh.version = htons(VERSION);
  sndpkt.sndh.command = htons(command_t(REQUEST));
  sndpkt.sndq.id = htonl(USER_ID);
  sndpkt.sndq.passwd = htonl((low + high)/2);

  /* Set a destination address */
  daddr.sin_family = AF_INET;
  memcpy(&daddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  daddr.sin_port = htons(uint16_t(atoi(argv[2])));

  /* First send packet to destination */
  if ((numbytes = sendto(sockfd, (void *)&sndpkt, sizeof(sndpkt), 0,
          (struct sockaddr *)&daddr, sizeof(struct sockaddr))) == -1) {
    perror("sendto");
    exit(1);
  }
  AddEntry(&header, sndpkt.sndq.passwd);
  sleep(1);

  /* For test */
  printf("\n--------------------------------------------------\n");
  printf("**send_pkt, magic: 0x%0x, version: %d, command: %d\n",
      ntohl(sndpkt.sndh.magic), ntohs(sndpkt.sndh.version),
      ntohs(sndpkt.sndh.command));
  printf("**send_pkt, my id: %d, passwd: %u\n",
      ntohl(sndpkt.sndq.id), ntohl(sndpkt.sndq.passwd));
  printf("==================================================\n");

  /* Find my password using binary search */
  while (low <= high) {

    /* Set timeout interval */
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    /* Copy ready set to temps */
    temps = reads;

    result = select(sockfd + 1, &temps, 0, 0, &timeout);

    if (result == -1) {
      perror("select");
      exit(1);
    } else if (result == 0) {
      printf("guess_password: timeout (%d times)\n", ++cnt);

      /* If time out occurred 3times, terminate program */
      if (cnt == 3) {
        printf("guess_password: %d times timeout occurred, terminate program\n",
            cnt);
        return -1;
      }

      /* resend packet, if cnt is less than 3 */
      if ((numbytes = sendto(sockfd, (void *)&sndpkt, sizeof(sndpkt), 0,
              (struct sockaddr *)&daddr, sizeof(struct sockaddr))) == -1) {
        perror("sendto");
        exit(1);
      }
      sleep(1);
      /* For inform received packet */
      printf("\n--------------------------------------------------\n");
      printf("**send_pkt, magic: 0x%0x, version: %d, command %d\n",
          ntohl(sndpkt.sndh.magic), ntohs(sndpkt.sndh.version),
          ntohs(sndpkt.sndh.command));
      printf("**send_pkt, my id: %d, passwd: %u\n",
          ntohl(sndpkt.sndq.id), ntohl(sndpkt.sndq.passwd));
      printf("==================================================\n");
    } else {
      if (FD_ISSET(sockfd, &temps)) {
        addr_len = sizeof daddr;
        if ((numbytes = recvfrom(sockfd, (void *)&rcvpkt, sizeof(rcvpkt), 0,
                (struct sockaddr *)&daddr, &addr_len)) == -1) {
          perror("recvfrom");
          exit(1);
        }

        printf("--received %d bytes\n", numbytes);
        cnt = 0;  // reset timeout counter

        printf("--rcvd_pkt, magic: 0x%0x, version: %d, command %d\n",
            ntohl(rcvpkt.resh.magic), ntohs(rcvpkt.resh.version),
            ntohs(rcvpkt.resh.command));
        printf("--rcvd_pkt, my id: %d, passwd: %u\n",
            ntohl(rcvpkt.resp.id), ntohl(rcvpkt.resp.passwd));
        printf("--rcvd_pkt, result is %d\n", rcvpkt.resp.ret);
        printf("-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n");

        /* Check packet */
        bogus_or_delayed = CheckPacket(&sndpkt, &rcvpkt, &header);

        if (bogus_or_delayed == -1) {
          printf("guess_password: bogus packet\n");
        } else if (bogus_or_delayed == -2) {
          printf("guess_password: delayed packet\n");
        } else {
          UpdateEntry(&header, &rcvpkt);
          if (rcvpkt.resp.ret == 1) {
            printf("guess_password: real passwd < guessed passwd\n");
            high = ntohl(rcvpkt.resp.passwd) - 1;
          } else if (rcvpkt.resp.ret == 2) {
            /* Seccessfully found password, right txt file and return 0 */
            break;
          } else {
            printf("guess_password: real passwd > guessed passwd\n");
            low = ntohl(rcvpkt.resp.passwd) + 1;
          }
          /* Resend with new password and add this number in check entries */
          sndpkt.sndq.passwd = htonl((high + low)/2);
          AddEntry(&header, sndpkt.sndq.passwd);
        }

        if ((numbytes = sendto(sockfd, (void *)&sndpkt, sizeof(sndpkt), 0,
                (struct sockaddr *)&daddr, sizeof(struct sockaddr))) == -1) {
          perror("sendto");
          exit(1);
        }

        printf("\n--------------------------------------------------\n");
        printf("**send_pkt, magic: 0x%0x, version: %d, command %d\n",
            ntohl(sndpkt.sndh.magic), ntohs(sndpkt.sndh.version),
            ntohs(sndpkt.sndh.command));
        printf("**send_pkt, my id: %d, passwd: %u\n",
            ntohl(sndpkt.sndq.id), ntohl(sndpkt.sndq.passwd));
        printf("==================================================\n");
        
        sleep(1);
      }
    }
  }

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

  /* save password */
  fptr = fopen("password.txt", "w");
  if (fptr == NULL) {
    perror("File open");
    exit(1);
  }
  fprintf(fptr, "%d %u", ntohl(rcvpkt.resp.id), ntohl(rcvpkt.resp.passwd));
  fclose(fptr);
  
  /* Free all dynamically allocated checkEntry linked list */
  Free_all(&header);

  /* close client socket */
  close(sockfd);

  return 0;
}
