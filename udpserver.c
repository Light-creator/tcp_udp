#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#define PORT 9000
#define BUF_SIZE 1024

#define MAX_CLIENTS 64
#define MAX_MSG_SIZE 65535
#define MAX_MSGS 21
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
  int recived_stop;
} state_t;

state_t state;

/* ---------------- Clients ----------------*/
void add_client(client_t* clients, struct sockaddr_in addr) {
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint16_t port = ntohs(addr.sin_port);

  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i].ip == 0) {
      clients[i].addr = addr;
      clients[i].ip = ip;
      clients[i].port = port;
      clients[i].addr_size = sizeof(clients[i].addr);
			memset(clients[i].recived_msgs, -1, sizeof(int)*MAX_MSGS);
			break;
    }
  }
}

int find_client(struct sockaddr_in addr) {
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
  clients[idx].count_recived = 0;
  memset(&clients[idx].addr, 0, sizeof(clients[idx].addr));
  memset(&clients[idx].recived_msgs, -1, sizeof(int)*MAX_MSGS);
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
  state.ports_count = end_port-start_port+1;
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

void handle_msg(FILE* f, int client_idx) {
  memset(state.curr_msg, 0, MAX_MSG_SIZE);

  char* ptr = state.recv_buf;
  if(strncmp(ptr, "put", 3) == 0) return;

  uint32_t recv_idx = 0;
  memcpy(&recv_idx, ptr, sizeof(uint32_t));
  ptr += sizeof(uint32_t);
  recv_idx = ntohl(recv_idx);
	uint32_t arr_idx = recv_idx % MAX_MSGS;
  
  /* printf("[+] Recived: %u | %u\n", recv_idx, arr_idx); */

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
  ptr += s_len;
  *ptr = '\0';
  
  if(strncmp(state.curr_msg, "stop", 4) == 0 && s_len == 4) state.stop_server = 1;
  
	/* printf("Recived Message: %u %s %s %02hhu:%02hhu:%02hhu %s\n", recv_idx, phone_1, phone_2, hh, mm, ss, state.curr_msg); */
    
  if(state.clients[client_idx].recived_msgs[arr_idx] == -1) {
    uint32_t ip = ntohl(state.clients[client_idx].ip);
    uint16_t port = ntohs(state.clients[client_idx].port);
		/* printf("From ip:port -> %u.%u.%u.%u:%u\n", ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, port); */
    fprintf(f, "%u.%u.%u.%u:%u %s %s %02hhu:%02hhu:%02hhu %s\n", 
            ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, 
            port,
            phone_1, phone_2, hh, mm, ss, state.curr_msg);

    state.clients[client_idx].recived_msgs[arr_idx] = recv_idx;
    state.clients[client_idx].count_recived++;
  }
}

void fatal_err(const char* msg) {
  clean_server();
  free_vars();

  /* printf("%s", msg); */
  exit(1);
}

int main(int argc, char** argv) {
  if(argc < 3) {
    fatal_err("Usage: ./udpserver [port1] [port2]");
  }
    
  state.recv_buf = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.curr_msg = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);

  FILE* f = fopen("msg.txt", "w");
  if(f == NULL) {
    fatal_err("Failed to open file\n");
  }

  init_sevrer(atoi(argv[1]), atoi(argv[2]));
  
	/* printf("Init success\n"); */
  while(!state.stop_server) {
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

        int recv_satus = recvfrom(state.socks[i], state.recv_buf, MAX_MSG_SIZE, 0, 
                                  (struct sockaddr*)&client_addr, &client_size);
        if(recv_satus < 0) continue;
        
        int client_idx = find_client(client_addr);
        if(client_idx == MAX_CLIENTS) add_client(state.clients, client_addr);

        client_idx = find_client(client_addr);
        
        handle_msg(f, client_idx);
        
        uint32_t msgs_to_send[MAX_MSGS];
        int j = 0;
        for(int i=0; i<MAX_MSGS; i++) {
          uint32_t maxi = MAX_MSGS;
          if(state.clients[client_idx].recived_msgs[i] != -1) {
            if(maxi < state.clients[client_idx].recived_msgs[i]) maxi = state.clients[client_idx].recived_msgs[i];
            msgs_to_send[j++] = htonl(state.clients[client_idx].recived_msgs[i]);
            /* printf("%u ", state.clients[client_idx].recived_msgs[i]); */
          }
        }
        /* printf("\n"); */

        while(j < MAX_MSGS) msgs_to_send[j++] = maxi;
        
        int send_status = sendto(state.socks[i], msgs_to_send, sizeof(uint32_t)*MAX_MSGS, MSG_NOSIGNAL, 
                                 (struct sockaddr*)&client_addr, client_size);
      }
    }
  }

  clean_server();
  free_vars();
  fclose(f);

  return 0;
}
