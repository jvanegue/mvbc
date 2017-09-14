#include "node.h"

clientmap_t clientmap;

void	  execute_worker(int numworkers, std::list<int> ports)
{
  fd_set  clientset;
  int	  err = 0;
  int     boot_sock;
  int	  serv_sock;
    
  std::cout << "Executing in worker mode" << std::endl;

  FD_ZERO(&clientset);

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

      // Connect to bootstrap node
      boot_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (boot_sock < 0)
	FATAL("socket");

      struct sockaddr_in caddr;

      caddr.sin_family = AF_INET;
      caddr.sin_port = htons(8888);
      caddr.sin_addr.s_addr = inet_addr("127.0.0.1");

      err = connect(boot_sock, (struct sockaddr *) &caddr, sizeof(caddr));
      if (err < 0)
	FATAL("connect");

      // Register new client
      client_t newclient;

      newclient.serv_sock = serv_sock;
      newclient.boot_sock = boot_sock;
      newclient.serv_port = (*it);
      clientmap[boot_sock] = newclient;

      // Advertize new worker to bootstrap node
      bootmsg_t msg;

      snprintf((char *) msg.port, sizeof(msg.port), "%06u", (*it));
      sha256((unsigned char *) "jfv47", 5, msg.addr);
      err = send(boot_sock, (char *) &msg, sizeof(msg), 0);
      if (err < 0)
	FATAL("send");

      FD_SET(boot_sock, &clientset);
      FD_SET(serv_sock, &clientset);
    }
  
  // Listen to all incoming traffic
  /*
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
    }
  */

}
