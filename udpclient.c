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

#define BUF_SIZE 1024
#define PORT 9000

#define MAX_MSG_SIZE 4096

#define PHONE_SIZE 12
#define TIME_SIZE 2
#define FIRST_PART_SIZE   35
#define SECOND_PART_SIZE  MAX_MSG_SIZE-FIRST_PART_SIZE

#define MAX_MSGS 64
#define LIM_MSGS 20

typedef struct state_t_ {
  struct sockaddr_in server_addr;
  int sock;

  char* curr_msg_raw;
  char* curr_msg_ready;
  char** msgs;
  size_t sz;

  size_t recived_msgs;
  size_t count_msgs;

  int msgs_hash[MAX_MSGS];
} state_t;
  
state_t state;

void parse_msg(uint32_t idx, char* dst) {
  idx = htonl(idx);

  char* ptr_raw = state.curr_msg_raw;
  char* ptr_ready = dst;
  
  // Skip spaces
  while(*ptr_raw && *ptr_raw == ' ') ptr_raw++;
  
  // Message index
  memcpy(ptr_ready, &idx, sizeof(uint32_t));
  ptr_ready += sizeof(uint32_t);
  
  // Phone 1
  // printf("Msg %d -> Phone 1: %s\n", htonl(idx), ptr_raw);
  memcpy(ptr_ready, ptr_raw, sizeof(char)*PHONE_SIZE);
  ptr_ready += PHONE_SIZE;
  ptr_raw += PHONE_SIZE + 1;
  
  // Phone 2
  memcpy(ptr_ready, ptr_raw, sizeof(char)*PHONE_SIZE);
  ptr_ready += PHONE_SIZE;
  ptr_raw += PHONE_SIZE + 1;
  
  char time_buf[3];
  
  // HH
  memcpy(time_buf, ptr_raw, sizeof(char)*TIME_SIZE);
  ptr_raw += TIME_SIZE + 1;
  uint8_t hh = (uint8_t)atoi(time_buf);   
  *ptr_ready++ = hh;

  // MM
  memcpy(time_buf, ptr_raw, sizeof(char)*TIME_SIZE);
  ptr_raw += TIME_SIZE + 1;
  printf("%d\n", (uint8_t)atoi(time_buf));
  uint8_t mm = (uint8_t)atoi(time_buf);
  *ptr_ready++ = mm;
  
  // SS
  memcpy(time_buf, ptr_raw, sizeof(char)*TIME_SIZE);
  ptr_raw += TIME_SIZE + 1;
  uint8_t ss = (uint8_t)atoi(time_buf);
  *ptr_ready++ = ss;

  // for(int i=0; i<128; i++) printf("%d ", state.curr_msg_ready[i]);
  // printf("\n");
  
  int s_len = strlen(ptr_raw);
  printf("s_len: %d\n", s_len);
  memcpy(ptr_ready, ptr_raw, s_len-2);
}

void parse_addr(char* buf, char* ip, char* port) {
  char* ptr = buf;
  char* ptr_ip = ip;
  char* ptr_port = port;
  
  while(*ptr == ' ') ptr++;
    
  int i = 0;
  while(*ptr != ':' && i++ < 16) *ptr_ip++ = *ptr++;
  *ptr_ip++ = '\0';
 
  i = 0;
  while(*ptr && i++ < 5) *ptr_port++ = *ptr++;
  *ptr_port++ = '\0';
}

void parse_msgs(FILE* f) {
  int msg_idx = 0;
  while(fgets(state.curr_msg_raw, MAX_MSG_SIZE, f)) {
    state.msgs[msg_idx] = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);  
    
    parse_msg(msg_idx, state.msgs[msg_idx]);

    msg_idx++;
    state.count_msgs++;
    memset(state.curr_msg_raw, 0, MAX_MSG_SIZE);
  }
}

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
  
  uint32_t recived_msg;
  memcpy(&recived_msg, buf, sizeof(uint32_t));
  recived_msg = ntohl(recived_msg);
  printf("[+] Recived: %d\n", recived_msg);
  
  if(recived_msg < state.count_msgs && state.msgs_hash[recived_msg] == 0) {
    state.recived_msgs++;
    state.msgs_hash[recived_msg] = 1;
  }
}

void send_msg() {
  int msg_to_send = 0;
  for(; msg_to_send<state.count_msgs && state.msgs_hash[msg_to_send] == 0; msg_to_send++) {}

  if(msg_to_send > state.count_msgs) return;

  int send_status = sendto(state.sock, state.msgs[msg_to_send], MAX_MSG_SIZE, MSG_NOSIGNAL, 
                           (struct sockaddr*)&state.server_addr, sizeof(state.server_addr));
}

void free_vars() {
  free(state.curr_msg_raw);
  free(state.curr_msg_ready);
  for(int i=0; i<state.count_msgs; i++) free(state.msgs[i]);
  free(state.msgs);
}

int main(int argc, char **argv){
  if(argc != 3) {
    fprintf(stderr, "Usage: ./udpclinet <IP>:<PORT> <filename>\n");
    return 1;
  }

  char port[5];
  char ip[16];
  parse_addr(argv[1], ip, port);

  char* filename = argv[2];
  FILE* f = fopen(filename, "r");
  
  state.curr_msg_raw = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.curr_msg_ready = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.msgs = (char**)malloc(sizeof(char*)*MAX_MSGS);

  state.sock = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&state.server_addr, 0, sizeof(state.server_addr));

  state.server_addr.sin_family = AF_INET;
  state.server_addr.sin_port = htons(atoi(port));
  state.server_addr.sin_addr.s_addr = inet_addr(ip);

  parse_msgs(f);

  while(state.recived_msgs < LIM_MSGS && state.recived_msgs < state.count_msgs) {
    send_msg();
    recv_msg();
  }


done:
  close(state.sock);
  free_vars();

  return 0;
}
