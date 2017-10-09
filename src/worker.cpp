#include "node.h"

workermap_t	workermap;
clientmap_t	clientmap;
UTXO		utxomap;
mempool_t	transpool;
mempool_t	pending_transpool;
blockchain_t	chain;
pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;


// Helper function to reset socket readset for select
static int reset_readset(int boot_sock, fd_set *readset)
{
  int max = 0;

  FD_ZERO(readset);
  if (boot_sock != 0)
    {
      FD_SET(boot_sock, readset);
      max = boot_sock;
    }
  
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
      miner_t miner = it->second.miner;
      if (miner.sock != 0)
	FD_SET(miner.sock, readset);
      if (miner.sock > max)
	max = miner.sock;
      
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
    FATAL("client_connect socket");

  std::cerr << "Connecting client on port " << port << std::endl;
  
  // Connect to bootstrap
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  caddr.sin_addr.s_addr = inet_addr("127.0.0.1");  
  err = connect(client_sock, (struct sockaddr *) &caddr, sizeof(caddr));
  if (err < 0)
    FATAL("Failed to connect to client node");

  // Make client socket non-blocking
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

  // Make sure we do this somewhere
  //FD_SET(client_sock, &readset);
  
  std::cerr << "Remote Added and connected to new client port " << port << std::endl;
  
  return (0);
}


// Treat events on the bootstrap node socket (possibly new remotes to connect to)
static void	bootnode_update(int boot_sock)
{
  char		buf[7];
  int		len; 
  int		port;
  char		*ports = NULL;

  std::cerr << "Bootnode update!" << std::endl;
  
  memset(buf, 0x00, sizeof(buf));
  len = read(boot_sock, buf, 6);
  if (len < 0)
    {
      std::cerr << "Bootnode socket closed" << std::endl;
      return;
    }
  else if (len == 0)
    {
      std::cerr << "1 Bootnode update read = 0" << std::endl;
      return;
    }

  std::cerr << "Received bytes on bootnode update: ";
  for (int idx = 0; idx < 6; idx++)
    fprintf(stderr, "%u ", (unsigned int) buf[idx]);
  std::cerr << std::endl;
  
  int numofports = atoi(buf);
  if (numofports == 0)
    {
      std::cerr << "Numofports = 0 : return early" << std::endl;
      return;
    }
  
  std::cerr << "Bootnode update: found numofport = " << numofports << std::endl;
  
  ports = (char *) malloc(numofports * 6);
  if (ports == NULL)
    {
      std::cerr << "Invalid number of ports - shutting down boot socket" << std::endl;
      return;
    }
  len = read(boot_sock, ports, numofports * 6);
  if (len < 0)
    {
      std::cerr << "Unable to read - shutting down boot socket" << std::endl;
      free(ports);
      return;
    }
  else if (len == 0)
    {
      std::cerr << "2 Bootnode update read = 0" << std::endl;
      free(ports);
      return;
    }
  
  for (int idx = 0; idx < numofports; idx++)
    {
      memset(buf, 0x00, sizeof(buf));
      memcpy(buf, ports + (idx * 6), 6);
      port = atoi((const char *) buf);
      clientmap_t::iterator it = clientmap.find(port);
      if (workermap.find(port) != workermap.end())
	{
	  std::cerr << "PORT " << port << " is a local worker - do not establish remote"
		    << std::endl;
	}
      else if (it == clientmap.end())
	{
	  std::cerr << "PORT " << port << " has no existing remote - creating " << std::endl;
	  
	  remote_t newclient;
	  int	   res;
	  
	  res = client_connect(port, newclient);
	  if (res < 0)
	    {
	      std::cerr << "Failed to create new client" << std::endl;
	      free(ports);
	      return;
	    }

	  std::cerr << "Added new client to clientmap (port " << port << ")" << std::endl;
	}
      else if (it != clientmap.end())
	{
	  std::cerr << "Existing client (port " << port
		    << ") declared by boot node - unchanged entry" << std::endl;
	}
    }

  free(ports);
  return;
}


// Treat events on the worker server socket (new clients)
static int		worker_update(int port)
{
  struct sockaddr_in	client;
  worker_t&		worker = workermap[port];
  socklen_t		clen;

  std::cerr << "worker update: now accepting connection on port "
	    << port << " socket = " << worker.serv_sock << std::endl;
  
  int			csock = accept(worker.serv_sock, (struct sockaddr *) &client, &clen);

  if (csock < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
	{
	  FATAL("Failed accept on worker socket : EAGAIN/WOULDBLOCK");
	  return (0);
	}
      FATAL("Failed accept on worker server socket");
    }

  std::cerr << "worker update: accepted connection on port " << port << std::endl;
  
  worker.clients.push_back(csock);
  workermap[port] = worker;
  return (0);
}


