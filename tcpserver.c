#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9000
#define BUF_SIZE 1024

#define MAX_CLIENTS 64
#define MAX_MSG_SIZE 65535
#define MAX_BUF_SIZE MAX_MSG_SIZE*6

#define PHONE_SIZE 13
#define TIME_SIZE 2
#define FIRST_PART_SIZE   35
#define SECOND_PART_SIZE  MAX_MSG_SIZE-FIRST_PART_SIZE

#define log(msg) printf("[+] %s\n", msg)

typedef struct client_t_ {
  struct sockaddr_in addr;
  int fd;

  uint32_t ip;
  uint16_t port;
  bool flag_put;

  char* recv_buf;
  char* ptr;
  uint32_t recived;
  uint32_t c_idx;
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
  for(int i=0; i<MAX_CLIENTS; i++) state.clients[i].fd = -1;
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

      clients[i].recv_buf = (char*)malloc(sizeof(char)*MAX_BUF_SIZE);
      memset(clients[i].recv_buf, 0, MAX_BUF_SIZE);
      clients[i].ptr = clients[i].recv_buf;

      WSAEventSelect(clients[i].fd, event, FD_READ | FD_ACCEPT | FD_CLOSE);
      break;
    }
  }
}

int is_client_active(client_t* clients, int idx) {
  return (clients[idx].fd != -1);
}

void del_client(client_t* clients, int idx) {
  closesocket(state.clients[idx].fd);
  clients[idx].fd = -1;
  clients[idx].flag_put = false;
  clients[idx].recived = 0;
  clients[idx].c_idx = 0;

  free(clients[idx].recv_buf);

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

void hex_dump(char* buf, int len) {
	for(int i=0; i<len; i++) printf("%x ", buf[i] & 0xff);
	printf("\n");
}

void handle_msgs(int idx, FILE* f) {
  if(state.clients[idx].recived - state.clients[idx].c_idx < 3) return;
  
  // printf("Client with index: %d\n", idx);
  if(!state.clients[idx].flag_put) {
    // log("Skip put msg");
    state.clients[idx].ptr += 3;
    state.clients[idx].c_idx += 3;
    state.clients[idx].flag_put = true;
  }
 
  char* ptr = state.clients[idx].ptr;
  // hex_dump(ptr, 128); 

  int c_idx = state.clients[idx].c_idx;
  
  if(state.clients[idx].recived - c_idx < 32) return;

  char* to_file_buf[128];
  int msg_idx = 0;

  while(c_idx < state.clients[idx].recived) {
    c_idx += 4;
    ptr += 4;
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

    uint32_t ip = ntohl(state.clients[idx].ip);
    uint16_t port = ntohs(state.clients[idx].port);
    
    char* msg = ptr;
      
    // Last byte of the message should be 0
    c_idx += 27;
    while(c_idx < state.clients[idx].recived && state.clients[idx].recv_buf[c_idx] != '\0') {
      c_idx++;
      ptr++;
    }
  
    if(c_idx >= state.clients[idx].recived) break;
      
    int send_status = send(state.clients[idx].fd, "ok", 2, 0);
    printf("send_status: %d\n", send_status);
    if (send_status == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error == WSAECONNRESET) {
        printf("[!] Клиент %d разорвал соединение (RST)\n", idx);
      } else {
        printf("[!] Ошибка отправки: %d\n", error);
      }
    }

    if(send_status < 0) {
      for(int i=0; i<msg_idx; i++) free(to_file_buf[i]);
      return;
    }

    // fprintf(f, "%u.%u.%u.%u:%u %s %s %02hhu:%02hhu:%02hhu %s\n", 
    //     ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, 
    //     port,
    //     phone_1, phone_2, hh, mm, ss, msg);
    to_file_buf[msg_idx] = (char*)malloc(sizeof(char)*MAX_BUF_SIZE);
    sprintf(to_file_buf[msg_idx], "%u.%u.%u.%u:%u %s %s %02hhu:%02hhu:%02hhu %s\n", 
        ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, 
        port,
        phone_1, phone_2, hh, mm, ss, msg);

    // to_f_ptr += shift + 27;

    if(strncmp(msg, "stop", 4) == 0 && *(msg+4) == '\0') state.stop_server = 1;
      
    ptr++;
    c_idx++;

    state.clients[idx].ptr = ptr;
    state.clients[idx].c_idx = c_idx;

    msg_idx++;
  }

  for(int i=0; i<msg_idx; i++) {
    fprintf(f, to_file_buf[i]);
    free(to_file_buf[i]);
  }

}

