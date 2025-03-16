/*
* MSG_NOSIGNAL - flag to not interrupt program execution, when
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_MSG_SIZE 1024

typedef struct state_t_ {
  int sock;
  struct sockaddr_in server_addr;
} state_t;
  
state_t state;

void recv_msg() {
  char buf[MAX_MSG_SIZE];

  struct timeval tv = {1, 100*1000};

  fd_set fds;
  FD_ZERO(&fds); FD_SET(state.sock, &fds);
  
  int res = select(state.sock+1, &fds, 0, 0, &tv);
  printf("res: %d\n", res);
  if(res <= 0) return;

  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int recv_status = recvfrom(state.sock, buf, MAX_MSG_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
  printf("recv_status: %d\n", recv_status);
  if(recv_status <= 0) return;

  printf("[+] Recived: %s\n", buf);
}

int main(int argc, char **argv){
  char buffer[1024];

  state.sock = socket(AF_INET, SOCK_DGRAM, 0);
  // memset(&state.server_addr, 0, sizeof(state.server_addr));

  state.server_addr.sin_family = AF_INET;
  state.server_addr.sin_port = htons(8000);
  state.server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  
  printf("Init success\n");
  strcpy(buffer, "Hello Server\n");

  for(int i=0; i<5; i++) {
    int send_status = sendto(state.sock, buffer, 1024, MSG_NOSIGNAL, (struct sockaddr*)&state.server_addr, sizeof(state.server_addr));
    printf("[+] send_status: %d\n", send_status);
    if(send_status < 0) break;
    recv_msg();
  }
  
  close(state.sock);
  return 0;
}