// Treat events on the miner unix socket (new block mined?)
static int	miner_update(worker_t& worker, int sock, int numtxinblock)
{
  blockmsg_t     newblock;
  char		*data;
  int		readlen;

  std::cerr << "MINER UPDATE!" << std::endl;
  
  // Read mined block from miner
  int len = read(sock, (char *) &newblock, sizeof(newblock));
  if (len == 0)
    {
      std::cerr << "Miner read returned 0 : passing" << std::endl;
      return (0);
    }
  if (len != sizeof(newblock))
    {
      std::cerr << "INCOMPLETE READ " << len << " bytes from miner pipefd" << std::endl;
      return (-1);
    }
  
  len = sizeof(transdata_t) * numtxinblock;
  data = (char *) malloc(len);
  if (data == NULL)
    {
      std::cerr << "Failed to malloc in miner update" << std::endl;
      return (-1);
    }
  
  readlen = read(sock, data, len);
  if (readlen != len)
    {
      std::cerr << "Failed to send in miner update 2" << std::endl;
      free(data);
      return (-1);
    }

  std::cerr << "MINER READ!" << std::endl;
  
  // Send block to all remotes
  for (clientmap_t::iterator it = clientmap.begin(); it != clientmap.end(); it++)
    {
      remote_t	remote = it->second;
      char	c = OPCODE_SENDBLOCK;

      std::cerr << "Sending block to remote on port " << remote.remote_port << std::endl;
      
      async_send(remote.client_sock, &c, 1, "Miner update");
      async_send(remote.client_sock, (char *) &newblock, sizeof(newblock), "Miner update 2");
      async_send(remote.client_sock, (char *) data, len, "Miner update 3");
    }

  // Update wallets amount values
  for (int idx = 0; idx < numtxinblock; idx++)
    {
      transdata_t *curtrans = &((transdata_t *) data)[idx];

      std::string sender_key = hash_binary_to_string(curtrans->sender);
      std::string receiver_key = hash_binary_to_string(curtrans->receiver);

      if (utxomap.find(sender_key) == utxomap.end())
	{
	  std::cerr << "Failed to find wallet by sender key - bad key encoding?" << std::endl;
	  free(data);
	  return (-1);
	}

      if (utxomap.find(receiver_key) == utxomap.end())
	{
	  std::cerr << "Failed to find wallet by receiver key - bad key encoding?" << std::endl;
	  free(data);
	  return (-1);
	}

      std::cerr << "Both sender and receiver are valid" << std::endl;
      
      account_t sender = utxomap[sender_key];
      account_t receiver = utxomap[receiver_key];      
      unsigned char result[32], result2[32];

      wallet_print("Before transaction: ", sender.amount, curtrans->amount, receiver.amount);
      
      string_sub(sender.amount, curtrans->amount, result);
      string_add(receiver.amount, curtrans->amount, result2);
      memcpy(sender.amount, result, 32);
      memcpy(receiver.amount, result2, 32);
      utxomap[sender_key] = sender;
      utxomap[receiver_key] = receiver;
      
      wallet_print("After transaction: ", sender.amount, curtrans->amount, receiver.amount);
     }

  // Close UNIX socket and zero miner pid
  close(worker.miner.sock);
  worker.miner.sock = 0;

  block_t    chain_elem;

  chain_elem.hdr = newblock;
  chain.push(chain_elem);

  free(data);
  
  std::cerr << "Blockchain and Wallets updated! new current height = " << tag2str(newblock.height) << std::endl;
  
  return (0);
}

// Perform the action of mining. Write result on socket when available
static int	do_mine(char *buff, int len, int difficulty, char *hash)
{
  int 		idx;
  
  sha256((unsigned char*) buff, len, (unsigned char *) hash);      
  for (idx = 0; idx < difficulty; idx++)
    {
      char c = hash[31 - idx];
      if (c != '0')
	return (-1);
    }
  return (0);
}


