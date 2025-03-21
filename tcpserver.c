#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9000
#define BUF_SIZE 1024

#define MAX_CLIENTS 64
#define MAX_MSG_SIZE 4096

#define PHONE_SIZE 13
#define TIME_SIZE 2
#define FIRST_PART_SIZE   35
#define SECOND_PART_SIZE  MAX_MSG_SIZE-FIRST_PART_SIZE

typedef struct client_t_ {
  struct sockaddr_in addr;
  int fd;

  uint32_t ip;
  uint16_t port;
} client_t;

typedef struct state_t_ {
  struct sockaddr_in server_addr;
  int sock;
  WSADATA wsa;

  client_t clients[MAX_CLIENTS];

  WSAEVENT events[2];

  char* recv_buf;
  char* curr_msg;

  int stop_server;
} state_t;

state_t state;

void init_clients(client_t* clients) {
  for(int i=0; i<MAX_CLIENTS; i++) clients[i].fd = -1;
}

void add_client(client_t* clients, WSAEVENT event, int fd, struct sockaddr_in addr) {
  uint32_t ip = ntohl(addr.sin_addr.s_addr);
  uint16_t port = ntohs(addr.sin_port);

  for(int i=0; i<MAX_CLIENTS; i++) {
    if(clients[i].fd == -1) {
      clients[i].fd = fd;
      clients[i].addr = addr;
      clients[i].ip = ip;
      clients[i].port = port;
      WSAEventSelect(clients[i].fd, event, FD_READ | FD_WRITE | FD_CLOSE);
    }
  }
}

bool is_client_active(client_t* clients, int idx) {
  return (clients[idx].fd != -1);
}

void del_client(client_t* clients, int idx) {
  closesocket(clients[idx].fd);
  clients[idx].fd = -1;
  memset(&clients[idx].addr, 0, sizeof(clients[idx].addr));
}

void init_events() {
  state.events[0] = WSACreateEvent();
  state.events[1] = WSACreateEvent();

  WSAEventSelect(state.sock, state.events[0], FD_ACCEPT);
}

void server_clean() {
  for(int i=0; i<MAX_CLIENTS; i++) {
    if(state.clients[i].fd != -1) {
      del_client(state.clients, i);
    }
  }
}

void handle_msg(int idx, char* recv_buf, FILE* f) {
  memset(state.recv_buf, 0, MAX_MSG_SIZE);
  memset(state.curr_msg, 0, MAX_MSG_SIZE);
  
  int client_len = sizeof(state.clients[idx].addr);
  int recv_status = recv(state.clients[idx].fd, state.recv_buf, MAX_MSG_SIZE, 0);
  
  if(strncmp(state.recv_buf, "put", 3) == 0) {
    int send_status = send(state.clients[idx].fd, "ok", 2, 0);
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
    
  // printf("Message field len: %d\n", s_len);
  if(strncmp(state.curr_msg, "stop", 4) == 0 && s_len == 4) state.stop_server = 1;

  uint32_t ip = ntohl(state.clients[client_idx].ip);
  uint16_t port = ntohs(state.clients[client_idx].port);
  // printf("From ip:port -> %u.%u.%u.%u:%u\n", ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, port);
  fprintf(f, "%u.%u.%u.%u:%u %s %s %d:%d:%d %s\n", 
          ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, 
          port,
          phone_1, phone_2, hh, mm, ss, state.curr_msg);

  // printf("Recived Message: %d %s %s %d:%d:%d %s\n", recv_idx, phone_1, phone_2, hh, mm, ss, state.curr_msg);
  fprintf(f, "%s %s %d:%d:%d %s\n", phone_1, phone_2, hh, mm, ss, state.curr_msg);
  int send_status = send(state.clients[idx].fd, "ok", 2, 0);
}

void free_vars() {
  free(state.curr_msg);
  free(state.recv_buf);
}

void fatal_err(const char* msg) {
  server_clean();
  free_vars();
  closesocket(state.sock);

  printf("%s", msg);
  exit(1);
}

int main(int argc, char** argv) {
  if(argc != 2) {
    fatal_err("Usage: ./tcpserver <PORT>\n");
  }

  WSAStartup(MAKEWORD(2, 2), &state.wsa);
  
  state.recv_buf = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.curr_msg = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);

  state.sock = socket(AF_INET, SOCK_STREAM, 0);
  if(state.sock < 0) {
    fatal_err("Failed to create socket\n");
  }

  // set socket to non-blocking mode
  unsigned long mode = 1;
  ioctlsocket(state.sock, FIONBIO, &mode);

  // init_events
  init_events();

  init_clients(state.clients);

  memset(&state.server_addr, 0, sizeof(state.server_addr));
  state.server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
  state.server_addr.sin_port = htons(atoi(argv[1]));
  state.server_addr.sin_family = AF_INET;

  if(bind(state.sock, (struct sockaddr*)&state.server_addr, sizeof(state.server_addr)) < 0) {
    fprintf(stderr, "Soket bind error...\n");
    return 1;
  }

  if(listen(state.sock, MAX_CLIENTS) < 0) {
    fatal_err("Failed to start listenning\n");
  }
  
  FILE* f = fopen("msg.txt", "w");

  // printf("Init success\n");
  while(!state.stop_server) {
    WSANETWORKEVENTS ne;
    DWORD dw = WSAWaitForMultipleEvents(2, state.events, false, 1000, false);

    WSAResetEvent(state.events[0]);
    WSAResetEvent(state.events[0]);
    
    if(WSAEnumNetworkEvents(state.sock, state.events[0], &ne) == 0 &&
        (ne.lNetworkEvents & FD_ACCEPT)) {

      struct sockaddr_in client_addr;
      int client_len = sizeof(client_addr);
      int conn = accept(state.sock, (struct sockaddr*)&client_addr, &client_len);
      add_client(state.clients, state.events[1], conn, client_addr);

      // printf("Connect client with fd = %d...\n", conn);
    }
    
    for(int i=0; i<MAX_CLIENTS; i++) {
      if(!is_client_active(state.clients, i)) continue;
    
      if(WSAEnumNetworkEvents(state.clients[i].fd, state.events[1], &ne) == 0) {
        if(ne.lNetworkEvents & FD_READ) {
          handle_msg(i, state.recv_buf, f);
        }

        if(ne.lNetworkEvents & FD_CLOSE) {
          del_client(state.clients, i);
        }
      }
    }
  }

  server_clean();
  free_vars();
  closesocket(state.sock);

  return 0;
}
