#include "node.h"

extern blockmap_t	bmap;
extern blockchain_t	chain;
extern pthread_mutex_t  chain_lock;
extern workermap_t	workermap;
extern clientmap_t	clientmap;
extern mempool_t	transpool;
extern mempool_t	past_transpool;
extern pthread_mutex_t  transpool_lock;


// Store new block in chain
bool		chain_store(blockmsg_t msg, char *transdata, unsigned int numtxinblock, int port)
{
  
  //std::cerr << "Trying to acquire chain lock" << std::endl;
  pthread_mutex_lock(&chain_lock);
  //std::cerr << "Acquired chain lock" << std::endl;
  
  if (chain.size() == 0)
    {
      std::string msgstr = tag2str(msg.height);
      
      std::cerr << "Entered chain store with msgstr = " << msgstr << std::endl;
      
      chain_accept_block(msg, transdata, numtxinblock, port);
    }
  else
    {
      unsigned char incheight[32];
      block_t top = chain.top();
      blockmsg_t tophdr = top.hdr;
      
      std::string msgstr = tag2str(msg.height);
      std::string topstr = tag2str(tophdr.height);
      std::string msgprior = hash2str(msg.priorhash);
      std::string msghash  = hash2str(msg.hash);
      std::string topprior = hash2str(tophdr.priorhash);
      std::string tophash  = hash2str(tophdr.hash);
      
      std::cerr << "Entered chain store with " << std::endl
		<< " msgheight   = " << msgstr << std::endl
		<< " topheight   = " << topstr << std::endl
		<< " msghash     = " << msghash  << std::endl
		<< " msgprior    = " << msgprior << std::endl
		<< " tophash     = " << tophash << std::endl
		<< " topprior    = " << topprior << std::endl
		<< std::endl;

      memcpy(incheight, tophdr.height, 32);
      string_integer_increment((char *) incheight, 32);

      // Just one block to push immediately on the chain
      if (memcmp(incheight, msg.height, 32) == 0 &&
	  memcmp(tophdr.hash, msg.priorhash, 32) == 0)
	{
	  std::cerr << "Entered ACCEPT_BLOCK with chain size = " << chain.size() << std::endl;	  
	  chain_accept_block(msg, transdata, numtxinblock, port);
	}
      
      // Kill current top and replace with new top
      else if (memcmp(msg.height, tophdr.height, 32) == 0 &&
	  memcmp(msg.priorhash, tophdr.priorhash, 32) == 0)
	chain_merge_simple(msg, transdata, numtxinblock, top, port);

      // Disagree on ancestry, nuke top blocks until common ancestry found
      else if (memcmp(tophdr.height, msg.height, 32) == 0 &&
	       memcmp(msg.priorhash, tophdr.priorhash, 32) != 0)
	{
	  std::cerr << "Entered MERGE_DEEP because priorhash not matching" << std::endl;	  
	  chain_merge_deep(msg, transdata, numtxinblock, top, port);
	}

      // Larger delta - find ancestry
      else if (smaller_than(tophdr.height, msg.height))
	{
	  std::cerr << "Entered MERGE_DEEP because top block height is smaller" << std::endl;	  
	  chain_merge_deep(msg, transdata, numtxinblock, top, port);
	}

      // New block look old - propagate only 
      else if (smaller_than(msg.height, tophdr.height))
	chain_propagate_only(msg, transdata, numtxinblock, port);

      else
	std::cerr << "Unknown case of chain merge! \n" << std::endl;
      
    }

  //std::cerr << "Releasing chain lock" << std::endl;
  pthread_mutex_unlock(&chain_lock);

  std::cerr << "Exiting chain store" << std::endl;
  
  return (true);
}


// Obtain a block hash from one of the peers
bool		worker_send_gethash(worker_t& worker, unsigned char next_height[32])
{
  std::cerr << "ENTERED worker_send_gethash (worker.clients nbr# "
	    << worker.clients.size() << ") requesting height "
	    << tag2str(next_height) << std::endl;
  
  // Send message to ask for hash of ancestor block
  for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
    {
      hashmsg_t msg;
      int sock = (*it);
      msg.hdr.opcode = OPCODE_GETHASH;
      memcpy(msg.height, next_height, 32);
      
      int ret = async_send(sock, (char *) &msg, sizeof(msg), 0, false);

      std::cerr << "worker_send_gethash request height = " << tag2str(msg.height)
		<< " on socket " << sock << " ret = " << ret << std::endl;
      
      worker.state.chain_state = CHAIN_WAITING_FOR_HASH;
      return (true);
    }

  // Failed to find anyone to send that message to
  return (false);
}

