#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h>
#include <arpa/inet.h>

#define RECV_BUF_SIZE 64
#define PORT 9000

#define MAX_MSG_SIZE 4096

#define PHONE_SIZE 12
#define TIME_SIZE 2
#define FIRST_PART_SIZE   35
#define SECOND_PART_SIZE  MAX_MSG_SIZE-FIRST_PART_SIZE

typedef struct state_t_ {
  struct sockaddr_in server_addr;
  int sock;

  char* curr_msg_raw;
  char* curr_msg_ready;
  char* recv_buf;

	size_t curr_msg_len;
  size_t sz;
} state_t;

state_t state;

void hex_dump(char* buf, int len) {
	for(int i=0; i<len; i++) printf("%x ", buf[i] & 0xff);
	printf("\n");
}

void parse_msg(uint32_t idx) {
  idx = htonl(idx);
  memset(state.curr_msg_ready, 0, MAX_MSG_SIZE);

  char* ptr_raw = state.curr_msg_raw;
  char* ptr_ready = state.curr_msg_ready;
  
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
  // printf("%d\n", (uint8_t)atoi(time_buf));
  uint8_t mm = (uint8_t)atoi(time_buf);
  *ptr_ready++ = mm;
  
  // SS
  memcpy(time_buf, ptr_raw, sizeof(char)*TIME_SIZE);
  ptr_raw += TIME_SIZE + 1;
  uint8_t ss = (uint8_t)atoi(time_buf);
  *ptr_ready++ = ss;
	

  
  int s_len = strlen(ptr_raw);
  memcpy(ptr_ready, ptr_raw, s_len);
	ptr_raw += s_len;
	*ptr_raw = '\0';

	/* printf("[+] Hex dump in parse_msg:\n"); */
	/* hex_dump(state.curr_msg_ready, 56); */
	
	state.curr_msg_len = FIRST_PART_SIZE + s_len;
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

void free_vars() {
  free(state.curr_msg_raw);
  free(state.curr_msg_ready);
  free(state.recv_buf);
}

void send_msg() {
  /* printf("[+] Hex dump in send_msg:\n"); */
	/* hex_dump(state.curr_msg_ready, 56); */
	/* printf("---------------\n"); */
	int send_status = send(state.sock, state.curr_msg_ready, state.curr_msg_len, 0);
}

void wait_ok_msg() {
  int recv_status = recv(state.sock, state.recv_buf, MAX_MSG_SIZE, 0);
  while(strncmp(state.recv_buf, "ok", 2) != 0) {
    recv_status = recv(state.sock, state.recv_buf, MAX_MSG_SIZE, 0);
		/* printf("[+] Recived: %s\n", state.recv_buf); */
	}
	/* printf("[+] Recived ok\n"); */
}

void send_msgs(FILE* f) {
  int read_status;
  uint32_t idx = 0;
  char recv_buf[MAX_MSG_SIZE];

  int send_status = send(state.sock, "put", 3, 0);
  // wait_ok_msg();
  
  while(fgets(state.curr_msg_raw, MAX_MSG_SIZE, f)) {
    printf("%s\n", state.curr_msg_raw);
    char* ptr = state.curr_msg_raw;
    /* while(*ptr && (*ptr == '\n' || *ptr == '\r')) ptr++; */
		if(*ptr == '\0' || *ptr == '\n' || *ptr == '\r') continue;
		
		/* printf("Parsing msg: %d\n", idx); */
    parse_msg(idx++);
    send_msg();
    /* printf("[+] Sended msg: %d\n", idx-1); */
    wait_ok_msg();

		memset(state.curr_msg_raw, 0, MAX_MSG_SIZE);
  }
}

void fatal_err(const char* msg) {
  free_vars();
	close(state.sock);

  printf("%s", msg);
  exit(1);
}

int main(int argc, char** argv) {
  if(argc != 3) {
    fatal_err("Usage: ./tcpclinet <IP>:<PORT> <filename>\n");
  }

  char port[5];
  char ip[16];
  parse_addr(argv[1], ip, port);

  char* filename = argv[2];
  FILE* f = fopen(filename, "r");
  
  state.curr_msg_raw = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.curr_msg_ready = (char*)malloc(sizeof(char)*MAX_MSG_SIZE);
  state.recv_buf = (char*)malloc(sizeof(char)*RECV_BUF_SIZE);

  state.sock = socket(AF_INET, SOCK_STREAM, 0);
  if(state.sock < 0) {
    fatal_err("Failed to create socket\n");
  }

  memset(&state.server_addr, 0, sizeof(state.server_addr));
  state.server_addr.sin_addr.s_addr = inet_addr(ip);
  state.server_addr.sin_port = htons(atoi(port));
  state.server_addr.sin_family = AF_INET;
  
  int tries = 10;
  int conn = -1;
  while(conn < 0 && tries--) {
    conn = connect(state.sock, (struct sockaddr*)&state.server_addr, sizeof(state.server_addr));
    if(conn >= 0) break;
      
		usleep(100*1000);
  }

  if(conn < 0) {
    fatal_err("Connection error\n");
  }
  
  /* printf("Connection success\n"); */
  send_msgs(f);
	
	/* printf("[+] Closeing connection\n"); */
  close(state.sock);
  free_vars();

  return 0;
}
