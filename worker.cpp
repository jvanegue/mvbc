#include "node.h"

workermap_t	workermap;
clientmap_t	clientmap;
UTXO		utxomap;
mempool_t	transpool;
blockchain_t	chain;

// Helper function to reset socket readset for select
static int reset_readset(int boot_sock, fd_set *readset)
{
  int max = 0;

  FD_ZERO(readset);
  FD_SET(boot_sock, readset);
  max = boot_sock;
  
  for (workermap_t::iterator it = workermap.begin(); it != workermap.end(); it++)
    {
      int cursock = it->second.serv_sock;
      if (cursock > max)
	max = cursock;
      FD_SET(cursock, readset);
      for (std::list<int>::iterator cit = it->second.clients.begin();
	   cit != it->second.clients.end(); cit++)
	{
	  int cursock = *cit;
	  if (cursock > max)
	    max = cursock;
	  FD_SET(cursock, readset);
	}	  
    }
  for (clientmap_t::iterator cit = clientmap.begin(); cit != clientmap.end(); cit++)
    {
      int cursock = cit->second.client_sock;
      if (cursock > max)
	max = cursock;
      FD_SET(cursock, readset);
    }
  max = max + 1;
  return (max);
}

// Connect to a new client advertized by the boot node
static int	client_connect(int port, remote_t &remote)
{
  int		client_sock;
  struct sockaddr_in caddr;
  int		err;
  int		flags;
  
  // Connect to new remote node
  client_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (client_sock < 0)
    FATAL("socket");

  // Connect to bootstrap
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  caddr.sin_addr.s_addr = inet_addr("127.0.0.1");  
  err = connect(client_sock, (struct sockaddr *) &caddr, sizeof(caddr));
  if (err < 0)
    FATAL("Failed to connect to client node");

  // Make bootstrap socket non-blocking
  flags = fcntl(client_sock, F_GETFL, 0);
  if (flags < 0)
    FATAL("client_sock fcntl1");
  err = fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);
  if (err < 0)
    FATAL("client_sock fcntl2");  

  // Register new client
  remote.remote_port = port;
  remote.client_sock = client_sock;
  clientmap[port] = remote;
  return (0);
}


// Treat events on the bootstrap node socket (possibly new remotes to connect to)
static int	bootnode_update(int boot_sock)
{
  bootmsg_t	msg;
  int		len;
  int		port;

  while (1)
    {
      len = read(boot_sock, (char *) &msg, sizeof(msg));
      if (len < 0)
	FATAL("Boot socket closed - shutting node down");
      else if (len == 0)
	return (0);
      else if (len < (int) sizeof(msg))
	FATAL("Read partial boot message - shutting node down");
      
      port = atoi((const char *) msg.port);
      clientmap_t::iterator it = clientmap.find(port);
      if (it == clientmap.end())
	{
	  remote_t newclient;
	  int	   res;
	  
	  res = client_connect(port, newclient);
	  if (res < 0)
	    FATAL("Failed to create new client");
	  
	  clientmap[port] = newclient;
	  std::cerr << "Adding new client to clientmap" << std::endl;
	}
      else if (it != clientmap.end())
	{
	  remote_t existing = clientmap[port];
	  remote_t newclient;
	  
	  int res = memcmp(existing.remote_node_addr, msg.addr, sizeof(msg.addr));
	  if (res == 0)
	    {
	      std::cerr << "Existing client not re-added to clientmap" << std::endl;
	      continue;
	    }
	  res = client_connect(port, newclient);
	  clientmap[port] = newclient;
	  close(existing.client_sock);
	  std::cerr << "Existing client (same port) changed address - reloading" << std::endl;
	}
    }

  return (0);
}


// Treat events on the worker server socket (new clients)
static int		worker_update(int port)
{
  struct sockaddr_in	client;
  worker_t		worker = workermap[port];
  socklen_t		clen;
  int			csock = accept(worker.serv_sock, (struct sockaddr *) &client, &clen);

  if (csock < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
	return (0);
      FATAL("Failed accept on worker server socket");
    }
  worker.clients.push_back(csock);
  return (0);
}


// Treat events on the miner unix socket (new block mined?)
static int	miner_update(int sock)
{

  return (0);
}

