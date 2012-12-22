#include "gt_ctl.h"

char *parse_msg(char msg){
  switch(msg){
  case START_SEND:
    return "START_SEND";
    break;
  case OK_SEND:
    return "OK_SEND";
    break;
  case FAILED_SEND:
    return "FAILED_SEND";
    break;
  case END_SEND:
    return "END_SEND";
    break;
  }
  
  return "<unknown>";
}

void print_pkt(struct ctl_pkt pkt){
  printf("Version is %d\n", pkt.ver);
  printf("Command is %s\n", parse_msg(pkt.msg));
}

int bind_local(char *local_addr, char *local_port, int socktype){
  struct addrinfo hints, *info, *p;
  int sockfd;
  int rv;
  int yes=1;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  
  if((rv = getaddrinfo(local_addr, local_port, &hints, &info)) != 0){
    fprintf(stderr, "Getaddrinfo (local) failed: %s\n", gai_strerror(rv));
    return -1;
  }
  
  for(p = info; p != NULL; p = p->ai_next){
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
      perror("Socket:");
      continue;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      close(sockfd);
      perror("Setsockopt");
      continue;
    }

    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      perror("Bind (local)");
      continue;
    }

    break;
  }

  if(p == NULL){
    fprintf(stderr, "Local bind failed\n");
    freeaddrinfo(info);  
    return -1;
  }

  freeaddrinfo(info);

  return sockfd;
}

int ctl_send_ok(int ctl_sock){
  struct ctl_pkt ok_pkt;

  memset(&ok_pkt, 0, sizeof(struct ctl_pkt));
  ok_pkt.ver = VERSION;
  ok_pkt.msg = OK_SEND;

  if(send(ctl_sock, &ok_pkt, sizeof(struct ctl_pkt), 0) < 0){
    printf("Failed to send OK-packet\n");
    return 0;
  }else{
    return 1;
  }
}

void ctl_send_failed(int ctl_sock){
  struct ctl_pkt fail;
  
  memset(&fail, 0, sizeof(struct ctl_pkt));
  fail.ver = VERSION;
  fail.msg = FAILED_SEND;
  send(ctl_sock, &fail, sizeof(struct ctl_pkt), 0);
}