// void handle_msgs(int idx, FILE* f) {
//   if(state.clients[idx].recived - state.clients[idx].c_idx < 3) return;
//  
//   // printf("Client with index: %d\n", idx);
//   if(!state.clients[idx].flag_put) {
//     // log("Skip put msg");
//     state.clients[idx].ptr += 3;
//     state.clients[idx].c_idx += 3;
//     state.clients[idx].flag_put = true;
//   }
// 
//   char* ptr = state.clients[idx].ptr;
//   // hex_dump(ptr, 128); 
//
//   int c_idx = state.clients[idx].c_idx;
//  
//   if(state.clients[idx].recived - c_idx < 32) return;
//  
//   while(c_idx < state.clients[idx].recived) {
//     c_idx += 4;
//     ptr += 4;
//     char phone_1[PHONE_SIZE];
//     memcpy(phone_1, ptr, sizeof(char)*PHONE_SIZE);
//     ptr += PHONE_SIZE - 1;
//     phone_1[PHONE_SIZE-1] = '\0';
//
//     char phone_2[PHONE_SIZE];
//     memcpy(phone_2, ptr, sizeof(char)*PHONE_SIZE);
//     ptr += PHONE_SIZE - 1;
//     phone_2[PHONE_SIZE-1] = '\0';
//
//     uint8_t hh = *ptr++;
//     uint8_t mm = *ptr++;
//     uint8_t ss = *ptr++;
//
//     uint32_t ip = ntohl(state.clients[idx].ip);
//     uint16_t port = ntohs(state.clients[idx].port);
//    
//     char* msg = ptr;
//      
//     // Last byte of the message should be 0
//     c_idx += 27;
//     while(c_idx < state.clients[idx].recived && state.clients[idx].recv_buf[c_idx] != '\0') {
//       c_idx++;
//       ptr++;
//     }
//  
//     if(c_idx >= state.clients[idx].recived) return;
//      
//     int send_status = send(state.clients[idx].fd, "ok", 2, 0);
//     printf("send_status: %d\n", send_status);
//     if (send_status == SOCKET_ERROR) {
//       int error = WSAGetLastError();
//       if (error == WSAECONNRESET) {
//         printf("[!] Клиент %d разорвал соединение (RST)\n", idx);
//       } else {
//         printf("[!] Ошибка отправки: %d\n", error);
//       }
//     }
//
//     if(send_status < 0) return;
//
//     fprintf(f, "%u.%u.%u.%u:%u %s %s %02hhu:%02hhu:%02hhu %s\n", 
//         ip&0xff, (ip>>8)&0xff, (ip>>16)&0xff, (ip>>24)&0xff, 
//         port,
//         phone_1, phone_2, hh, mm, ss, msg);
//      
//     if(strncmp(msg, "stop", 4) == 0 && *(msg+4) == '\0') state.stop_server = 1;
//      
//     ptr++;
//     c_idx++;
//
//     state.clients[idx].ptr = ptr;
//     state.clients[idx].c_idx = c_idx;
//   }
// }

void recv_msgs(int idx, FILE* f) {
  memset(state.recv_buf, 0, MAX_BUF_SIZE);
  
  // char* ptr = state.recv_buf;
  char* ptr = state.clients[idx].ptr;
  int recv_status;
  bool flag_recived = false;
  
  uint32_t total_recived = 0;
  while((recv_status = recv(state.clients[idx].fd, ptr, MAX_MSG_SIZE, 0)) > 0) {
    // printf("recv: %d\n", recv_status);
    flag_recived = true;
   
    ptr += recv_status;
    state.clients[idx].recived += recv_status;
  
    total_recived += recv_status;
  
    if(recv_status < 10) Sleep(30);
    // else Sleep(10);
  }
  
  if(flag_recived) handle_msgs(idx, f);
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
  
  state.recv_buf = (char*)malloc(sizeof(char)*MAX_BUF_SIZE);
  state.curr_msg = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);

  state.sock = socket(AF_INET, SOCK_STREAM, 0);
  if(state.sock < 0) {
    fatal_err("Failed to create socket\n");
  }

  unsigned long mode = 1;
  ioctlsocket(state.sock, FIONBIO, &mode);

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
    }
    
    for(int i=0; i<MAX_CLIENTS; i++) {
      if(!is_client_active(state.clients, i)) continue;
      if(WSAEnumNetworkEvents(state.clients[i].fd, state.events[1], &ne) == 0) {
        if(ne.lNetworkEvents & FD_CLOSE) {
          printf("[+] Close\n"); 
          del_client(state.clients, i);
          continue;
        }

        if(ne.lNetworkEvents & FD_READ) {
          printf("[+] Read\n");
          recv_msgs(i, f);
        }
      }
    }
  }
  
  fclose(f);
  WSACleanup();
  server_clean();
  free_vars();
  closesocket(state.sock);

  return 0;
}
