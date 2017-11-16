#include "node.h"

extern clientmap_t	clientmap;
extern UTXO		utxomap;
extern mempool_t	transpool;
extern mempool_t	past_transpool;
extern pthread_mutex_t  transpool_lock;

// Check if a transaction is already present in the mempool
bool		trans_exists(worker_t *worker, transmsg_t trans)
{
  std::string	transkey;

  pthread_mutex_lock(&transpool_lock);
  
  transkey = hash_binary_to_string(trans.data.sender) +
    hash_binary_to_string(trans.data.receiver) +
    hash_binary_to_string(trans.data.amount) +
    hash_binary_to_string(trans.data.timestamp);
  
  if (transpool.find(transkey) == transpool.end() &&
      worker->miner.pending.find(transkey) == worker->miner.pending.end() &&
      past_transpool.find(transkey) == past_transpool.end())
    {
      
      pthread_mutex_unlock(&transpool_lock);
      return (false);
    }

  pthread_mutex_unlock(&transpool_lock);
  return (true);
}


// Verify transaction and add it to the pool if correct
int		trans_verify(worker_t *worker,
			     transmsg_t trans,
			     unsigned int numtxinblock,
			     int difficulty)
{
  account_t	sender;
  account_t	receiver;

  //std::cerr << "VERIFYing transaction" << std::endl;

  if (trans_exists(worker, trans))
    {
      //std::cerr << "Transaction is already in mempool - ignoring" << std::endl;
      return (0);
    }
  
  std::string mykey = hash_binary_to_string(trans.data.sender);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown sender - ignoring" << std::endl;
      return (0);
    }
  //std::cerr << "Transaction has known sender - continuing" << std::endl;    
  
  sender = utxomap[mykey];
  mykey = hash_binary_to_string(trans.data.receiver);
  if (utxomap.find(mykey) == utxomap.end())
    {
      std::cerr << "Received transaction with unknown receiver - ignoring" << std::endl;
      return (0);
    }
  //std::cerr << "Transaction has known receiver - continuing" << std::endl;    
  
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
  
  //std::cerr << "Added transaction to mempool" << std::endl;
  
  // Send transaction to all remotes
  for (clientmap_t::iterator it = clientmap.begin(); it != clientmap.end(); it++)
    {
      remote_t remote = it->second;

      async_send(remote.client_sock, (char *) &trans,
		 sizeof(transmsg_t), "Send transaction on remote", false);
      
      //std::cerr << "Sent transaction to remote port " << remote.remote_port << std::endl;
    }

  pthread_mutex_lock(&transpool_lock);
  transpool[transkey] = trans;
  pthread_mutex_unlock(&transpool_lock);
  
  // Start mining if transpool contains enough transactions to make a block
  if (transpool.size() == numtxinblock)
    {
      std::cerr << "Block is FULL " << numtxinblock << " - starting miner" << std::endl;
      do_mine(worker, difficulty, numtxinblock);
    }
  
  //else
  //std::cerr << "Block is not FULL - keep listening" << std::endl;
  
  return (0);      
}


