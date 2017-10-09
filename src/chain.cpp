#include "node.h"

extern blockchain_t	chain;
extern pthread_mutex_t  chain_lock;
extern workermap_t	workermap;
extern mempool_t	transpool;
extern mempool_t	pending_transpool;

// Obtain a block hash from one of the peers
bool		worker_get_hash(worker_t& worker, unsigned char next_height[32], int numtxinblock, hashmsg_t& msg)
{
  fd_set	fds;
  int		max;
  unsigned char found_hash[32];
  int		ret = 0;
  
  // Send message to ask for hash of ancestor
  FD_ZERO(&fds);
  for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
    {
      hashmsg_t msg;
      int sock = (*it);
      FD_SET(sock, &fds);
      if (sock > max)
	max = sock;
      msg.hdr.opcode = OPCODE_GETHASH;
      memcpy(msg.height, next_height, 32);
      async_send(sock, (char *) &msg, sizeof(msg), 0);
    }
  
  // Check if anyone sent us the reply in the next 5 seconds
  int nbr = 0;
  struct timeval tv;
  memset(&tv, 0x00, sizeof(tv));
  tv.tv_sec = 5;
  do {
    ret = select(max + 1, &fds, NULL, NULL, &tv);
    if (ret < 0)
      {
	std::cerr << "Chain syncing failed in select" << std::endl;
	perror("select");
	return (false);
      }
    else if (ret != 0)
      {
	for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
	  {
	    int sock = (*it);
	    if (FD_ISSET(sock, &fds))
	      {
		int len = async_read(sock, (char *) found_hash, 32, 0);
		if (len != 32)
		  FATAL("unable to async read in chain find ancestor block");
		nbr++;
	      }
	  }
      }
  }
  while (ret > 0);

  // If we had no response from any worker, just drop
  if (nbr == 0)
    return (false);

  // We had an answer but the hash differed from what expected
  block_t top = chain.top();
  if (memcmp(found_hash, top.hdr.hash, 32) != 0)
    {
      std::cerr << "Found block at desired height but hash differed - continuing" << std::endl;
      return (false);
    }

  // We received an answer with the same hash as the expected!
  std::cerr << "Found block at desired height with SAME hash!" << std::endl;
  return (true);
}


// Obtain a block data from one of the peers
bool		worker_get_block(worker_t& worker, unsigned char next_height[32], int numtxinblock, block_t& block)
{
  fd_set	fds;
  int		max;
  blockmsg_t	hdr;
  char		*transdata = (char *) malloc(numtxinblock * 128);
  int		ret;
  
  if (transdata == NULL)
    {
      std::cerr << "Failed to allocate transdata for block during chain syncing" << std::endl;
      return (false);
    }
  
  // Send message to ask for hash of ancestor
  FD_ZERO(&fds);
  for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
    {
      hashmsg_t msg;
      int sock = (*it);
      FD_SET(sock, &fds);
      if (sock > max)
	max = sock;
      msg.hdr.opcode = OPCODE_GETBLOCK;
      memcpy(msg.height, next_height, 32);
      async_send(sock, (char *) &msg, sizeof(msg), 0);
    }
  
  // Check if anyone sent us the reply in the next 5 seconds
  int nbr = 0;
  struct timeval tv;
  memset(&tv, 0x00, sizeof(tv));
  tv.tv_sec = 5;
  do {
    ret = select(max + 1, &fds, NULL, NULL, &tv);
    if (ret < 0)
      {
	std::cerr << "Block syncing failed in select 1" << std::endl;
	perror("select");
	free(transdata);
	return (false);
      }
    else if (ret != 0)
      {
	for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
	  {
	    int sock = (*it);
	    if (FD_ISSET(sock, &fds))
	      {
		int len = async_read(sock, (char *) &hdr, sizeof(hdr), 0);
		if (len != sizeof(hdr))
		  {
		    std::cerr << "Block syncing failed in read 1" << std::endl;
		    free(transdata);
		    return (false);
		  }
		int toread = numtxinblock * 128;
		len = async_read(sock, transdata, toread, 0);
		if (len != toread)
		  {
		    std::cerr << "Block syncing failed in read 2" << std::endl;
		    free(transdata);
		    return (false);
		  }
		nbr++;
	      }
	  }
      }
  }
  while (ret > 0);

  // If we had no response from any worker, just drop
  if (nbr == 0)
    return (false);

  block.hdr = hdr;
  block.trans = (transdata_t *) transdata;
  
  // We received an answer with the same hash as the expected!
  std::cerr << "Found block data at desired height" << std::endl;
  return (true);
}



