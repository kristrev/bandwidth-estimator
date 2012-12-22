#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define PACKETLEN 1500
#define PAYLOADLEN 1448

int main(int argc, char* argv[]){
  double pkts_per_sec = 0.0;
  double iat = 0;
  char filename[strlen("testfil_.txt") + strlen("4294967294") + 1];
  char addr[INET_ADDRSTRLEN];
  char buffer[PAYLOADLEN];
  char seq_nu[strlen("4294967294") + 2]; //\n + \0
  int port = 0;
  int duration = 0;
  int i, rv, sockfd, b_recv;
  struct addrinfo hints, *servinfo, *p;
  FILE* seq_nu_file;
  time_t t0, t1;
  int num_test = 0;

  if(argc < 4){
    printf("Must be started with ./gt_recv <dest. ip> <dest. port> <duration>\n");
    return 1;
  }

  port = atoi(argv[2]);

  if(port <= 0 || port > 65535){
    printf("Given port is invalid!\n");
    return 3;
  }

  duration = atoi(argv[3]);
  
  if(duration <= 0){
    printf("Invalid duration\n");
    return 2;
  }

  /*Does all the stuff related to getting a socket*/
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  if((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo))){
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

  inet_ntop(p->ai_family, &(((struct sockaddr_in *)p->ai_addr)->sin_addr), addr, sizeof addr);

  if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
    perror("bind:");
    return -1;
  }

  printf("Will receive packets on IP %s, port %d, duration %ds\n", addr, port, duration);

  while(1){
    printf("Will begin test no. %d\n", num_test);
    sprintf(filename, "testfile_%d.txt", num_test);
    seq_nu_file = fopen(filename, "w");

    if(seq_nu_file == NULL){
      printf("Could not open %s\n", filename);
      return 2;
    }

    num_test += 1;
    
    for(i=0; i<PAYLOADLEN; i++)
      buffer[i] = '\0';
    
    /* Hack to avoid stressing the CPU 100% all the time  */
    b_recv = recvfrom(sockfd, buffer, sizeof(buffer), 0, p->ai_addr, &(p->ai_addrlen));
    t0 = time(NULL);
    
    while(1){
      if(b_recv >= 0){
	if(atol(buffer) == -1)
	  break;
	sprintf(seq_nu, "%u\n", (unsigned int) atol(buffer));
	fwrite(seq_nu, 1, strlen(seq_nu), seq_nu_file);
      }
      
      t1 = time(NULL);
      
      if(difftime(t1,t0) >= duration){
	printf("Testes aborted after done after %f\n", difftime(t1,t0));
	break;
      }
      
      b_recv = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT, p->ai_addr, &(p->ai_addrlen));
      
    }

    fclose(seq_nu_file);
  }

  close(sockfd);

  return 0;
}