// Remove all duplicate transactions from the current pool after a chain syncing
// Input: The list of blocks that were pushed on the chain and  the list that was removed
// Return: The number of duplicate transactions removed from the pool
int		trans_sync(blocklist_t added, blocklist_t removed, unsigned int numtxinblock)
{
  unsigned int	nbr;

  std::cerr << "TRANS SYNC with " << added.size() << " added blocks and " << removed.size() << " removed blocks " << std::endl;
  
  // Go over the removed blocks and revert all transactions
  for (blocklist_t::iterator it = removed.begin(); it != removed.end(); it++)
    {
      block_t curblock = *it;
	  
      // True == revert
      nbr = trans_exec(curblock.trans, numtxinblock, true);
      if (nbr != numtxinblock)
	std::cerr << "NOTE: Unable to revert all transactions from removed block" << std::endl;
      
      for (unsigned int idx = 0; idx < numtxinblock; idx++)
	{
	  transdata_t *curdata = curblock.trans + idx;
	  transmsg_t  msg;
	  std::string transkey = hash_binary_to_string(curdata->sender) +
	    hash_binary_to_string(curdata->receiver) +
	    hash_binary_to_string(curdata->amount) +
	    hash_binary_to_string(curdata->timestamp);

	  // If this is not already in the transpool, add it back
	  pthread_mutex_lock(&transpool_lock);

	  if (transpool.find(transkey) == transpool.end())
	    {
	      msg.hdr.opcode = OPCODE_SENDTRANS;
	      msg.data = *curdata;
	      transpool[transkey] = msg;
	    }
	  pthread_mutex_unlock(&transpool_lock);	  
	}
    }
  
  // Go over the added blocks and execute transactions
  for (blocklist_t::iterator it = added.begin(); it != added.end(); it++)
    {
      block_t& curblock = *it;
      // Execute all transactions of the block 
      nbr = trans_exec(curblock.trans, numtxinblock, false);
      if (nbr != numtxinblock)
	std::cerr << "NOTE: Unable to exec all transactions from added block" << std::endl;
	      
      // Remove all executed transactions from transpool, put them in the past pool
      for (unsigned int idx = 0; idx < numtxinblock; idx++)
	{
	  transdata_t *curdata = curblock.trans + idx;
	  transmsg_t  msg;
	  std::string transkey = hash_binary_to_string(curdata->sender) +
	    hash_binary_to_string(curdata->receiver) +
	    hash_binary_to_string(curdata->amount) +
	    hash_binary_to_string(curdata->timestamp);

	  // Remove all duplicate transactions from the transpool
	  pthread_mutex_lock(&transpool_lock);
	  if (transpool.find(transkey) != transpool.end())
	    {
	      msg = transpool[transkey];
	      transpool.erase(transkey);
	      past_transpool[transkey] = msg;
	    }
	  else
	    {
	      msg.hdr.opcode = OPCODE_SENDTRANS;
	      msg.data = *curdata;
	      past_transpool[transkey] = msg;
	    }
	  pthread_mutex_unlock(&transpool_lock);
	}
    }
  
  std::cerr << "Trans_sync success: transpool size = " << transpool.size()
	    << " past_transpool size = " << past_transpool.size() << std::endl;
  
  return (0);
}


// Execute all transactions of a block
// Input: transaction data
// Output: Number of transactions executed
int		trans_exec(transdata_t *data, int numtxinblock, bool revert)
{
  int		idx;
  int		nbr;

  std::cerr << "Execute all transactions in block (reverted = " << revert << ")" << std::endl;
  
  // Update wallets amount values
  for (nbr = idx = 0; idx < numtxinblock; idx++)
    {
      transdata_t *curtrans = &((transdata_t *) data)[idx];
      std::string sender_key = hash_binary_to_string(curtrans->sender);
      std::string receiver_key = hash_binary_to_string(curtrans->receiver);

      // Verify that the sender exists
      if (utxomap.find(sender_key) == utxomap.end())
	{
	  std::cerr << "Failed to find wallet by sender key - bad key encoding?" << std::endl;
	  continue;
	}

      // Verify that the receiver exists
      if (utxomap.find(receiver_key) == utxomap.end())
	{
	  std::cerr << "Failed to find wallet by receiver key - bad key encoding?" << std::endl;
	  continue;
	}

      //std::cerr << "Both sender and receiver are valid" << std::endl;
      
      account_t sender;
      account_t receiver;

      if (revert == false)
	{
	  sender = utxomap[sender_key];
	  receiver = utxomap[receiver_key];
	}
      else
	{
	  sender = utxomap[receiver_key];
	  receiver = utxomap[sender_key];
	}
      
      unsigned char result[32], result2[32];

      //wallet_print("Before transaction: ", sender.amount, curtrans->amount, receiver.amount);
      
      string_sub(sender.amount, curtrans->amount, result);
      string_add(receiver.amount, curtrans->amount, result2);
      memcpy(sender.amount, result, 32);
      memcpy(receiver.amount, result2, 32);

      if (revert == false)
	{
	  utxomap[sender_key] = sender;
	  utxomap[receiver_key] = receiver;
	}
      else
	{
	  utxomap[sender_key] = receiver;
	  utxomap[receiver_key] = sender;
	}
      
      //wallet_print("After transaction: ", sender.amount, curtrans->amount, receiver.amount);
      nbr++;
     }
  
  return (nbr);
}

