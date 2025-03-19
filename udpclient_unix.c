#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define PORT 9000

#define MAX_MSG_SIZE 16384

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
  uint8_t mm = (uint8_t)atoi(time_buf);
  *ptr_ready++ = mm;
  
  // SS
  memcpy(time_buf, ptr_raw, sizeof(char)*TIME_SIZE);
  ptr_raw += TIME_SIZE + 1;
  uint8_t ss = (uint8_t)atoi(time_buf);
  *ptr_ready++ = ss;

  int s_len = strlen(ptr_raw);
  memcpy(ptr_ready, ptr_raw, s_len-1);
  ptr_ready += s_len-1;
  *ptr_ready = '\0'; 

  memset(state.curr_msg_raw, 0, MAX_MSG_SIZE);
}

void parse_addr(char* buf, char* ip, char* port) {
  char* ptr = buf;
  char* ptr_ip = ip;
  char* ptr_port = port;
  
  while(*ptr == ' ') ptr++;
    
  int i = 0;
  while(*ptr != ':' && i++ < 16) *ptr_ip++ = *ptr++;
  *ptr_ip++ = '\0';
  
  ptr++;
  i = 0;
  while(*ptr && i++ < 5) *ptr_port++ = *ptr++;
  *ptr_port++ = '\0';
}

void hex_dump_msg(char* s, int l) {
  for(int i=0; i<l; i++) printf("%d ", s[i]);
  printf("\n");
}

void parse_msgs(FILE* f) {
  int msg_idx = 0;
  while(fgets(state.curr_msg_raw, MAX_MSG_SIZE, f)) {
    state.msgs[msg_idx] = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);  
    
    parse_msg(msg_idx, state.msgs[msg_idx]);

    // printf("parsing msg_idx: %d\n", msg_idx);
    // hex_dump_msg(state.msgs[msg_idx], 128);
    
    msg_idx++;
    state.count_msgs++;
  }
}

void recv_msg() {
  char buf[MAX_MSG_SIZE];

  struct timeval tv = {1, 100*1000};

  fd_set fds;
  FD_ZERO(&fds); FD_SET(state.sock, &fds);
  
  int res = select(state.sock+1, &fds, 0, 0, &tv);
  if(res <= 0) return;

  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  int recv_status = recvfrom(state.sock, buf, MAX_MSG_SIZE, 0, (struct sockaddr*)&addr, &addrlen);
  if(recv_status <= 0) return;
  
  uint32_t recived_msg;
  memcpy(&recived_msg, buf, sizeof(uint32_t));
  recived_msg = ntohl(recived_msg);
  // printf("[+] Recived: %d\n", recived_msg);
  
  if(recived_msg < state.count_msgs && state.msgs_hash[recived_msg] == 0) {
    state.recived_msgs++;
    state.msgs_hash[recived_msg] = 1;
  }
}

void send_msg() {
  int msg_to_send = 0;
  for(; msg_to_send<state.count_msgs && state.msgs_hash[msg_to_send] == 1; msg_to_send++) {}

  if(msg_to_send > state.count_msgs) return;

  int send_status = sendto(state.sock, state.msgs[msg_to_send], MAX_MSG_SIZE, 0, 
                           (struct sockaddr*)&state.server_addr, sizeof(state.server_addr));
}

void free_vars() {
  free(state.curr_msg_raw);
  free(state.curr_msg_ready);
  for(int i=0; i<state.count_msgs; i++) free(state.msgs[i]);
  free(state.msgs);
}

void fatal_err(const char* msg) {
  free_vars();
  close(state.sock);

  printf("%s", msg);
  exit(1);
}

int main(int argc, char **argv){
  if(argc != 3) {
    fatal_err("Usage: ./udpclinet <IP>:<PORT> <filename>\n");
  }

  char port[5];
  char ip[16];
  parse_addr(argv[1], ip, port);
  
  char* filename = argv[2];
  FILE* f = fopen(filename, "r");
  if(f == NULL) {
    fatal_err("Failed to open file\n");
  }

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
    printf("[+] Sended\n");
    recv_msg();
    sleep(1);
  }

  close(state.sock);
  free_vars();
  fclose(f);

  return 0;
}
