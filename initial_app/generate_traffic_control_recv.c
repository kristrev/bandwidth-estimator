#include <sys/select.h>
#include <stdint.h>
#include <sys/time.h>
#include "gt_ctl.h"

#define BACKLOG 0 //Currently, no support for more than one connection

/*Stolen from beej*/
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* Returns control_sock or 0 on failure. Control_sock will later be passed to do_ctl_end  */
int do_ctl_start(int listen_sock, char *local_addr, char *local_port){
  int control_sock;
  struct sockaddr_storage sender_addr; //Information about connection
  socklen_t sin_size;
  char sender_ip[INET6_ADDRSTRLEN];
  struct ctl_pkt rcv_pkt;
  int numbytes;
  memset(&rcv_pkt, 0, sizeof(struct ctl_pkt));

  if(listen(listen_sock, BACKLOG) == -1){
    perror("Listen failed");
    return -1;
  }

  printf("Waiting for connections, IP: %s Port: %s\n", local_addr, local_port);

  sin_size = sizeof(sender_addr);
  control_sock = accept(listen_sock, (struct sockaddr *) &sender_addr, &sin_size);

  if(control_sock == -1){
    perror("Accept");
    return 0;
  }
  
  inet_ntop(sender_addr.ss_family, get_in_addr((struct sockaddr*) &sender_addr), sender_ip, sizeof(sender_ip));
  printf("Sender %s connected\n", sender_ip);
  
  if((numbytes = recv(control_sock, &rcv_pkt, sizeof(struct ctl_pkt), 0)) <= 0){
    printf("Connection closed by other party/failed. Aborting\n");
    close(control_sock);
    return 0;
  }
  
  printf("Received %d bytes\n", numbytes);
  
  if(rcv_pkt.msg == START_SEND){
    printf("Will go on to configure UDP\n");
    return control_sock;
  } else
    printf("Did not received START_SEND as first packet! Closing\n");

  close(control_sock);

  return 0;
}

int configure_udp(char *local_addr, char *local_port){
  int udp_sock;

  udp_sock = bind_local(local_addr, local_port, SOCK_DGRAM);
  return udp_sock;
}

/* Function that receives data for both UDP and TCP (end_packet)*/
int recv_data(int ctl_sock, int udp_sock, char *prefix, int num_test){
  //Idea is: If TCP and END_SEND: Close UDP, close file and stop
  fd_set master;
  fd_set read_fds;
  int fdmax, i, numbytes;
  struct ctl_pkt rcv_pkt;
  char buffer[PAYLOADLEN];
  char seq_nu[strlen("4294967294") + 2]; //\n + \0
  char filename[255];
  FILE* seq_nu_file;
  uint64_t tot_numbytes = 0;
  struct timeval t_start, t_end;
  uint32_t tdiff;

  sprintf(filename, "%s%d.txt", prefix, num_test);
  seq_nu_file = fopen(filename, "w");

  if(seq_nu_file == NULL){
    printf("Failed to open file\n");
    return 0;
  }

  for(i=0; i<PAYLOADLEN; i++)
    buffer[i] = '\0';

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  FD_SET(ctl_sock, &master);
  FD_SET(udp_sock, &master);

  fdmax = ctl_sock > udp_sock ? ctl_sock : udp_sock;

  while(1){
    read_fds = master;

    if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1){
      perror("Select");
      break;
    }

    for(i = 0; i<=fdmax; i++){
      if(FD_ISSET(i, &read_fds)){
	if(i == ctl_sock){
	  if((numbytes = recv(i, &rcv_pkt, sizeof(struct ctl_pkt), 0)) <= 0){
	    printf("Connection closed by other party/failed. Aborting\n");
	    fclose(seq_nu_file);
	    return 0;
	  }

	  if(rcv_pkt.msg == END_SEND){
	  	/* Not entirely accurate as there is a delay here, before END_SEND is sent */
	  	tdiff = (uint32_t) (((t_end.tv_sec - t_start.tv_sec) * 1e6) + (t_end.tv_usec - t_start.tv_usec));
	  	
	  	printf("Transfer laster for %.2f sec. Received %llu bytes. Estimated incoming bandwidth: %.2f Mbit/s\n", tdiff/1e6, tot_numbytes, ((tot_numbytes / 1e6) * 8) / (tdiff/1e6));
	    printf("Received END_SEND, closing connections\n");
	    fclose(seq_nu_file);
	    return 1;
	  }
	  
	} else if(i == udp_sock){
	  numbytes = recvfrom(i, buffer, sizeof(buffer), 0, NULL, NULL);

	  if(numbytes > 0){
	  	if(tot_numbytes == 0)
		  		gettimeofday(&t_start, NULL);

	  	gettimeofday(&t_end, NULL);	  	
		tot_numbytes += numbytes;
	    sprintf(seq_nu, "%u %llu\n", (unsigned int) atol(buffer), (uint64_t) ((t_end.tv_sec * 1e6) + t_end.tv_usec));
	    fwrite(seq_nu, 1, strlen(seq_nu), seq_nu_file);
	  } else {
	    printf("UDP connection closed unexpectedly\n");
	    fclose(seq_nu_file);
	    return 0;
	  }
	}
      }
    }
  }

  return 0;
}

int main(int argc, char *argv[]){
  int port, control_sock, listen_sock, udp_sock;
  char udp_port[strlen("65535") + 1];
  int num_tests = 0;

  if(argc < 4){
    printf("Program must be started with ./gt_ctl_recv <stc. ip> <src.port> <output prefix>\n");
    return -1;
  }

  port = atoi(argv[2]);

  /*UDP gets port +1, so limit is one lower than SHORT_MAX*/
  if(port <= 0 || port > 65534){
    printf("Port must be 0<port<65534\n");
    return -1;
  }

  listen_sock = bind_local(argv[1], argv[2], SOCK_STREAM);

  if(listen_sock < 0){
    printf("Could not get listen sock, aborting\n");
    return -1;
  }

  while(1){
    if((control_sock = do_ctl_start(listen_sock, argv[1], argv[2])) == 0)
      continue;
    else{
      sprintf(udp_port, "%d", port+1);
      udp_sock = configure_udp(argv[1], udp_port);
      if(udp_sock){
	printf("Ready to receive data\n");
	//The UDP socket is ready, so the worst thing that can happen
	//is that there are some packets ready for receiving when
	//recv_data is called.
	ctl_send_ok(control_sock);
	printf("Control sock: %d UDP sock: %d\n", control_sock, udp_sock);	

	if(recv_data(control_sock, udp_sock, argv[3], num_tests))
	  num_tests +=1;

	close(udp_sock);
      }else{
	ctl_send_failed(control_sock);
      }
    }

    close(control_sock);

  }

  return 0;
}
