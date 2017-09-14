#include <iostream>
#include <list>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <openssl/sha.h>
#include <arpa/inet.h>

// Types
typedef struct __attribute__((packed, aligned(1))) bootmsg
{
  unsigned char		port[6];
  unsigned char		addr[32];
}			bootmsg_t;

typedef struct		client
{
  int			serv_sock;
  unsigned short	serv_port;
  int		        boot_sock;
}			client_t;

typedef unsigned int uint;
typedef std::map<int, bootmsg_t> bootmap_t;
typedef std::map<int, client_t> clientmap_t;

// Defined
#define OPCODE_SENDTRANSACTIONS 0
#define OPCODE_SENDBLOCK	1
#define OPCODE_GETBLOCK		2
#define OPCODE_GETHASH		3
#define OPCODE_SENDPORTS	4

// Macros
#define FATAL(str) do { perror(str); exit(-1); } while (0)

// Prototypes
void execute_bootstrap();
void execute_worker(int num, std::list<int> ports);
char *build_sendport(bootmap_t portmap, int *len);
int  sha256(unsigned char *buff, unsigned int len, unsigned char *output);