// Obtain a block data from one of the peers
bool		worker_send_getblock(worker_t& worker, int sock)
{
  std::cerr << "ENTERED worker_send_getblock" << std::endl;
  
  // Send message to ask for the block data at given height
  for (std::list<int>::iterator it = worker.clients.begin(); it != worker.clients.end(); it++)
    {
      hashmsg_t msg;
      int sock = (*it);
      msg.hdr.opcode = OPCODE_GETBLOCK;
      memcpy(msg.height, worker.state.working_height, 32);
      
      int ret = async_send(sock, (char *) &msg, sizeof(msg), 0, true);

      std::cerr << "worker_send_getblock request height " << tag2str(msg.height)
		<< " on socket " << sock << " ret = " << ret << std::endl;
      
      worker.state.chain_state = CHAIN_WAITING_FOR_BLOCK;
      return (true);
    }

  std::cerr << "worker_send_getblock has no peer to request hash: returning false" << std::endl;
  return (false);
}


// We have a client update where we were waiting for 
bool			chain_gethash(worker_t *worker, int sock,
				      unsigned int numtxinblock, int difficulty)
{        
  unsigned char		found_hash[32];

  // Read hash data
  int len = async_read(sock, (char *) &found_hash, 32, 0);
  if (len != 32)
    {
      std::cerr << "Hash syncing failed in read" << std::endl;
      return (false);
    }

  // We had an answer but the hash differed from what expected
  block_t top = chain.top();
  if (memcmp(found_hash, top.hdr.hash, 32) != 0)
    {
      std::cerr << "WARN: get_hash: Hash differed: received " << hash2str(found_hash)
		<< " vs top: " << hash2str(top.hdr.hash) << std::endl;
      
      chain.pop();
      if (worker->state.dropped == NULL)
	worker->state.dropped = new std::list<block_t>();
      worker->state.dropped->push_front(top);
      bmap.erase(tag2str(top.hdr.height));
      
      if (chain.size() == 0)
	{
	  std::cerr << "WARN: get_hash: Hash differed and reached empty chain" << std::endl;
	  return (worker_send_getblock(*worker, sock));
	}
      else
	{
	  std::cerr << "FOUND CHAIN SIZE = " << chain.size() << std::endl;
	  string_integer_decrement((char *) worker->state.working_height, 32);
	  std::cerr << "WARN: get_hash: Hash differed, asking deeper hash at height "
		    << tag2str(worker->state.working_height) << std::endl;
	  return (worker_send_gethash(*worker, worker->state.working_height));
	}
      
    }

  // We received an answer with the same hash as the expected
  std::cerr << "chain_gethash: Found ancestor block with MATCHING hash (height "
	    << tag2str(worker->state.working_height) << ") NOW SEND GETBLOCK" << std::endl;

  // Now start getting block data starting with the bottom hash
  return (worker_send_getblock(*worker, sock));
}


