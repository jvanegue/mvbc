#include <iostream>
#include <list>
#include <map>
#include <stack>
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
#include <unistd.h>
#include <sstream>
#include <fcntl.h>

// Types
typedef struct __attribute__((packed, aligned(1))) bootmsg
{
  unsigned char		port[6];
  unsigned char		addr[32];
}			bootmsg_t;

typedef struct __attribute__((packed, aligned(1))) hdr
{
  char			opcode;
}			hdr_t;

typedef struct __attribute__((packed, aligned(1))) transdata
{
  unsigned char		sender[32];
  unsigned char		receiver[32];
  unsigned char		amount[32];
  unsigned char		timestamp[32];
}			transdata_t;

typedef struct __attribute__((packed, aligned(1))) transmsg
{
  hdr_t			hdr;
  transdata_t		data;
}			transmsg_t;

typedef struct __attribute__((packed, aligned(1))) blockmsg
{
  unsigned char		nonce[32];
  unsigned char		priorhash[32];
  unsigned char		hash[32];
  unsigned char		height[32];
  unsigned char		mineraddr[32];
}			blockmsg_t;

typedef struct __attribute__((packed, aligned(1))) blockdata
{
  unsigned char		nonce[32];
  unsigned char		priorhash[32];
  unsigned char		height[32];
  unsigned char		mineraddr[32];
}			blockhash_t;

typedef struct		block
{
  blockmsg_t		hdr;
  transmsg_t		*trans;
}			block_t;

typedef struct		miner
{
  pid_t			pid;
  int			sock;
}			miner_t;

typedef struct		worker
{
  int			serv_sock;
  unsigned short	serv_port;
  std::list<int>	clients;
  miner_t		miner;
}			worker_t;

typedef struct		remote
{
  int			client_sock;
  unsigned short	remote_port;
  char			remote_node_addr[32];
}			remote_t;

typedef struct		 account
{
  unsigned long long int amount;
}			 account_t;

typedef unsigned long long int ullint;
typedef unsigned int uint;
typedef std::map<int, bootmsg_t> bootmap_t;
typedef std::map<int, remote_t>  clientmap_t;
typedef std::map<int, worker_t>  workermap_t;
typedef std::map<int, miner_t>   minermap_t;
typedef std::map<std::string,account_t> UTXO;
typedef std::list<transmsg_t>	 mempool_t;
typedef std::stack<block_t>	 blockchain_t;

// Defined
#define OPCODE_SENDTRANS	'0'
#define OPCODE_SENDBLOCK	'1'
#define OPCODE_GETBLOCK		'2'
#define OPCODE_GETHASH		'3'
#define OPCODE_SENDPORTS	'4'

#define DEFAULT_TRANS_PER_BLOCK	50000

// Macros
#define FATAL(str) do { perror(str); exit(-1); } while (0)

// Prototypes
void		execute_bootstrap();
void		execute_worker(unsigned int numtx, int difficulty, int num, std::list<int> ports);
char		*pack_sendport(bootmap_t portmap, int *len);
void		pack_bootmsg(unsigned short port, bootmsg_t *msg);
int		sha256(unsigned char *buff, unsigned int len, unsigned char *output);
void		sha256_mineraddr(unsigned char *output);
char		*unpack_sendblock(char *buf, int len);
char		*unpack_sendtransaction(char *buf, int len);
std::string	hash_binary_to_string(unsigned char hash[32]);
void		string_integer_increment(char *buff, int len);
