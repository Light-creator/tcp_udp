#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_MSG_SIZE 1024
#define MAX_PORTS 64

typedef struct state_t_ {
  int socks[MAX_PORTS];
  struct sockaddr_in server_addrs[MAX_PORTS];
  int ports_count;

  fd_set r_fds;
} state_t;

state_t state;

void init_sevrer(int start_port, int end_port) {
  state.ports_count = end_port-start_port;
  for(int i=0; i<state.ports_count; i++) {
    int port = start_port + i;
    
    state.socks[i] = socket(AF_INET, SOCK_DGRAM, 0);
    int fl = fcntl(state.socks[i], F_GETFL, 0);
    fcntl(state.socks[i], F_SETFL, fl | O_NONBLOCK);
    
    memset(&state.server_addrs[i], 0, sizeof(state.server_addrs[i]));
    state.server_addrs[i].sin_family = AF_INET;
    state.server_addrs[i].sin_port = htons(port);
    state.server_addrs[i].sin_addr.s_addr = htonl(INADDR_ANY);
  
    if(bind(state.socks[i], (struct sockaddr*)&state.server_addrs[i],  sizeof(state.server_addrs[i])) < 0)
      exit(1);
  }
}

void clean_server() {
  for(int i=0; i<state.ports_count; i++) {
    close(state.socks[i]);
  }
}

int main() {
  init_sevrer(8000, 8005);

  printf("Init success\n");
  for(;;) {
    FD_ZERO(&state.r_fds);

    int maxi = 0;
    for(int i=0; i<state.ports_count; i++) {
      FD_SET(state.socks[i], &state.r_fds);
      if(maxi < state.socks[i]) maxi = state.socks[i];
    }
    
    int select_status = select(maxi+1, &state.r_fds, 0, 0, 0);
    if(select_status < 0) continue;

    for(int i=0; i<state.ports_count; i++) {
      if(FD_ISSET(state.socks[i], &state.r_fds)) {
        char buf[MAX_MSG_SIZE] = {0};
        struct sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);

        int recv_satus = recvfrom(state.socks[i], buf, MAX_MSG_SIZE, 0, (struct sockaddr*)&client_addr, &client_size);
        if(recv_satus < 0) continue;

        uint32_t ip = ntohl(client_addr.sin_addr.s_addr);
        printf("Client addr: %u\n", ip);
        printf("Msg: %s\n", buf);

        int send_status = sendto(state.socks[i], "ok", 2, 0, (struct sockaddr*)&client_addr, client_size);
        printf("Sended: %d\n", send_status);
      }
    }
  }

  clean_server();
  
  return 0;
}