// Perform the action of mining: 
int		do_mine_fork(worker_t &worker, int difficulty, int numtxinblock)
{
  int		pipefd[2];
  pid_t		pid;

  if (pipe(pipefd) != 0)
    FATAL("pipe");

  // Child: start the miner in a different process
  pid = fork();
  if (!pid)
    {
      close(pipefd[0]);
      
      unsigned char  hash[32];
      blockmsg_t     newblock;
      
      if (false == chain.empty())
	{
	  block_t      lastblock = chain.top();
	  blockmsg_t   header    = lastblock.hdr;
	  memcpy(newblock.priorhash, header.hash, sizeof(newblock.priorhash));
	  memcpy(newblock.height, header.height, sizeof(newblock.height));
	  string_integer_increment((char *) newblock.height, sizeof(newblock.height));
  	}
      else
	{
	  unsigned char	  priorhash[32];
	  memset(priorhash, '0', sizeof(priorhash));
	  sha256(priorhash, sizeof(priorhash), newblock.priorhash);
	  memset(newblock.height, '0', sizeof(newblock.height));
	}      
      sha256_mineraddr(newblock.mineraddr);

      char		*buff;
      blockhash_t	*data;
      int		len;

      // Prepare for hashing
      len = sizeof(blockhash_t) + sizeof(transdata_t) * numtxinblock;
      buff = (char *) malloc(len);
      if (buff == NULL)
	FATAL("FAILED miner malloc");
      data = (blockhash_t *) buff;
      memset(data->nonce, '0', sizeof(data->nonce));

      memcpy(data->priorhash, newblock.priorhash, 32);
      memcpy(data->height, newblock.height, 32);
      memcpy(data->mineraddr, newblock.mineraddr, 32);

      // Copy transaction buffer into block to mine
      int off = sizeof(blockhash_t);
      for (mempool_t::iterator it = transpool.begin(); it != transpool.end(); it++)
	{
	  transmsg_t cur = it->second;
	  transdata_t curdata = cur.data;
	  
	  memcpy(buff + off, &curdata, sizeof(curdata));
	  off += sizeof(transdata_t);
	}
      
      // Mine
      while (do_mine(buff, len, difficulty, (char *) hash) < 0)
	string_integer_increment((char *) data->nonce, sizeof(data->nonce));	      
      memcpy(newblock.hash, hash, sizeof(newblock.hash));

      std::cerr << "WORKER on port " << worker.serv_port << " MINED BLOCK!" << std::endl;
      
      // Send result on UNIX socket and exit
      if (sizeof(newblock) != write(pipefd[1], (char *) &newblock, sizeof(newblock)))
	std::cerr << "Miner process failed to write new block on UNIX socket" << std::endl;
      int totlen = len - sizeof(blockhash_t);
      if (totlen != write(pipefd[1], (char *) buff + sizeof(blockhash_t), totlen))
	std::cerr << "Miner process failed to write new block hash on UNIX socket" << std::endl;

      std::cerr << "MINER will exit" << std::endl;
      
      //close(pipefd[1]);
      free(buff);
      exit(0);
    }


  // Parent: add miner in miner map and add miner unix socket to readset
  else
    {
      miner_t newminer;

      close(pipefd[1]);
      newminer.sock = pipefd[0];
      newminer.pid = pid;
      worker.miner = newminer;
    }
  
  // Return to main select loop
  return (0);
}




