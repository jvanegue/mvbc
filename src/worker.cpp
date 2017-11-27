#include "node.h"

// These maps contains all the worker, clients and accounts
fd_set		readset;
fd_set		writeset;
workermap_t	workermap;
clientmap_t	clientmap;
UTXO		utxomap;

// Current transaction pool and past pool (already committed)
mempool_t	transpool;
mempool_t	past_transpool;
pthread_mutex_t transpool_lock = PTHREAD_MUTEX_INITIALIZER;

// The block chain is also indexed in a map for faster access by height
blockchain_t	chain;
pthread_mutex_t chain_lock = PTHREAD_MUTEX_INITIALIZER;

// The block map - not under any lock right now
blockmap_t	bmap;

// Some global timers for statistics purpose - no lock
time_t		time_first_block = 0;
time_t		time_last_block = 0;

// For multithread implementation
threadmap_t	tmap;
jobqueue_t	jobq;
pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t	job_cond = PTHREAD_COND_INITIALIZER;

// Read and Write caches
pthread_mutex_t sockmap_lock = PTHREAD_MUTEX_INITIALIZER;
sockmap_t	rsockmap;
sockmap_t	wsockmap;

// Helper function to reset socket readset for selects
static int reset_fdsets(int boot_sock)
{
  int max = 0;

  FD_ZERO(&writeset);
  FD_ZERO(&readset);  
  if (boot_sock != 0)
    {
      FD_SET(boot_sock, &readset);
      max = boot_sock;
    }
  
  for (workermap_t::iterator it = workermap.begin(); it != workermap.end(); it++)
    {
      int cursock = it->second.serv_sock;
      if (cursock > max)
	max = cursock;

      FD_SET(cursock, &readset);
      for (std::list<int>::iterator cit = it->second.clients.begin();
	   cit != it->second.clients.end(); cit++)
	{
	  int cursock = *cit;

	  if (rsockmap.find(cursock) == rsockmap.end())
	    {
	      //std::cout << "worker clients read sock FD_SET " << cursock << std::endl;
	      FD_SET(cursock, &readset);
	      if (cursock > max)
		max = cursock;  
	    }

	  if (wsockmap.find(cursock) != wsockmap.end())
	    {
	      FD_SET(cursock, &writeset);
	      if (cursock > max)
		max = cursock;  
	    }	  
	}
    }
  
  for (clientmap_t::iterator cit = clientmap.begin(); cit != clientmap.end(); cit++)
    {
      int cursock = cit->second.client_sock;
      if (rsockmap.find(cursock) == rsockmap.end())
	{
	  if (cursock > max)
	    max = cursock;
	  //std::cout << "clientmap read sock FD_SET " << cursock << std::endl;
	  FD_SET(cursock, &readset);
	}
      if (wsockmap.find(cursock) != wsockmap.end())
	{
	  FD_SET(cursock, &writeset);
	  if (cursock > max)
	    max = cursock;
	}
    }
  max = max + 1;

  //std::cerr << "Recomputed select sets" << std::endl;
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

  std::cerr << "Connecting client on port " << port << " socket " << client_sock << std::endl;
  
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

      // is this correct?
      /**/
      if (workermap.find(port) != workermap.end())
	{
	  std::cerr << "PORT " << port << " is a local worker - do not establish remote"
		    << std::endl;
	}
      else
	// is this correct?
	/**/
      
	if (it == clientmap.end())
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
	    << port << " srv socket = " << worker.serv_sock << std::endl;

  memset(&client, 0x00, sizeof(client));
  clen = 0;
  int			csock = accept(worker.serv_sock, (struct sockaddr *) &client, &clen);

  if (csock < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
	{
	  FATAL("Failed accept on worker socket : EAGAIN/WOULDBLOCK");
	  return (0);
	}
      
      std::cerr << "Failure to accept on socket: " << worker.serv_sock << std::endl;
      FATAL("Failed accept on worker server socket");
    }

  worker.clients.push_back(csock);
  //worker_zero_state(worker);

  std::cerr << "worker update: accepted conx. Adding socket " << csock << " port " << port
	    << " to worker.clients list, now has " << worker.clients.size() << " elms" << std::endl;
  
  workermap[port] = worker;
  return (0);
}