// Store new block in chain
bool		chain_store(blockmsg_t msg, char *transdata, unsigned int numtxinblock, int port)
{
  pthread_mutex_lock(&chain_lock);  
  if (chain.size() == 0)
    chain_accept_block(msg, transdata, numtxinblock, port);
  else
    {
      block_t top = chain.top();
      blockmsg_t tophdr = top.hdr;
      
      if (memcmp(msg.height, tophdr.height, 32) == 0 &&
	  memcmp(msg.priorhash, tophdr.priorhash, 32) == 0)
	chain_merge_simple(msg, transdata, numtxinblock, top, port);
      
      else if (smaller_than(msg.height, tophdr.height) &&
	       memcmp(msg.priorhash, tophdr.priorhash, 32) != 0)
	chain_merge_deep(msg, transdata, numtxinblock, top, port);
      
      else if (smaller_than(tophdr.height, msg.height))
	chain_propagate_only(msg, transdata, numtxinblock, port);
    }

  pthread_mutex_unlock(&chain_lock);
  return (true);
}


// Find common ancestor on block chain using GET_HASH requests
bool			chain_sync(worker_t& worker, blockmsg_t newblock, unsigned int numtxinblock, blocklistpair_t& out)
{
  unsigned char		next_height[32];
  unsigned char		updated_height[32];
  unsigned char		*one = (unsigned char *) "00000000000000000000000000000001";
  std::list<block_t>	dropped;
  std::list<block_t>	added;
  hashmsg_t		ancestor_hash;
  block_t		ancestor_block;
  
  std::cerr << "Entering chain sync" << std::endl;

  // We start to search from the minimal height from chain and new block
  if (chain.size() == 0)
    memcpy(next_height, "00000000000000000000000000000000", 32);
  else
    {
      block_t	top = chain.top();
      memcpy(next_height, top.hdr.height, 32);
  
      // Keep searching by decreasing height while we have not found an ancestor
      bool	found = false;
      while (false == found && false == is_zero(next_height))
	{
	  found = worker_get_hash(worker, next_height, numtxinblock, ancestor_hash);
	  if (false == found)
	    {
	      string_sub(next_height, one, updated_height);
	      memcpy(next_height, updated_height, 32);
	    }
	  dropped.push_front(chain.top());
	  chain.pop();
	}
      
      // If no ancestor could be found, restore original chain and return error
      if (found == false)
	{
	  std::cerr << "Could not find hash of any ancestor block - dropping new block" << std::endl;
	  for (blocklist_t::iterator it = dropped.begin(); it != dropped.end(); it++)
	    chain.push(*it);
	  return (false);
	}
    }

  // We have an ancestor, now get block data until the top is reached
  bool finished = false;
  while (false == finished)
    {
      bool res = worker_get_block(worker, next_height, numtxinblock, ancestor_block);
      if (res == false)
	{
	  std::cerr << "Unable to obtain block data from known hash - dropping new block" << std::endl;
	  for (blocklist_t::iterator it = dropped.begin(); it != dropped.end(); it++)
	    chain.push(*it);
	  return (false);
	}
      added.push_back(ancestor_block);
      if (memcmp(next_height, newblock.height, 32))
	finished = true;
      else
	{
	  string_add(next_height, one, updated_height);
	  memcpy(next_height, updated_height, 32);
	}
    }

  // We found all the blocks until the top. Push added blocks to the chain
  for (blocklist_t::iterator it = added.begin(); it != added.end(); it++)
    chain.push(*it); 
  
  std::cerr << "Succesfully synced chain" << std::endl;
  blocklistpair_t lout = std::make_pair(added, dropped);
  out = lout;
  return (true);
}