// Treat traffic from existing worker's clients
// Could be transaction or block messages usually
static int	client_update(int port, int client_sock, unsigned int numtxinblock, int difficulty)
{
  worker_t&	worker = workermap[port];
  char		blockheight[32];
  transmsg_t	trans;
  transdata_t	data;
  unsigned char opcode;
  blockmsg_t	block;
  char		*transdata = NULL;
  
  int len = read(client_sock, &opcode, 1);
  if (len == 0)
    return (1);
  if (len != 1)
    FATAL("FAILED client update read");

  switch (opcode)
    {

      // Send transaction opcode
    case OPCODE_SENDTRANS:
      std::cerr << "SENDTRANS OPCODE " << std::endl;
      len = read(client_sock, (char *) &data, sizeof(data));
      if (len == 0)
	{
	  std::cerr << "SENDTRANS read for data returned 0 - passing" << std::endl;
	  return (0);
	}
      
      if (len < (int) sizeof(data))
      	FATAL("Not enough bytes in SENDTRANS message");
      trans.hdr.opcode = OPCODE_SENDTRANS;
      trans.data = data;
      trans_verify(worker, trans, numtxinblock, difficulty);
      return (0);
      break;

      // Send block opcode
    case OPCODE_SENDBLOCK:
      std::cerr << "SENDBLOCK OPCODE " << std::endl;
      len = async_read(client_sock, (char *) &block, sizeof(block), "sendblock read (1)");
      transdata = (char *) malloc(numtxinblock * 128);
      if (transdata == NULL)
	FATAL("SENDBLOCK malloc");
      len = async_read(client_sock, (char *) transdata, numtxinblock * 128, "sendblock read (2)");
      chain_store(block, transdata, numtxinblock, port);
      //free(transdata); -- this is kept for roll back purpose 
      return (0);
      break;

      // Get block opcode
    case OPCODE_GETBLOCK:
      std::cerr << "GETBLOCK OPCODE " << std::endl;
      len = read(client_sock, blockheight, sizeof(blockheight));
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETBLOCK message");

      // FIXME: send content of that block if it exists
      std::cerr << "UNIMPLEMENTED: GETBLOCK" << std::endl;
      exit(-1);
      break;

      // Get hash opcode
    case OPCODE_GETHASH:
      std::cerr << "GETHASH OPCODE " << std::endl;
      len = read(client_sock, blockheight, sizeof(blockheight));
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETBLOCK message");

      // FIXME: send back the hash of that block if it exists
      std::cerr << "UNIMPLEMENTED: GETHASH" << std::endl;
      exit(-1);
      break;

      // Send ports opcode (only sent via boot node generally)
    case OPCODE_SENDPORTS:
      std::cerr << "SENDPORT OPCODE " << std::endl;
      bootnode_update(client_sock);
      return (1);
      break;

      
    default:
      std::cerr << "INVALID OPCODE " << opcode << std::endl;
      return (0);
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
      memcpy(acc.amount, "00000000000000000000000000100000", 32);
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

      int val = 1;
      if (setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int)) < 0)
	FATAL("worker serv_sock: setsockopt SO_REUSEADDR failed");
      
      struct sockaddr_in saddr;
      int port = *it;

      std::cerr << "Worker now binding server socket on port " << port << std::endl;
      
      saddr.sin_family = AF_INET;
      saddr.sin_port = htons(port);
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
      newworker.serv_port = port;
      newworker.miner.pid = 0;
      newworker.miner.sock = 0;
      workermap[port] = newworker;

      // Advertize new worker to bootstrap node
      bootmsg_t msg;
      pack_bootmsg(port, &msg);
      async_send(boot_sock, (char *) &msg, sizeof(msg), "BOOTMSG");
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
      if (boot_sock != 0 && FD_ISSET(boot_sock, &readset))
	{
	  unsigned char opcode;

	retry:
	  ret = read(boot_sock, &opcode, 1);
	  if (ret == 0)
	    goto retry;
	  if (ret != 1 || opcode != OPCODE_SENDPORTS)
	    std::cerr << "Invalid SENDPORTS opcode from boot node ret = "
		      << ret << " opcode = " << opcode << std::endl;
	  else
	    bootnode_update(boot_sock);
	  close(boot_sock);
	  FD_CLR(boot_sock, &readset);
	  boot_sock = 0;
	  std::cerr << "Boot socket closed" << std::endl;
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
	      break;
	    }

	  // Treat traffic from existing worker's clients
	  for (std::list<int>::iterator it2 = it->second.clients.begin();
	       it2 != it->second.clients.end(); )
	    {
	      int client_sock = *it2;
	      if (FD_ISSET(client_sock, &readset))
		{
		  ret = client_update(it->first, client_sock, numtxinblock, difficulty);
		  if (ret < 0)
		    {
		      std::cerr << "Error during client update" << std::endl;
		      close(client_sock);
		      FD_CLR(client_sock, &readset);
		      it2 = it->second.clients.erase(it2);
		    }
		  else if (ret == 1)
		    {
		      std::cerr << "Removed client from worker clients list" << std::endl;
		      close(client_sock);
		      FD_CLR(client_sock, &readset);
		      it2 = it->second.clients.erase(it2);
		    }
		  break;
		}
	      it2++;
	    }
	  
	  // Check if we have any update from an existing miner
	  int miner_sock = it->second.miner.sock;
	  if (miner_sock && FD_ISSET(miner_sock, &readset))
	    {
	      ret = miner_update(it->second, miner_sock, numtxinblock);
	      if (ret < 0)
		{
		  close(miner_sock);
		  FD_CLR(miner_sock, &readset);
		  it->second.miner.sock = 0;
		}
	      break;
	    }
	}

    }

}