// Perform the action of mining. Write result on socket when available
static int	do_mine(char *buff, int len, int difficulty, char *hash)
{
  int `		idx;

  sha256((unsigned char*) buff, len, hash);      
  for (idx = 0; idx < difficulty; idx++)
    {
      char c = hash[31 - idx];
      if (c != '0')
	return (-1);
    }
  return (0);
}

// Perform the action of mining: 
static int	do_mine_fork(int difficulty)
{
  int		sock;
  pid_t		pid;
  
  sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0)
    FATAL("Failed to create AF_UNIX for miner");
  pid = fork();
  if (!pid)
    {
      unsigned char  hash[32];
      blockmsg_t     lastblock = chain.top();
      blockmsg_t	newblock;
      
      newblock.priorhash = ;
      newblock.height = ;
      sha256_mineraddr(newblock.mineraddr)
      do {
	// WIP
	memset("");
      }
      while (do_mine(buff, len, difficulty, hash) < 0);
      
    }
  else
    {
      
    }
  
  
  // Launch miner (fork). Miner has UNIX socket that is added to the readset    

  return (0);
}


// Verify transaction and add it to the pool if correct
static int	trans_verify(transmsg_t trans, unsigned int numtxinblock, int difficulty)
{
  account_t	sender;
  account_t	receiver;
  
  std::string mykey = hash_binary_to_string(trans.sender);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown sender - ignoring" << std::endl;
      return (0);
    }  
  sender = utxomap[mykey];
  mykey = hash_binary_to_string(trans.receiver);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown receiver - ignoring" << std::endl;
      return (0);
    }
  receiver = utxomap[mykey];
  unsigned int	amount = atoi((const char *) trans.amount);
  if (amount > sender.amount)
    {
      std::cerr << "Received transaction with bankrupt sender - ignoring" << std::endl;
      return (0);
    }
  transpool.push_back(trans);
  if (transpool.size() == numtxinblock)
    do_mine(difficulty);
  return (0);      
}



// Treat traffic from existing worker's clients
// Could be transaction or block messages usually
  static int	client_update(int port, int client_sock, unsigned int numtxinblock, int difficulty)
{
  worker_t	worker = workermap[port];
  char		blockheight[32];
  transmsg_t	trans;
  unsigned char opcode;

  int len = read(client_sock, &opcode, 1);
  if (len != 1)
    FATAL("read");

  switch (opcode)
    {
    case OPCODE_SENDTRANS:
      std::cerr << "SENDTRANS OPCODE " << std::endl;

      len = read(client_sock, &trans, sizeof(trans));
      if (len != sizeof(trans))
      	FATAL("Not enough bytes in SENDTRANS message");
      trans_verify(trans, numtxinblock, difficulty);
      break;
      
    case OPCODE_SENDBLOCK:
      std::cerr << "SENDBLOCK OPCODE " << std::endl;

      // If new block is the same height as block being mined, kill miner(s) and close their sockets
      // If new block is height is greater, use GET_BLOCK/GET_HASH to synchronize local chain

      std::cerr << "UNIMPLEMENTED: SENDBLOCK" << std::endl;
      return (0);
      
      break;
      
    case OPCODE_GETBLOCK:
      std::cerr << "GETBLOCK OPCODE " << std::endl;

      len = read(client_sock, blockheight, sizeof(blockheight));
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETBLOCK message");

      // send content of that block if it exists
      std::cerr << "UNIMPLEMENTED: GETBLOCK" << std::endl;
      return (0);
      
      break;
      
    case OPCODE_GETHASH:
      std::cerr << "GETHASH OPCODE " << std::endl;

      len = read(client_sock, blockheight, sizeof(blockheight));
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETBLOCK message");

      // send back the hash of that block if it exists
      std::cerr << "UNIMPLEMENTED: GETHASH" << std::endl;
      return (0);
      
      break;
      
    default:
      std::cerr << "INVALID OPCODE " << opcode << std::endl;
      return (0);
      break;
    }
  
  return (0);
}


// Initialize all wallets
void		UTXO_init()
{
  unsigned int	predef;

  for (predef = 0; predef <= 100; predef++)
    {
      char	     buff[4];
      unsigned char  hash[32];
      account_t	     acc;
      int	     len;
      std::string    key;
      
      memset(&acc, 0x00, sizeof(acc));
      memset(buff, 0x00, sizeof(buff));
      len = snprintf(buff, sizeof(buff), "%u", predef);
      sha256((unsigned char*) buff, len, hash);
      acc.amount = 100000;
      key = hash_binary_to_string(hash);
      utxomap[key] = acc;
    }

  std::cerr << "Finished initializing UTXO" << std::endl;
}



// Main procedure for node in worker mode
void	  execute_worker(unsigned int numtxinblock, int difficulty,
			 int numworkers, std::list<int> ports)
{
  fd_set  readset;
  int	  err = 0;
  int     boot_sock;
  int	  serv_sock;
  struct sockaddr_in caddr;
  int     max;
  int	  flags;
  
  std::cout << "Executing in worker mode" << std::endl;

  UTXO_init();
  FD_ZERO(&readset);

  // Connect to bootstrap node
  boot_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (boot_sock < 0)
    FATAL("socket");

  // Connect to bootstrap
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(8888);
  caddr.sin_addr.s_addr = inet_addr("127.0.0.1");  
  err = connect(boot_sock, (struct sockaddr *) &caddr, sizeof(caddr));
  if (err < 0)
    FATAL("Failed to connect to boot node");

  // Make bootstrap socket non-blocking
  flags = fcntl(boot_sock, F_GETFL, 0);
  if (flags < 0)
    FATAL("boot_sock fcntl1");
  err = fcntl(boot_sock, F_SETFL, flags | O_NONBLOCK);
  if (err < 0)
    FATAL("boot_sock fcntl2");
  
  // Establish server socket for each worker and client socket from each worker to bootstrap
  for (std::list<int>::iterator it = ports.begin(); it != ports.end(); it++)
    {

      // Create server socket for worker
      serv_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (serv_sock < 0)
	FATAL("socket");

      struct sockaddr_in saddr;
      
      saddr.sin_family = AF_INET;
      saddr.sin_port = htons(*it);
      saddr.sin_addr.s_addr = INADDR_ANY;
      
      int err = bind(serv_sock, (struct sockaddr *) &saddr, sizeof(saddr));
      if (err < 0)
	FATAL("bind");
      err = listen(serv_sock, 100);
      if (err < 0)
	FATAL("listen");

      // Register new client
      worker_t newworker;

      newworker.serv_sock = serv_sock;
      newworker.serv_port = (*it);
      workermap[(*it)] = newworker;

      // Advertize new worker to bootstrap node
      bootmsg_t msg;
      pack_bootmsg((*it), &msg);
      err = send(boot_sock, (char *) &msg, sizeof(msg), 0);
      if (err < 0)
	FATAL("send");
    }
  
  // Listen to all incoming traffic
  while (1)
    {
      
      // Reset the read set
      max = reset_readset(boot_sock, &readset);
      
      int ret = 0;
      do { ret = select(max, &readset, NULL, NULL, NULL); }
      while (ret == -1 && errno == EINTR); 
      if (ret == -1)
	FATAL("select");

      // Will possibly update the remote map if boot node advertize new nodes
      if (FD_ISSET(boot_sock, &readset))
	{
	  ret = bootnode_update(boot_sock);
	  if (ret < 0)
	    FATAL("bootnode_update");
	  continue;
	}

      // Check if any worker has been connected to by new clients
      for (workermap_t::iterator it = workermap.begin(); it != workermap.end(); it++)
	{
	  int serv_sock = it->second.serv_sock;
	  if (FD_ISSET(serv_sock, &readset))
	    {
	      ret = worker_update(it->first);
	      if (ret < 0)
		FATAL("worker_update");
	      continue;
	    }

	  // Treat traffic from existing worker's clients
	  for (std::list<int>::iterator it2 = it->second.clients.begin();
	       it2 != it->second.clients.end(); it2++)
	    {
	      int client_sock = *it2;
	      if (FD_ISSET(client_sock, &readset))
		{
		  ret = client_update(it->first, client_sock, numtxinblock, difficulty);
		  if (ret < 0)
		    FATAL("client_update");
		}
	    }
	  
	}

    }

}