// Treat event when new block was mined
static int	miner_update(worker_t *worker, blockmsg_t newblock, char *data, int numtxinblock)
{
  std::cerr << "MINER READ!" << std::endl;
  
  // Send block to all remotes
  // This comes from a local miner so there is no verification to perform
  for (clientmap_t::iterator it = clientmap.begin(); it != clientmap.end(); it++)
    {
      remote_t	remote = it->second;
      char	c = OPCODE_SENDBLOCK;

      if (worker->serv_port == remote.remote_port)
	{
	  std::cerr << "Do not send the block to yourself - passing" << std::endl;
	  continue;
	}
      std::cerr << "Sending block to remote on sock " << remote.client_sock
		<< " no port " << remote.remote_port << std::endl;
      async_send(remote.client_sock, &c, 1,
		 "Miner update", false);
      async_send(remote.client_sock, (char *) &newblock, sizeof(newblock),
		 "Miner update 2", false);
      async_send(remote.client_sock, (char *) data, sizeof(transdata_t) * numtxinblock,
		 "Miner update 3", false);
    }

  // Make sure nobody can touch the chain while we execute transaction and stack new block
  //std::cerr << "Acquiring chain lock..." << std::endl;
  pthread_mutex_lock(&chain_lock);
  //std::cerr << "Acquired chain lock..." << std::endl;

  // Transactions are marked as past instead of pending
  //std::cerr << "Acquiring trans lock..." << std::endl;
  pthread_mutex_lock(&transpool_lock);
  //std::cerr << "Acquired trans lock..." << std::endl;

  // Always clean past transpool before it gets too big
  // e.g. past transpool contains only most recent past block
  past_transpool.clear();
  past_transpool.insert(worker->miner.pending.begin(), worker->miner.pending.end());
  worker->miner.pending.clear();

  //std::cerr << "Releasing translock..." << std::endl;
  pthread_mutex_unlock(&transpool_lock);
  
  // Execute all transactions of the block
  trans_exec((transdata_t *) data, numtxinblock, false);

  // Some debug
  std::string hash  = hash2str(newblock.hash);
  std::string phash = hash2str(newblock.priorhash);
  std::cerr << "Miner pushing new block: " << std::endl
	    << " new top hash       = " << hash << std::endl
	    << " new top prior hash = " << phash << std::endl;
  
  // Create block and push it on chain
  block_t    chain_elem;
  chain_elem.hdr = newblock;
  chain_elem.trans = (transdata_t *) data;
  chain.push(chain_elem);
  std::string height = tag2str(newblock.height);
  bmap[height] = chain_elem;

  // Statistics on performance
  time_t curtime;
  time(&curtime);
  if (time_first_block == 0)
    time_first_block = curtime;
  if (time_last_block == 0)
    time_last_block = curtime;
  double since_first_block = difftime(curtime, time_first_block); 
  double since_last_block  = difftime(curtime, time_last_block);
  time_last_block = curtime;
  std::string curheight = tag2str(newblock.height);
  std::cerr << "CHAIN/ACCOUNTS UPDATE : new current height = " << curheight
	    << " on port " << worker->serv_port
	    << " SEC_SINCE_LAST:  " << since_last_block
	    << " SEC_SINCE_FIRST: " << since_first_block
	    << std::endl;
  std::cerr << "STATS:" << curheight << "," << since_first_block << std::endl;

  // Done updating the chain
  //std::cerr << "Releasing chain lock..." << std::endl;
  pthread_mutex_unlock(&chain_lock);
  
  return (0);
}

