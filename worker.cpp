#include "node.h"

workermap_t workermap;
clientmap_t clientmap;
UTXO	    utxomap;


// Helper function to reset socket readset for select
static int reset_readset(int boot_sock, fd_set *readset)
{
  int max = 0;

  FD_ZERO(readset);
  
  for (workermap_t::iterator it = workermap.begin(); it != workermap.end(); it++)
    {
      int cursock = it->second.serv_sock;
      if (cursock > max)
	max = cursock;
      FD_SET(cursock, readset);
      for (std::list<int>::iterator cit = it->second.clients.begin(); cit != it->second.clients.end(); cit++)
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


// Treat events on the bootstrap node socket (possibly new remotes to connect to)
static int	bootnode_update(int boot_sock)
{

  return (0);
}

// Treat events on the worker server socket (new client?)
static int	worker_update(int port)
{
  worker_t	worker = workermap[port];

  // do something
  
  return (0);
}


// Treat traffic from existing worker's clients
// Could be transaction or block messages usually
static int	client_update(int port, int client_sock)
{
  worker_t	worker = workermap[port];

  // do something

  return (0);
}


// Initialize all wallets
void	  UTXO_init()
{
  unsigned int predef;

  for (predef = 0; predef <= 100; predef++)
    {
      char buff[32];
      account_t acc;
      int	len;
      
      memset(buff, 0x00, sizeof(buff));
      memset(&acc, 0x00, sizeof(acc));
      len = snprintf(buff, sizeof(buff), "%u", predef);
      sha256((unsigned char*) buff, len, acc.addr);
      utxomap[acc] = 100000;
    }
}



// Main procedure for node in worker mode
void	  execute_worker(int numworkers, std::list<int> ports)
{
  fd_set  readset;
  int	  err = 0;
  int     boot_sock;
  int	  serv_sock;
  struct sockaddr_in caddr;
  int     max;
  
  std::cout << "Executing in worker mode" << std::endl;

  UTXO_init();
  
  FD_ZERO(&readset);

  // Connect to bootstrap node
  boot_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (boot_sock < 0)
    FATAL("socket");

  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(8888);
  caddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  
  err = connect(boot_sock, (struct sockaddr *) &caddr, sizeof(caddr));
  if (err < 0)
    FATAL("Failed to connect to boot node");
  
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
	  for (std::list<int>::iterator it2 = it->second.clients.begin(); it2 != it->second.clients.end(); it2++)
	    {
	      int client_sock = *it2;
	      if (FD_ISSET(client_sock, &readset))
		{
		  ret = client_update(it->first, client_sock);
		  if (ret < 0)
		    FATAL("client_update");
		}
	    }
	  
	}

    }

}