// We have a client update where we were waiting for a block
bool		chain_getblock(worker_t *worker, int sock,
			       unsigned int numtxinblock, int difficulty)
{
  blockmsg_t	hdr;
  block_t	block;
  
  int len = async_read(sock, (char *) &hdr, sizeof(hdr), 0);
  if (len != sizeof(hdr))
    {
      std::cerr << "Block syncing failed in read 1" << std::endl;
      // XXX: should restore all dropped block here
      return (false);
    }
  int total = numtxinblock * 128;
  if (worker->state.recv_buff == NULL)
    {
      worker->state.recv_buff = (char *) malloc(total);
      if (worker->state.recv_buff == NULL)
	{
	  std::cerr << "chain_getblock malloc failure" << std::endl;
	  // XXX: should restore all dropped block here
	  return (false);
	}
      worker->state.recv_sz = total;
      worker->state.recv_off = 0;
    }
  int toread = worker->state.recv_sz - worker->state.recv_off;
  len = async_read(sock, worker->state.recv_buff + worker->state.recv_off, toread, 0);
  if (len < 0)
    {
      std::cerr << "chain_getblock: async_read failed " << len << " vs " << toread << std::endl;
      // XXX: should restore all dropped block here
      return (false);
    }
  if (len != toread)
    {
      std::cerr << "chain_getblock: async_read incomplete TBC " << len << " of " << toread << std::endl;
      return (true);
    }

  block.hdr = hdr;
  block.trans = (transdata_t *) worker->state.recv_buff;

  if (worker->state.added == NULL)
    worker->state.added = new std::list<block_t>();
  worker->state.added->push_back(block);

  // If we are done, sync transactions
  if (memcmp(hdr.height, worker->state.expected_height, sizeof(hdr.height)) == 0)
    {
      std::cerr << "Detected expected_height " << tag2str(hdr.height) << " syncing and returning OK" << std::endl;
      trans_sync(*worker->state.added, *worker->state.dropped, numtxinblock, true);
      worker_zero_state(*worker);
      return (true);
    }
  else
    {
      std::cerr << "HDR height = " << tag2str(hdr.height) << " expected height " << tag2str(worker->state.expected_height) << std::endl;
    }

  // Not done yet - ask for block at next height
  string_integer_increment((char *) worker->state.working_height, 32);
  return (worker_send_getblock(*worker, sock));
 
  /***** This is failure mode code  that was temporarily removed *****
  // We found all the blocks until the top. Push added blocks to the chain
  std::cerr << "Unable to obtain block data from known hash - dropping new block"
  << std::endl;
  for (blocklist_t::iterator it = dropped.begin(); it != dropped.end(); it++)
  {
  block_t& cur = *it;
  chain.push(cur);
  std::string height = tag2str(cur.hdr.height);
  bmap[height] = cur;
  }
  *************************/
}


// Find common ancestor on block chain using GET_HASH requests
bool			chain_sync(worker_t& worker, unsigned char expected_height[32])
{
  std::cerr << "ENTERED chain_sync expected height = " << tag2str(expected_height) << std::endl;

  memcpy(worker.state.expected_height, expected_height, 32);
    
  // We start to search from the minimal height from chain and new block
  if (chain.size() == 0)
    memset(worker.state.working_height, '0', 32); 
  else
    {
      block_t	top = chain.top();
      memcpy(worker.state.working_height, top.hdr.height, 32);
      string_integer_decrement((char *) worker.state.working_height, 32);
      if (worker.state.dropped == NULL)
	worker.state.dropped = new std::list<block_t>();
      worker.state.dropped->push_front(top);
      bmap.erase(tag2str(top.hdr.height));
      chain.pop();
    }

  std::cerr << "chain_sync: sending new GETHASH command" << std::endl;
  
  return (worker_send_gethash(worker, worker.state.working_height));
}


// This is what happens when the chain is currently empty and we accept a new block
bool		chain_accept_block(blockmsg_t msg, char *transdata,
				   unsigned int numtxinblock, int port)
{
  block_t	newtop;
  miner_t&	miner = workermap[port].miner;
  blocklist_t	synced;
  blocklist_t	removed;
  blocklistpair_t bp;

  std::cerr << "Entered chain accept block" << std::endl;
  
  // Kill any existing miner and push new block on chain
  if (miner.tid)
    {
      std::cout << "Killing miner tid = " << miner.tid << std::endl;
      
      pthread_kill(miner.tid, SIGTERM);
      miner.tid = 0;
      
      pthread_mutex_lock(&transpool_lock);
      transpool.insert(miner.pending.begin(), miner.pending.end());
      pthread_mutex_unlock(&transpool_lock);
      
      miner.pending.clear();
      std::cerr << "Accepted block on the chain - killed miner tid "
		<< miner.tid << " on the way " << std::endl;
      thread_create();
    }
  else
    {
      std::cerr << "Accepted block on the chain - no miner was currently running" << std::endl;
    }

  newtop.hdr = msg;
  newtop.trans = (transdata_t *) transdata;
  chain.push(newtop);
  std::string height = tag2str(newtop.hdr.height);
  bmap[height] = newtop;
  synced.push_back(newtop);
  bp = std::make_pair(synced, removed);
  trans_sync(bp.first, bp.second, numtxinblock, false);
  return (true);
}


