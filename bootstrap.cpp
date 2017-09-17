#include "node.h"

void			execute_bootstrap()
{
  std::list<int>	csocks;
  bootmap_t		portmap;
  fd_set		readset;
  int			max;
  int			sock;
  struct sockaddr_in	addr;
  socklen_t		clen = 0;      
  struct sockaddr_in	client;
      
  std::cout << "Executing in bootstrap mode" << std::endl;

  // Establish server socket
  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0)
    FATAL("socket");

  addr.sin_family = AF_INET;
  addr.sin_port = htons(8888);
  addr.sin_addr.s_addr = INADDR_ANY;

  int err = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
  if (err < 0)
    FATAL("bind");
  err = listen(sock, 100);
  if (err < 0)
    FATAL("listen");

  // Listen to all incoming traffic
  while (1)
    {
      max = 0;
      FD_ZERO(&readset);
      FD_SET(sock, &readset);
      max = sock;
      for (std::list<int>::iterator it = csocks.begin(); it != csocks.end(); it++)
	{
	  int cursock = *it;
	  if (cursock > max)
	    max = cursock;
	  FD_SET(cursock, &readset);
	}

      int ret = 0;
      do { ret = select(max + 1, &readset, NULL, NULL, NULL); }
      while (ret == -1 && errno == EINTR); 
      if (ret == -1)
	FATAL("select");

      if (FD_ISSET(sock, &readset))
	{
	  int csock = accept(sock, (struct sockaddr *) &client, &clen);
	  if (csock <= 0)
	    FATAL("accept");
	  csocks.push_back(csock);
	  continue;
	}

      // Listen any new node
      bool updated = false;
      for (std::list<int>::iterator it = csocks.begin(); it != csocks.end(); it++)
	if (FD_ISSET(*it, &readset))
	  {
	    bootmsg_t	msg;
	    int		err = recv(*it, (char *) &msg, sizeof(msg), 0);

	    updated = true;
	    if (err == 0)
	      {
		csocks.remove(*it);
		portmap.erase(*it);
		FD_CLR((*it), &readset);
		std::cout << "Removed socket " << (*it) << std::endl;
		//continue;
		break;
	      }
	    
	    fprintf(stderr, "Received portnum = ");
	    int idx = 0;
	    for (idx = 0; idx < 6; idx++)
	      fprintf(stderr, "\\x%02X", msg.port[idx]);
	    fprintf(stderr, " hash = ");
	    for (idx = 0; idx < 32; idx++)
	      fprintf(stderr, "\\x%02X", msg.addr[idx]);	      
	    fprintf(stderr, "\n");
	    portmap[*it] = msg;
	  }

      if (updated == false)
	continue;

      // Advertize the new node(s) to everyone
      int len = 0;
      char *reply = pack_sendport(portmap, &len);
      for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
	{
	  int dstsock = it->first;
	  int err = send(dstsock, reply, len, 0);
	  if (err < 0)
	    FATAL("send");
	}
      free(reply);
      
    }
    
}