// This is what happens when the chain is currently empty and we accept a new block
bool		chain_accept_block(blockmsg_t msg, char *transdata, unsigned int numtxinblock, int port)
{
  block_t	newtop;
  miner_t&	miner = workermap[port].miner;
  blocklist_t	synced;
  blocklist_t	removed;
  blocklistpair_t bp;
  bool		res;
  
  // Kill any existing miner and push new block on chain
  if (miner.pid)
    {
      close(miner.sock);
      miner.sock = 0;
      kill(miner.pid, SIGTERM);
      transpool.insert(pending_transpool.begin(), pending_transpool.end());
      pending_transpool.clear();
      std::cerr << "Accepted block on the chain - killed miner pid " << miner.pid << " on the way " << std::endl;
    }
  else
    {
      std::cerr << "Accepted block on the chain - no miner was currently running" << std::endl;
    }

  // The block is height 0 and we have nothing on the chain: just push the block
  if (is_zero(msg.height))
    {
      newtop.hdr = msg;
      newtop.trans = (transdata_t *) transdata;
      chain.push(newtop);
      synced.push_back(newtop);
      bp = std::make_pair(synced, removed);
    }

  // The block is not height 0 and we have nothing on the chain: get all ancestor blocks
  else
    {
      res = chain_sync(workermap[port], newtop.hdr, numtxinblock, bp);
      if (res == false)
	{
	  std::cerr << "Unable to find ancestor block : dropping" << std::endl;
	  return (false);
	}
    }
  
  // Sync the transaction pool and account to reflect the new state of the chain
  trans_sync(bp.first, bp.second, numtxinblock);
  return (true);
}


// Fork at one level deep only - simple case
bool		chain_merge_simple(blockmsg_t msg, char *transdata, unsigned int numtxinblock, block_t& top, int port)
{
  block_t	newtop;
  miner_t&	miner = workermap[port].miner;

  std::cerr << "ENTERED chain merge simple" << std::endl;
  
  // This is the caase where the new block is at the same height as the top block in our chain
  // We must first pop the current block, push the new one, and settle all pending transactions between the two blocks 
  chain.pop();
  
  // Kill any existing miner and push new block on chain
  if (miner.pid)
    {
      close(miner.sock);
      miner.sock = 0;
      kill(miner.pid, SIGTERM);
      transpool.insert(pending_transpool.begin(), pending_transpool.end());
      pending_transpool.clear();
      newtop.hdr = msg;
      chain.push(newtop);
      std::cerr << "Accepted block on the chain - killed miner pid " << miner.pid << " on the way " << std::endl;
    }
  else
    {
      newtop.hdr = msg;
      chain.push(newtop);
      std::cerr << "Accepted block on the chain - no miner was currently running" << std::endl;
    }

  // Sync the transaction pool and account to reflect the new state of the chain
  blocklist_t removed;
  blocklist_t added;
  added.push_back(newtop);
  removed.push_back(top);
  trans_sync(added, removed, numtxinblock);
  return (true);
}



// Merge block chain were divergence was more than one block
bool			chain_merge_deep(blockmsg_t msg, char *transdata, unsigned int numtxinblock, block_t& top, int port)
{
  worker_t&		worker = workermap[port];
  miner_t&		miner = worker.miner;
  blocklistpair_t	bp;

  std::cerr << "ENTERED chain merge deep" << std::endl;

  // Killing any running miner
  if (miner.pid)
    {
      close(miner.sock);
      miner.sock = 0;
      kill(miner.pid, SIGTERM);
    }

  // There is no common ancestor - sync up with chain of sent block entirely
  bool found = chain_sync(worker, msg, numtxinblock, bp);
  if (found == false)
    {
      std::cerr << "Unable to find ancestor block : dropping" << std::endl;
      return (false);
    }	  

  // Synchronize transactions and accounts
  trans_sync(bp.first, bp.second, numtxinblock);
  return (true);
}


// This block is old - check if any transaction of the block is legit, if so propagate it
bool	chain_propagate_only(blockmsg_t msg, char *transdata, unsigned int numtxinblock, int port)
{
  block_t top = chain.top();
  blockmsg_t hdr = top.hdr;
  std::string newhd = tag2str(msg.height);
  std::string curhd = tag2str(hdr.height);
  
  std::cerr << "Received block of lower height : " << newhd
	    << " current is " << curhd
	    << " chain size = " << chain.size()
	    << " -  propagate only" << std::endl;


  // FIXME XXX: Propagate transaction that were legit, leave chain unchanged
  
  return (true);
}

