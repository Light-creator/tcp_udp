#include <netinet/in.h>
#include <stdio.h>
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

typedef struct state_t_ {
  int sock;
  struct sockaddr_in server_addr;
} state_t;

state_t state;

int main() {
  state.sock = socket(AF_INET, SOCK_DGRAM, 0);
  
  int fl = fcntl(state.sock, F_GETFL, 0);
  fcntl(state.sock, F_SETFL, fl | O_NONBLOCK);
  
  memset(&state.server_addr, 0, sizeof(state.server_addr));
  state.server_addr.sin_family = AF_INET;
  state.server_addr.sin_port = htons(8000);
  state.server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  
  if(bind(state.sock, (struct sockaddr*)&state.server_addr, sizeof(state.server_addr)) < 0)
    return 1;
  
  printf("Init success\n");
  for(;;) {
    char buf[MAX_MSG_SIZE] = {0};
    struct sockaddr_in client_addr;
    socklen_t client_size = sizeof(client_addr);
 
    int recv_satus = recvfrom(state.sock, buf, MAX_MSG_SIZE, 0, (struct sockaddr*)&client_addr, &client_size);
    if(recv_satus < 0) continue;
    buf[recv_satus] = '\0';

    uint32_t ip = ntohl(client_addr.sin_addr.s_addr);
    printf("Client addr: %u\n", ip);
    printf("Msg: %s\n", buf);
    
    int send_status = sendto(state.sock, "ok", 2, 0, (struct sockaddr*)&client_addr, client_size);
    printf("Sended: %d\n", send_status);
  }

  close(state.sock);
  
  return 0;
}