// Perform the action of mining. Write result on socket when available
static int	do_mine_hash(char *buff, int len, int difficulty, char *hash)
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
int		do_mine(worker_t *worker, int difficulty, int numtxinblock)
{
  miner_t	newminer;
  
  newminer.tid = pthread_self();

  //std::cerr << "Acquiring trans lock..." << std::endl;
  pthread_mutex_lock(&transpool_lock);
  //std::cerr << "Acquired trans lock..." << std::endl;
  newminer.pending.insert(transpool.begin(), transpool.end());
  transpool.clear();
  //std::cerr << "Releasing transpool lock..." << std::endl;
  pthread_mutex_unlock(&transpool_lock);

  worker->miner = newminer;
  
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
  for (mempool_t::iterator it = newminer.pending.begin(); it != newminer.pending.end(); it++)
    {
      transmsg_t cur = it->second;
      transdata_t curdata = cur.data;
      
      memcpy(buff + off, &curdata, sizeof(curdata));
      off += sizeof(transdata_t);
    }
  
  // Mine
  while (do_mine_hash(buff, len, difficulty, (char *) hash) < 0)
    string_integer_increment((char *) data->nonce, sizeof(data->nonce));	      
  memcpy(newblock.hash, hash, sizeof(newblock.hash));
  
  std::cerr << "WORKER on port " << worker->serv_port << " MINED BLOCK!" << std::endl;
  
  miner_update(worker, newblock, ((char *) buff) + sizeof(blockhash_t), numtxinblock);
  worker->miner.tid = 0;
  
  // Return to main loop
  return (0);
}


