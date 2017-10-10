#include "node.h"

extern clientmap_t	clientmap;
extern UTXO		utxomap;
extern mempool_t	transpool;
extern mempool_t	pending_transpool;

// Check if a transaction is already present in the mempool
bool		trans_exists(transmsg_t trans)
{
  std::string	transkey;

  transkey = hash_binary_to_string(trans.data.sender) +
    hash_binary_to_string(trans.data.receiver) +
    hash_binary_to_string(trans.data.amount) +
    hash_binary_to_string(trans.data.timestamp);
  
  if (transpool.find(transkey) == transpool.end() &&
      pending_transpool.find(transkey) == pending_transpool.end())
    return (false);
  
  return (true);
}


// Verify transaction and add it to the pool if correct
int		trans_verify(worker_t &worker,
			     transmsg_t trans,
			     unsigned int numtxinblock,
			     int difficulty)
{
  account_t	sender;
  account_t	receiver;

  std::cerr << "VERIFYing transaction" << std::endl;

  if (trans_exists(trans))
    {
      std::cerr << "Transaction is already in mempool - ignoring" << std::endl;
      return (0);
    }
  
  std::string mykey = hash_binary_to_string(trans.data.sender);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown sender - ignoring" << std::endl;
      return (0);
    }
  std::cerr << "Transaction has known sender - continuing" << std::endl;    
  
  sender = utxomap[mykey];
  mykey = hash_binary_to_string(trans.data.receiver);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown receiver - ignoring" << std::endl;
      return (0);
    }
  std::cerr << "Transaction has known receiver - continuing" << std::endl;    
  
  receiver = utxomap[mykey];
  if (smaller_than(sender.amount, trans.data.amount))
    {
      std::cerr << "Received transaction with bankrupt sender - ignoring" << std::endl;
      return (0);
    }

  std::string transkey = hash_binary_to_string(trans.data.sender) +
    hash_binary_to_string(trans.data.receiver) +
    hash_binary_to_string(trans.data.amount) +
    hash_binary_to_string(trans.data.timestamp);
  
  transpool[transkey] = trans;

  std::cerr << "Added transaction to mempool" << std::endl;
  
  // Send transaction to all remotes
  for (clientmap_t::iterator it = clientmap.begin(); it != clientmap.end(); it++)
    {
      remote_t remote = it->second;

      async_send(remote.client_sock, (char *) &trans,
		 sizeof(transmsg_t), "Send transaction on remote");
      
      std::cerr << "Sent transaction to remote port " << remote.remote_port << std::endl;
    }

  // Start mining if transpool contains enough transactions to make a block
  if (transpool.size() == numtxinblock)
    {
      std::cerr << "Block is FULL " << numtxinblock << " - starting miner" << std::endl;
      do_mine_fork(worker, difficulty, numtxinblock);

      pending_transpool.insert(transpool.begin(), transpool.end());

      // After some time we forget the transactions...
      if (pending_transpool.size() > numtxinblock * 10)	{
	pending_transpool.clear();
	pending_transpool.insert(transpool.begin(), transpool.end());
      }
      transpool.clear();
    }
  else
    std::cerr << "Block is not FULL - keep listening" << std::endl;
  
  return (0);      
}


// Remove all duplicate transactions from the current pool after a chain syncing
// Input: The list of blocks that were pushed on the chain and  the list that was removed
// Return: The number of duplicate transactions removed from the pool
int		trans_sync(blocklist_t added, blocklist_t removed, unsigned int numtxinblock)
{
  int erased = 0;
  
  // Go over the added blocks and remove all duplicate transactions from the transpool
  for (blocklist_t::iterator it = added.begin(); it != added.end(); it++)
    {
      block_t& curblock = *it;
  
      // Remove all the transactions from the mempool that were already integrated via the new block
      unsigned int erased = 0;
      for (unsigned int idx = 0; idx < numtxinblock; idx++)
	{
	  transdata_t *curdata = curblock.trans + idx;
	  std::string transkey = hash_binary_to_string(curdata->sender) +
	    hash_binary_to_string(curdata->receiver) +
	    hash_binary_to_string(curdata->amount) +
	    hash_binary_to_string(curdata->timestamp);
	  
	  if (transpool.find(transkey) != transpool.end())
	    {
	      transpool.erase(transkey);
	      erased++;
	    }
	}
    }

  // Go over the removed blocks and revert all transactions
  for (blocklist_t::iterator it = removed.begin(); it != removed.end(); it++)
    {
      block_t curblock = *it;  

      for (unsigned int idx = 0; idx < numtxinblock; idx++)
	{
	  transdata_t *curdata = curblock.trans + idx;
	  std::string transkey = hash_binary_to_string(curdata->sender) +
	    hash_binary_to_string(curdata->receiver) +
	    hash_binary_to_string(curdata->amount) +
	    hash_binary_to_string(curdata->timestamp);
	  
	  if (transpool.find(transkey) != transpool.end())
	    {
	      transpool.erase(transkey);
	      erased++;
	    }
	}
      
    }
  
  std::cerr << "accept block: CLEANED transpool after creating chain (" << erased << " erased and " << transpool.size() << " trans left)" << std::endl;
  
  return (0);
}


