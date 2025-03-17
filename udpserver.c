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
#include <stdbool.h>

#define PORT 9000
#define BUF_SIZE 1024

#define MAX_CLIENTS 64
#define MAX_MSG_SIZE 4096
#define MAX_MSGS 64
#define MAX_PORTS 64

#define PHONE_SIZE 13
#define TIME_SIZE 2
#define FIRST_PART_SIZE   35
#define SECOND_PART_SIZE  MAX_MSG_SIZE-FIRST_PART_SIZE

typedef struct client_t_ {
  struct sockaddr_in addr;
  uint32_t ip;
  uint16_t port;
    
  int recived_msgs[MAX_MSGS];
  size_t addr_size;
  size_t count_recived;
} client_t;

typedef struct state_t_ {
  int socks[MAX_PORTS];
  struct sockaddr_in server_addrs[MAX_PORTS];
  int ports_count;

  fd_set r_fds;

  client_t clients[MAX_CLIENTS];

  char* recv_buf;
  char* curr_msg;

  int recived_msgs[MAX_MSGS];
  int stop_server;
} state_t;

state_t state;

/* ---------------- Clients ----------------*/
void init_clients(client_t* clients) {
  for(int i=0; i<MAX_CLIENTS; i++) {
    // clients->clients[i].fd = -1;
  }
}

void add_client(client_t* clients, struct sockaddr_in addr) {
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint16_t port = ntohs(addr.sin_port);

  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i].ip == 0) {
      clients[i].addr = addr;
      clients[i].ip = ip;
      clients[i].port = port;
      clients[i].addr_size = sizeof(clients[i].addr);
    }
  }
}

int find_clinet(struct sockaddr_in addr) {
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint16_t port = ntohs(addr.sin_port);

  int i = 0;
  for(; i<MAX_CLIENTS; i++) {
    if(state.clients[i].ip == ip && state.clients[i].port == port) 
      break;
  }

  return i;
}

bool is_client_active(client_t* clients, int idx) {
  return (clients[idx].ip != 0);
}

void del_client_idx(client_t* clients, int idx) {
  clients[idx].ip = 0;
  clients[idx].port = 0;
  memset(&clients[idx].addr, 0, sizeof(clients[idx].addr));
}

void del_client(client_t* clients, struct sockaddr_in addr) {
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint16_t port = ntohs(addr.sin_port);

  for(int i=0; i<MAX_CLIENTS; i++) {
    if(state.clients[i].ip == ip && state.clients[i].port == port) 
      del_client_idx(clients, i);
  }
}

/* ---------------- Utils ----------------*/
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

  for(int i=0; i<MAX_CLIENTS; i++) {
    del_client_idx(state.clients, i);
  }
}

void free_vars() {
  free(state.recv_buf);
  free(state.curr_msg);
}

void handle_msg(int idx, FILE* f) {
  memset(state.curr_msg, 0, MAX_MSG_SIZE);

  if(strncmp(state.recv_buf, "stop", 4) == 0) {
    clean_server();
    state.stop_server = 1;
    return;
  }
  
  char* ptr = state.recv_buf;

  uint32_t recv_idx = 0;
  memcpy(&recv_idx, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  recv_idx = htonl(recv_idx);

  char phone_1[PHONE_SIZE];
  memcpy(phone_1, ptr, sizeof(char)*PHONE_SIZE);
  ptr += PHONE_SIZE - 1;
  phone_1[PHONE_SIZE-1] = '\0';

  char phone_2[PHONE_SIZE];
  memcpy(phone_2, ptr, sizeof(char)*PHONE_SIZE);
  ptr += PHONE_SIZE - 1;
  phone_2[PHONE_SIZE-1] = '\0';
  
  uint8_t hh = *ptr++;
  uint8_t mm = *ptr++;
  uint8_t ss = *ptr++;
    
  int s_len = strlen(ptr);
  memcpy(state.curr_msg, ptr, s_len);
  ptr += s_len-1;
  *ptr = '\0';

  printf("Recived Message: %d %s %s %d:%d:%d %s\n", recv_idx, phone_1, phone_2, hh, mm, ss, state.curr_msg);
    
  fprintf(f, "%s %s %d:%d:%d %s\n", phone_1, phone_2, hh, mm, ss, state.curr_msg);
  state.recived_msgs[idx] = 1;
}

int main(int argc, char** argv) {
  if(argc < 3) {
    printf("Usage: ./udpserver [port1] [port2]");
    return 1;
  }
    
  state.recv_buf = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.curr_msg = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);

  init_sevrer(atoi(argv[1]), atoi(argv[2]));

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

        int recv_satus = recvfrom(state.socks[i], state.curr_msg, MAX_MSG_SIZE, 0, (struct sockaddr*)&client_addr, &client_size);
        if(recv_satus < 0) continue;

        // uint32_t ip = ntohl(client_addr.sin_addr.s_addr);
        // printf("Client addr: %u\n", ip);
        // printf("Msg: %s\n", buf);
        
        add_client(state.clients, client_addr);
        int recived_msg;
        memcpy(&recived_msg, state.curr_msg, sizeof(uint32_t));
        recived_msg = ntohl(recived_msg);

        int clinet_idx = find_clinet(client_addr);
        if(state.clients[clinet_idx].recived_msgs[recived_msg] == 0) {
          state.clients[clinet_idx].recived_msgs[recived_msg] = 1;
          state.clients[clinet_idx].count_recived++;
        }
        
        recived_msg = htonl(recived_msg);
        int send_status = sendto(state.socks[i], &recived_msg, sizeof(uint32_t), MSG_NOSIGNAL, 
                                 (struct sockaddr*)&client_addr, client_size);
        printf("Sended: %d\n", send_status);
      }
    }
  }

done:
  clean_server();
  free_vars();

  return 0;
}
