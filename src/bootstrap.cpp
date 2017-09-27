#include "node.h"

void			execute_bootstrap()
{
  //std::list<int>	csocks;
  bootmap_t		portmap;
  fd_set		readset;
  int			max;
  int			boot_sock;
  struct sockaddr_in	addr;
      
  std::cout << "Executing in bootstrap mode" << std::endl;

  // Establish server socket
  boot_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (boot_sock < 0)
    FATAL("socket");

  addr.sin_family = AF_INET;
  addr.sin_port = htons(8888);
  addr.sin_addr.s_addr = INADDR_ANY;

  int err = bind(boot_sock, (struct sockaddr *) &addr, sizeof(addr));
  if (err < 0)
    FATAL("bind");
  err = listen(boot_sock, 100);
  if (err < 0)
    FATAL("listen");

  // Listen to all incoming traffic
  while (1)
    {
      max = 0;
      FD_ZERO(&readset);
      FD_SET(boot_sock, &readset);
      max = boot_sock;
      
      for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
	{
	  bootclient_t& client = *it;

	  if (client.csock != 0)
	    {
	      if (client.csock > max)
		max = client.csock;
	      FD_SET(client.csock, &readset);
	    }
	}

      int ret = 0;
      do { ret = select(max + 1, &readset, NULL, NULL, NULL); }
      while (ret == -1 && errno == EINTR); 
      if (ret == -1)
	{
	  std::cerr << "Select failed with return = " << ret << std::endl;
	  FATAL("select");
	}
      
      // Detect any new clients and add them to list
      if (FD_ISSET(boot_sock, &readset))
	{
	  bootclient_t	client;
	  
	  int csock = accept(boot_sock, NULL, NULL);
	  if (csock <= 0)
	    {
	      std::cerr << "Failure during acceptance of connection from worker : ignoring" << std::endl;
	      continue;
	    }

	  // Make client socket non-blocking
	  int flags = fcntl(csock, F_GETFL, 0);
	  if (flags < 0)
	    FATAL("client_sock fcntl1");
	  int err = fcntl(csock, F_SETFL, flags | O_NONBLOCK);
	  if (err < 0)
	    FATAL("client_sock fcntl2");  
	  
	  client.csock = csock;
	  memset(&client.config, 0x00, sizeof(client.config));
	  portmap.push_back(client);

	  std::cerr << "Accepted new client socket" << std::endl;
	  
	  continue;
	}

      // Listen for traffic on any newly connected node 
      bool updated = false;
      for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
	if (it->csock != 0 && FD_ISSET(it->csock, &readset))
	  {
	    bootmsg_t	  msg;
	    int		  err = recv(it->csock, (char *) &msg, sizeof(msg), 0);
	    
	    updated = true;
	    if (err == 0 && errno == EWOULDBLOCK)
	      std::cerr << "Client socket is ready but recv returns 0" << std::endl;
	    if (err <= 0)
	      {
		char	port[7];
		int	iport;
		
		memcpy(port, it->config.port, 6);
		port[6] = 0x00;
		iport = atoi(port);
			      
		close(it->csock);
		FD_CLR(it->csock, &readset);
		it->csock = 0;
		std::cout << "Closed socket associated to port " << iport << " (kept client entry)" << std::endl;
		continue;
	      }
	    
	    fprintf(stderr, "Received PortNum = %c%c%c%c%c%c ",
		    msg.port[0], msg.port[1], msg.port[2], msg.port[3], msg.port[4], msg.port[5]);
	    fprintf(stderr, " NodeAddr = ");
	    for (int idx = 0; idx < 32; idx++)
	      {
		unsigned char c = msg.addr[idx];
		fprintf(stderr, "%u ", (unsigned int) c);
	      }
	    fprintf(stderr, "\n");
	    
	    it->config = msg;
	  }

      // Make sure all disconnected clients are removed from the read set
      if (updated == false)
	continue;

      // Advertize the new node(s) to everyone - we connect, send and close for each update per request from TA
      int len = 0;
      char *reply = pack_sendport(portmap, &len);
      for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
	{
	  int	dstsock = it->csock;
	  char	port[7];
	  int	iport;
	      
	  memcpy(port, it->config.port, 6);
	  port[6] = 0x00;
	  iport = atoi(port);

	  // This socket was previously closed, reopen it
	  if (dstsock == 0)
	    {
	      struct sockaddr_in	addr;
	      
	      dstsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	      if (dstsock < 0)
		{
		  std::cerr << "Failed to establish socket with worker on port " << iport << std::endl;
		  continue;
		}

	      std::cerr << "Opened socket " << dstsock << " to connect on port " << iport << std::endl;
	      
	      addr.sin_family = AF_INET;
	      addr.sin_port = htons(iport);
	      addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	      int err = connect(dstsock, (struct sockaddr *) &addr, sizeof(addr));
	      if (err < 0)
		{
		  std::cerr << "Failed to established socket with worker on port " << iport << std::endl;
		  continue;
		}
	      else
		std::cerr << "Connected to worker port " << iport << std::endl;
	      
	    }
	  
	  err = send(dstsock, reply, len, 0);
	  if (err < 0)
	    std::cerr << "Failed to send update to worker on port " << iport << std::endl;

	  std::cerr << "Closing socket " << dstsock << std::endl;
	  
	  close(dstsock);
	  it->csock = 0;
	}
      free(reply);
      
    }
    
}