// Go ahead and listen to new requests
static int	client_update_new(worker_t *worker, int client_sock,
				  unsigned int numtxinblock, int difficulty)
{
  char		blockheight[32];
  std::string	height;
  transmsg_t	trans;
  transdata_t	data;
  unsigned char opcode;
  blockmsg_t	block;
  char		*transdata = NULL;
  block_t	blk;
  time_t	ltime;
  char		*ts;
  
  int len = async_read(client_sock, (char *) &opcode, 1, "READ opcode in client update failed");
  if (len == 0)
    {
      std::cerr << "** async_read opcode returned 0: will return -1 and close socket " << client_sock << std::endl;
      return (-1);
    }
  else if (len != 1)
    FATAL("FAILED client update read");


  std::string topheight;
  std::string topprior;
  std::string tophash;
  
  switch (opcode)
    {

      // Send transaction opcode
    case OPCODE_SENDTRANS:
      //std::cerr << "SENDTRANS OPCODE " << transpool.size() << std::endl;
      len = async_read(client_sock, (char *) &data, sizeof(data), "SENDTRANS read failed");
      if (len != (int) sizeof(data))
      	FATAL("Not enough bytes in SENDTRANS message");
      trans.hdr.opcode = OPCODE_SENDTRANS;
      trans.data = data;
      trans_verify(worker, trans, numtxinblock, difficulty);
      return (0);
      break;

      // Send block opcode
    case OPCODE_SENDBLOCK:

      time(&ltime);
      ts = ctime(&ltime);      
      std::cerr << "[" << ts << "] SENDBLOCK OPCODE " << std::endl;
      
      len = async_read(client_sock, (char *) &block, sizeof(block), "sendblock read (1)");
      if (len != (int) sizeof(block))
      	FATAL("Not enough bytes in SENDBLOCK message 1");
      transdata = (char *) malloc(numtxinblock * 128);
      if (transdata == NULL)
	FATAL("SENDBLOCK malloc");
      len = async_read(client_sock, (char *) transdata, numtxinblock * 128, "sendblock read (2)");
      if (len != (int) numtxinblock * 128)
      	FATAL("Not enough bytes in SENDBLOCK message 2");
      chain_store(block, transdata, numtxinblock, worker->serv_port);
      return (0);
      break;

      // Get block opcode
    case OPCODE_GETBLOCK:

      time(&ltime);
      ts = ctime(&ltime);      
      std::cerr << "[" << ts << "] GETBLOCK OPCODE " << std::endl;
      
      len = async_read(client_sock, blockheight, sizeof(blockheight), "GETBLOCK read failed");
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETBLOCK message");
      height = tag2str((unsigned char *) blockheight);
      if (bmap.find(height) == bmap.end())
	{
	  std::cerr << "GETBLOCK: Did not find block at desired height " << height << std::endl;
	  return (0);
	}
      blk = bmap[height];
      
      topheight = tag2str(blk.hdr.height);
      topprior  = hash2str(blk.hdr.priorhash);
      tophash   = hash2str(blk.hdr.hash);
      
      std::cerr << "OPCODE GETBLOCK with " << std::endl
		<< " topheight   = " << topheight << std::endl
		<< " tophash     = " << tophash << std::endl
		<< " topprior    = " << topprior << std::endl
		<< std::endl;

      opcode = OPCODE_SENDBLOCK;
      async_send(client_sock, (char *) &opcode, 1, "GETBLOCK send 1", false);
      async_send(client_sock, (char *) &blk.hdr, sizeof(blk.hdr), "GETBLOCK send 2", false);
      async_send(client_sock, (char *) blk.trans, sizeof(transdata_t) * numtxinblock,
		 "GETBLOCK send 2", false);
      std::cerr << "GETBLOCK SENT ANSWER" << std::endl;
      return (0);
      break;

      // Get hash opcode
    case OPCODE_GETHASH:

      time(&ltime);
      ts = ctime(&ltime);
      std::cerr << "[" << ts << "] GETHASH OPCODE " << std::endl;
      
      len = async_read(client_sock, blockheight, sizeof(blockheight), "GETHASH read failed");
      if (len != sizeof(blockheight))
	FATAL("Not enough bytes in GETHASH message");
      height = tag2str((unsigned char *) blockheight);
      std::cerr << "GETHASH requested height " << height << std::endl;
      if (bmap.find(height) == bmap.end())
	{
	  std::cerr << "GETHASH: Did not find block at desired height" << std::endl;
	  return (0);
	}
      blk = bmap[height];
      std::cerr << "GETHASH SENDING: " << hash2str(blk.hdr.hash) << std::endl;      
      async_send(client_sock, (char *) blk.hdr.hash, 32, "GETHASH send", false);
      std::cerr << "GETHASH SENT ANSWER" << std::endl;
      return (0);
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


// Treat traffic from existing worker's clients
// Could be transaction or block messages usually
static int	client_update(worker_t *worker, int client_sock,
			      unsigned int numtxinblock, int difficulty)
{
  int		ret = -1;
  bool		res;
  
  /* This update came from a remote, there is no worker associated to it */
  if (worker == NULL)
    {
      ret = client_update_new(NULL, client_sock, numtxinblock, difficulty);
      return (ret);
    }
  
  switch (worker->state.chain_state)
    {
    case CHAIN_READY_FOR_NEW:
      memset(&worker->state, 0x00, sizeof(worker->state));
      ret = client_update_new(worker, client_sock, numtxinblock, difficulty);
      break;
    case CHAIN_WAITING_FOR_HASH:
      std::cerr << "client_update: UPDATE GETHASH state" << std::endl;
      res = chain_gethash(worker, client_sock, numtxinblock, difficulty);
      if (res) ret = 0;
      break;
    case CHAIN_WAITING_FOR_BLOCK:
      std::cerr << "client_update: UPDATE GETBLOCK state" << std::endl;
      res = chain_getblock(worker, client_sock, numtxinblock, difficulty);
      if (res) ret = 0;
      break;
    default:
      std::cerr << "Chain: unknown state" << std::endl;
    }  

  return (ret);
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




static void		worker_socket_update(worker_t* worker, int client_sock, int numtxinblock, int difficulty)
{
  
  // Treat traffic outbound when sockets can be sent more data and we have data waiting
  if (FD_ISSET(client_sock, &writeset) && wsockmap.find(client_sock) != wsockmap.end())
    {
      //std::cerr << "Unblocked on writable client sock " << client_sock << std::endl; 
      std::string tosend = wsockmap[client_sock];
      int sent = send(client_sock, tosend.c_str(), tosend.size(), 0);
      if (sent < 0)
	sent = 0;
      else if (sent != (int) tosend.size())
	wsockmap[client_sock] = tosend.substr(sent, tosend.size());
      else
	wsockmap.erase(client_sock);
    }
  
  // debug only
  /*
  else if (FD_ISSET(client_sock, &writeset))
    std::cout << "client_sock " << client_sock
	      << " is SET but wsockmap indicates no data to send" << std::endl;
  */
  
  // Treat sockets for read
  // Dont create job if rsockmap is already market for that client
  if (FD_ISSET(client_sock, &readset) && rsockmap.find(client_sock) == rsockmap.end())
    {
      //std::cerr << "Unblocked on readable client sock " << client_sock << std::endl;
      job_t	job;
      ctx_t	ctx;
      ctx.worker = worker;
      ctx.sock = client_sock;
      ctx.numtxinblock = numtxinblock;
      ctx.difficulty = difficulty;
      job.context = ctx;
      pthread_mutex_lock(&job_lock);
      jobq.push(job);
      pthread_mutex_unlock(&job_lock);
      //pthread_cond_broadcast(&job_cond);
      rsockmap[client_sock] = "ready";
      //std::cerr << "Queued job" << std::endl;
    }
  
  // debug only
  /*
  else if (FD_ISSET(client_sock, &readset))
    std::cerr << "client_sock " << client_sock
	      << " is SET but rsockmap already marked for it" << std::endl;
  */
}




// Main procedure for node in worker mode
void	  execute_worker(unsigned int numtxinblock, unsigned int difficulty,
			 unsigned int numworkers, unsigned int numcores, std::list<int> ports)
{
  int	  err = 0;
  int     boot_sock;
  int	  serv_sock;
  struct sockaddr_in caddr;
  int     max;
  int	  flags;
  
  std::cout << "Executing in worker mode" << std::endl;

  UTXO_init();
  FD_ZERO(&readset);

  if (numcores == 0)
    numcores = 1;
  
  // Connect to bootstrap node
  boot_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (boot_sock < 0)
    FATAL("socket");

  std::cout << "Bootsock " << boot_sock << std::endl;
  
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

      std::cerr << "Worker now binding server socket " << serv_sock
		<< " on port " << port << std::endl;
      
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
      newworker.miner.tid = 0;
      newworker.state.added = NULL;
      newworker.state.dropped = NULL;
      worker_zero_state(newworker);      
      workermap[port] = newworker;

      // Advertize new worker to bootstrap node
      bootmsg_t msg;
      pack_bootmsg(port, &msg);
      async_send(boot_sock, (char *) &msg, sizeof(msg), "BOOTMSG", false);
    }

  // Create all threads
  for (unsigned int idx = 0; idx < numcores; idx++)
    thread_create();
  
  // Listen to all incoming traffic
  while (1)
    {
      
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 1;
      
      // Reset the read set
      max = reset_fdsets(boot_sock);
      int ret = 0;

      //std::cerr << "Calling select with max = " << max << std::endl;
      
      do { ret = select(max, &readset, &writeset, NULL, &tv); }
      while (ret == -1 && errno == EINTR);
      if (ret == 0)
	continue;
      if (ret == -1)
	FATAL("select");

      //std::cout << "Unblocked with select ret = " << ret << std::endl;
      
      // Will possibly update the remote map if boot node advertize new nodes
      if (boot_sock != 0 && FD_ISSET(boot_sock, &readset))
	{
	  unsigned char opcode;

	  std::cerr << "Unblocked on boot sock" << std::endl;
	  
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
	  worker_t& worker = it->second;
	  int serv_sock = worker.serv_sock;
	  if (FD_ISSET(serv_sock, &readset))
	    {

	      std::cerr << "Unblocked on server sock" << std::endl;
	      
	      ret = worker_update(it->first);
	      if (ret < 0)
		FATAL("worker_update");
	      break;
	    }

	  // Treat traffic from existing worker's clients
	  for (std::list<int>::iterator it2 = worker.clients.begin();
	       it2 != worker.clients.end(); it2++)
	    {
	      int client_sock = *it2;
	      //std::cerr << "Acquiring sockm lock..." << std::endl;
	      pthread_mutex_lock(&sockmap_lock);
	      //std::cerr << "Acquired sockm lock..." << std::endl;
	      worker_socket_update(&worker, client_sock, numtxinblock, difficulty);
	      //std::cerr << "Releasing sockm lock..." << std::endl;
	      pthread_mutex_unlock(&sockmap_lock);
	    }
	}


      // Treat traffic from remotes (GETHASH or GETBLOCK requests only)
      for (clientmap_t::iterator it = clientmap.begin();
	   it != clientmap.end(); it++)
	{
	  int client_sock = it->second.client_sock;
	  //std::cerr << "Acquiring sockm lock..." << std::endl;
	  pthread_mutex_lock(&sockmap_lock);
	  //std::cerr << "Acquired sockm lock..." << std::endl;
	  worker_socket_update(NULL, client_sock, numtxinblock, difficulty);
	  //std::cerr << "Releasing sockm lock..." << std::endl;
	  pthread_mutex_unlock(&sockmap_lock);
	}
      
      
    }

}



// Create a new thread
void		thread_create()
{
  pthread_t      thr;
  
  if (pthread_create(&thr, NULL, thread_start, NULL) != 0)
    {
      std::cerr << "Failed to create new thread" << std::endl;
      exit(-1);
    }
}

// Main thread loop - used to treat worker and miner updates
void*		thread_start(void *null)
{
  int		ret;
  job_t		next;
  
  while (true)
    {
 
      // Make sure we access the job queue under lock
      //std::cerr << "Acquiring job lock..." << std::endl;
      pthread_mutex_lock(&job_lock);
      //std::cerr << "Acquired job lock..." << std::endl;
      //pthread_cond_wait(&job_cond, &job_lock);
      if (jobq.empty())
	{
	  //std::cerr << "Empty job queue - Releasing job lock..." << std::endl;
	  pthread_mutex_unlock(&job_lock);
	  //sleep(1);
	  continue;
	}
      next = jobq.front();
      jobq.pop();

      //std::cerr << "Picked up job" << std::endl;
      //std::cerr << "Releasing job lock..." << std::endl;
      
      pthread_mutex_unlock(&job_lock);

      // Treat the job
      ret = client_update(next.context.worker, next.context.sock,
			  next.context.numtxinblock, next.context.difficulty);

      // This shows up when the socket was closed
      if (ret < 0)
	{
	  std::cerr << "Client update was a close (removing sock "
		    << next.context.sock << " from client list)" << std::endl;
	  close(next.context.sock);
	  FD_CLR(next.context.sock, &readset);
	  if (next.context.worker)
	    next.context.worker->clients.remove(next.context.sock);
	}

      // This is only for SENDPORT - we close the socket after such request
      else if (ret == 1)
	{
	  std::cerr << "Removed client from worker clients list socket "
		    << next.context.sock << std::endl;
	  close(next.context.sock);
	  FD_CLR(next.context.sock, &readset);
	  if (next.context.worker)
	    next.context.worker->clients.remove(next.context.sock);
	}
      
      // Mark this socket as not waiting for more job
      //std::cerr << "Acquiring job lock..." << std::endl;
      pthread_mutex_lock(&sockmap_lock);
      //std::cerr << "Acquired sockm lock..." << std::endl;
      rsockmap.erase(next.context.sock);
      //std::cerr << "Released sockm lock..." << std::endl;
      pthread_mutex_unlock(&sockmap_lock);

      //std::cerr << "Done with job on sock " << next.context.sock << std::endl;      
    }

  return (NULL);
}
