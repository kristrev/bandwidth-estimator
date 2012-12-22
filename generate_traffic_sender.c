#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define PACKETLEN 1500
#define PAYLOADLEN 1472 //UDP header smaller than TCP

int main(int argc, char* argv[]){
  double bandwidth = 0;
  double pkts_per_sec = 0.0;
  double desired_iat = 0;
  double iat = 0;
  double adjust = 0;
  char buffer[PAYLOADLEN];
  int seq = 0;
  int port = 0;
  int duration = 0;
  int i, rv, sockfd, b_sent;
  struct addrinfo hints, *servinfo, *p;
  time_t t0, t1;
  struct timeval t0_p, t1_p;
  struct timespec iat_sleep;

  if(argc < 5){
    printf("Must be started with ./gt <bandwidth in Mbit> <dest. ip> <dest. port> <duration (seconds)>\n");
    return 1;
  }

  bandwidth = atof(argv[1]);

  if(bandwidth == 0){
    printf("Specified bandwidth is illegal!\n");
    return 2;
  }

  duration = atoi(argv[4]);

  if(duration <= 0){
    printf("Invalid duration\n");
    return 2;
  }

  port = atoi(argv[3]);

  if(port <= 0 || port > 65535){
    printf("Given port is invalid!\n");
    return 3;
  }

  /*Does all the stuff related to getting a socket*/
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if((rv = getaddrinfo(argv[2], argv[3], &hints, &servinfo))){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 4;
  }

  for(p = servinfo; p != NULL; p = p->ai_next){
    if(sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
      break;
  }

  if(p == NULL){
    fprintf(stderr, "Failed to bind socket\n");
    return 5;
  }

  /*Med Mbit skal man gange med 1000, 1024 er binary*/
  /* Finn ut hva jeg skal dele på her for å få ønsket bw, spørs om bwm-ng gir
   litt feil. */

  /* Discovered that iperf measures on payload, I want to measure on
   actual packetsize. */

#ifdef PAYLOAD
  printf("Measure throughput\n");
  pkts_per_sec = (bandwidth * 1000 * 1000) / (double) (PAYLOADLEN * 8);
#else
  printf("Emulate throughput\n");
  pkts_per_sec = (bandwidth * 1000 * 1000) / (double) (PACKETLEN * 8);
#endif

  desired_iat = 1000000 / pkts_per_sec; //IAT is microseconds

  printf("Bandiwdth of %f Mbit/s, duration %ds\n", bandwidth, duration);
  printf("Sending %f packets/s, IAT %f microseconds\n", pkts_per_sec, desired_iat);

  for(i=0; i<PAYLOADLEN; i++)
    buffer[i] = '\0';

  t0 = time(NULL);

  gettimeofday(&t0_p, NULL);

  while(1){
    
    gettimeofday(&t1_p, NULL);

    //See notebook for order, goal is that difference should be 0. I want to find how much more/less I should
    //sleep this time to provide the desired IAT. That adjustment is desired_iat + the time it took for the last packet
    //to be processed.
    //If it it took longer than the desired IAT, the adjustment will be negative and "this" packet have to sleep less
    //If it took a shorter time than desiread IAT, this packet will have to be delayed a little bit
    adjust = desired_iat + (((t0_p.tv_sec - t1_p.tv_sec) * 1000000) + (t0_p.tv_usec - t1_p.tv_usec));
    t0_p.tv_sec = t1_p.tv_sec;
    t0_p.tv_usec = t1_p.tv_usec;

    if(adjust > 0 || iat > 0)
      iat += adjust;

    //printf("Adjust %f\n", adjust);
    sprintf(buffer, "%d", seq);
    seq = (seq + 1) % INT_MAX;
    b_sent = sendto(sockfd, buffer, sizeof(buffer), 0, p->ai_addr, p->ai_addrlen);

    if(b_sent <= 0)
      break;

    t1 = time(NULL);
    
    if(difftime(t1,t0) >= duration){
      printf("Testes done after %f\n", difftime(t1,t0));
      break;
    }

    if(iat > 0){
      usleep(iat);
    }
  }

  sprintf(buffer, "%d", -1);
  sendto(sockfd, buffer, sizeof(buffer), 0, p->ai_addr, p->ai_addrlen);

  close(sockfd);
  return 0;
}
