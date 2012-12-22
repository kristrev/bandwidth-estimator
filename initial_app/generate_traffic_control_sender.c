#include <stdint.h>

#include "gt_ctl.h"

int connect_remote(char *remote_addr, char *remote_port, int socktype, int sockfd){
  struct addrinfo hints, *info, *p;
  int rv;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;

  /*Get info for host we are connecting to*/
  if((rv = getaddrinfo(remote_addr, remote_port, &hints, &info)) != 0){
    fprintf(stderr, "Getaddrinfo (remote) failed: %s\n", gai_strerror(rv));
    return -1;
  }
  
  for(p = info; p!= NULL; p = p->ai_next){
    if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      perror("Connect on sender");
      continue;
    }

    break;
  }

  freeaddrinfo(info);
  
  if(p==NULL)
    return -1;

  return 1;
}

/* Uses TCP to initiate connection  */
int init_conn(int sockfd_tcp, char* remote_addr, char* remote_port){
  struct ctl_pkt start_pkt, rcv_pkt;
  int numbytes;
  
  memset(&start_pkt, 0, sizeof(struct ctl_pkt));
  memset(&rcv_pkt, 0, sizeof(struct ctl_pkt));
  start_pkt.ver = VERSION;
  start_pkt.msg = START_SEND;

  if(connect_remote(remote_addr, remote_port, SOCK_STREAM, sockfd_tcp) == -1){
    fprintf(stderr, "Connect failed, exiting\n");
    return 0;
  }

  printf("Connected to %s:%s\n", remote_addr, remote_port);

  if(send(sockfd_tcp, &start_pkt, sizeof(struct ctl_pkt), 0) == -1){
    perror("Send");
    close(sockfd_tcp);
    return 0;
  }

  if((numbytes = recv(sockfd_tcp, &rcv_pkt, sizeof(struct ctl_pkt), 0)) <= 0){
    printf("Connection closed by other party/failed. Aborting\n");
    close(sockfd_tcp);
    return 0;
  }

  if(rcv_pkt.msg == OK_SEND)
    return 1;
  else
    return 0;
}

/* This function gets me the correct sockaddr-struct  */
int init_udp(char* remote_addr, char* remote_port, struct addrinfo *udp){
  struct addrinfo hints, *p, *servinfo;
  int rv;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if((rv = getaddrinfo(remote_addr, remote_port, &hints, &servinfo))){
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 4;
  }

  for(p = servinfo; p != NULL; p = p->ai_next){
    if(p)
      break;
  }

  if(p == NULL){
    fprintf(stderr, "Failed to bind socket\n");
    return 0;
  }

  memcpy(udp, p, sizeof(struct addrinfo));
  return 1;
}

void send_udp(int udp_sock, struct addrinfo *udp_addr, int duration, double bandwidth, int payload_len){
  struct timeval t0_p, t1_p;
  time_t t0, t1;
  double pkts_per_sec = 0.0;
  double desired_iat = 0.0;
  double iat = 0.0;
  double adjust = 0.0;
  char buffer[PAYLOADLEN];
  unsigned int seq = 0;
  uint64_t tot_numbytes = 0;
    int numbytes;
  int i;

  /*Med Mbit skal man gange med 1000, 1024 er binary*/
  pkts_per_sec = (bandwidth * 1000 * 1000) / (double) (payload_len * 8);
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

    sprintf(buffer, "%u", seq);
    seq = (seq + 1) % UINT_MAX;
    numbytes = sendto(udp_sock, buffer, payload_len, 0, udp_addr->ai_addr, udp_addr->ai_addrlen);

    if(numbytes <= 0){
      printf("Socket closed unexpectedly\n");
      break;
    }

	tot_numbytes += numbytes;
    t1 = time(NULL);
    
    if(difftime(t1,t0) > duration){
      printf("Testes done after %f. Sent %u byte data\n", difftime(t1,t0), tot_numbytes);
      break;
    }

    if(iat > 0){
      usleep(iat);
    }
  }
}

void end_conn(int sockfd_tcp){
  struct ctl_pkt end_pkt, rcv_pkt;
  int numbytes;

  memset(&end_pkt, 0, sizeof(struct ctl_pkt));
  end_pkt.ver = VERSION;
  end_pkt.msg = END_SEND;


  send(sockfd_tcp, &end_pkt, sizeof(struct ctl_pkt), 0);
}

int main(int argc, char *argv[]){

  struct addrinfo udp_addr;
  int port, duration, sockfd_tcp, sockfd_udp;
  int rv, numbytes;
  double bandwidth;
  char udp_port[strlen("65535") + 1];
  int payload_len = PAYLOADLEN;

  if (argc < 6){
    printf("Must be started with: ./gt_ctl <src. ip> <dst. ip> <dst. port> <duration in sec> <bandwidth in Mbit> <payload length>\n");
    return 1;
  }

  port = atoi(argv[3]);

  if(port <= 0 || port > 65535){
    printf("Port must be 0<port<65535\n");
    return 1;
  }

  duration = atoi(argv[4]);

  if(duration <= 0){
    printf("Invalid duration, must be greater than zero\n");
    return 1;
  }

  bandwidth = atof(argv[5]);

  if(bandwidth <= 0){
    printf("Invalid duration, must be greater than zero\n");
    return 1;
  }
  
  payload_len = atoi(argv[6]);

  if((sockfd_tcp = bind_local(argv[1], NULL, SOCK_STREAM)) == -1){
    fprintf(stderr, "Exiting application\n");
    return 1;
  }

  if(init_conn(sockfd_tcp, argv[2], argv[3]) == 0){
    printf("Could not initialize connection\n");
    close(sockfd_tcp);
    return 1;
  }

  sprintf(udp_port, "%d", port+1);
  sockfd_udp = bind_local(argv[1], NULL, SOCK_DGRAM);

  if(sockfd_udp > 0){
    printf("Ready to initialize UDP\n");
    if(init_udp(argv[2], udp_port, &udp_addr)){
      printf("Initialized UDP and ready to send. TCP port: %d UDP Port: %d\n", sockfd_tcp, sockfd_udp);
      send_udp(sockfd_udp, &udp_addr, duration, bandwidth, payload_len);
      close(sockfd_udp);
    } else
      printf("Could not initiate udp, aborting\n");
  }

  end_conn(sockfd_tcp);
  close(sockfd_tcp);
  return 0;
}
