#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>

#define VERSION 1
#define PACKETLEN 1500
#define PAYLOADLEN 1472

enum{
  START_SEND = 0,
  OK_SEND,
  FAILED_SEND,
  END_SEND,
};

struct ctl_pkt{
  char ver;
  char msg;
};
