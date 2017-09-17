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

typedef struct __attribute__((packed, aligned(1))) transmsg
{
  unsigned char		sender[32];
  unsigned char		receiver[32];
  unsigned char		amount[32];
  unsigned char		timestamp[32];
}			transmsg_t;

typedef struct __attribute__((packed, aligned(1))) blockmsg
{
  unsigned char		nonce[32];
  unsigned char		priorhash[32];
  unsigned char		hash[32];
  unsigned char		height[32];
  unsigned char		mineraddr[32];
}			blockmsg_t;

typedef struct		worker
{
  int			serv_sock;
  unsigned short	serv_port;
  std::list<int>	clients;
}			worker_t;

typedef struct		remote
{
  int			client_sock;
  unsigned short	remote_port;
}			remote_t;

typedef struct		account
{
  unsigned char		addr[32];
}			account_t;

typedef unsigned int uint;
typedef std::map<int, bootmsg_t> bootmap_t;
typedef std::map<int, remote_t>  clientmap_t;
typedef std::map<int, worker_t>  workermap_t;
typedef std::map<account_t, unsigned long long int> UTXO;
typedef std::list<transmsg_t>	 mempool;

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
char *pack_sendport(bootmap_t portmap, int *len);
void pack_bootmsg(unsigned short port, bootmsg_t *msg);
int  sha256(unsigned char *buff, unsigned int len, unsigned char *output);

char		*unpack_sendblock(char *buf, int len);
char		*unpack_sendtransaction(char *buf, int len);