// Fork at one level deep only - simple case
bool		chain_merge_simple(blockmsg_t msg, char *transdata,
				   unsigned int numtxinblock, block_t& top, int port)
{
  block_t	newtop;
  miner_t&	miner = workermap[port].miner;

  std::cerr << "ENTERED chain merge simple" << std::endl;
  
  // This is the caase where the new block is at the same height as the top block in our chain
  // We must first pop the current block, push the new one, and settle all pending transactions between the two blocks
  block_t oldtop = chain.top();
  chain.pop();
  std::string height = tag2str(oldtop.hdr.height);
  bmap.erase(height);
  
  // Kill any existing miner and push new block on chain
  if (miner.tid)
    {

      std::cout << "Killing miner tid = " << miner.tid << std::endl;
      
      pthread_kill(miner.tid, SIGTERM);
      miner.tid = 0;
      
      pthread_mutex_lock(&transpool_lock);
      transpool.insert(miner.pending.begin(), miner.pending.end());
      pthread_mutex_unlock(&transpool_lock);
      
      miner.pending.clear();
      thread_create();
      
      newtop.hdr = msg;
      newtop.trans = (transdata_t *) transdata;
      
      chain.push(newtop);
      std::string height = tag2str(newtop.hdr.height);
      bmap[height] = newtop;
      
      std::cerr << "Accepted block on the chain - killed miner tid "
		<< miner.tid << " on the way " << std::endl;
    }
  else
    {
      newtop.hdr = msg;
      newtop.trans = (transdata_t *) transdata;
      chain.push(newtop);
      std::string height = tag2str(newtop.hdr.height);
      bmap[height] = newtop;
      
      std::cerr << "Accepted block on the chain - no miner was currently running" << std::endl;
    }

  // Sync the transaction pool and account to reflect the new state of the chain
  blocklist_t removed;
  blocklist_t added;
  added.push_back(newtop);
  removed.push_back(top);
  trans_sync(added, removed, numtxinblock, false);
  return (true);
}


// Merge block chain were divergence was more than one block
bool			chain_merge_deep(blockmsg_t msg, char *transdata,
					 unsigned int numtxinblock, block_t& top, int port)
{
  worker_t&		worker = workermap[port];
  miner_t&		miner = worker.miner;

  std::cerr << "ENTERED chain merge deep" << std::endl;
  
  if (miner.tid)
    {
      std::cerr << "Killing miner tid = " << miner.tid << std::endl;
      
      pthread_kill(miner.tid, SIGTERM);
      miner.tid = 0;
      pthread_mutex_lock(&transpool_lock);
      transpool.insert(miner.pending.begin(), miner.pending.end());
      miner.pending.clear();
      pthread_mutex_unlock(&transpool_lock);
      thread_create();
    }
  

  // There is no common ancestor - sync up with chain of sent block entirely
  bool ret = chain_sync(worker, msg.height);
  if (ret == false)
    {
      std::cerr << "Unable to initiate chain sync : dropping" << std::endl;
      return (false);
    }	  

  return (true);
}


// This block is old - check if any transaction of the block is legit, if so propagate it
bool	chain_propagate_only(blockmsg_t msg, char *transdata,
			     unsigned int numtxinblock, int port)
{
  block_t top = chain.top();
  blockmsg_t hdr = top.hdr;
  std::string newhd = tag2str(msg.height);
  std::string curhd = tag2str(hdr.height);
  
  std::cerr << "Received block of lower height : " << newhd
	    << " current is " << curhd
	    << " chain size = " << chain.size()
	    << " -  propagate only" << std::endl;

  // If any of these transactions were already executed, send them over
  for (unsigned int idx = 0; idx < numtxinblock; idx++)
    {
      transdata_t *curdata = ((transdata_t *) transdata) + idx;
      
      std::string transkey = hash_binary_to_string(curdata->sender) +
	hash_binary_to_string(curdata->receiver) +
	hash_binary_to_string(curdata->amount) +
	hash_binary_to_string(curdata->timestamp);
      
      if (past_transpool.find(transkey) == past_transpool.end())
	continue;
      
      for (clientmap_t::iterator it = clientmap.begin(); it != clientmap.end(); it++)
	{
	  remote_t remote = it->second;

	  char opcode = OPCODE_SENDTRANS;
	  async_send(remote.client_sock, &opcode, 1, "Propagate opcode on remote", false);
	  async_send(remote.client_sock, (char *) curdata,
		     sizeof(transdata_t), "Propagate transaction on remote", false);
	  
	  //std::cerr << "Propagated trans to remote port " << remote.remote_port << std::endl;
	}
    }

  std::cerr << "Propagation completed, returning." << std::endl;
  
  return (true);
}
