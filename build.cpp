#include "node.h"

// Build a SEND_PORT message
char		*pack_sendport(bootmap_t portmap, int *len)
{
  unsigned short mapsize = portmap.size();
  unsigned int	 totlen = 1 + 6 + mapsize * 6;
  char		 *reply = (char *) malloc(totlen);
  int            off = 0;
  
  if (reply == NULL)
    FATAL("malloc");
  
  reply[0] = OPCODE_SENDPORTS;
  snprintf(reply + 1, 6, "%06u", mapsize);  
  off = 7;

  for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
    {
      int port = it->first;
      snprintf(reply + off, 6, "%06u", port);
      off += 6;
    }
  
  fprintf(stderr, "SEND_PORT = %s \n", reply + 1);
  return (reply);
}


// Build the boot msg from workers to bootstrap node
void		pack_bootmsg(unsigned short port, bootmsg_t *msg)
{
  snprintf((char *) msg->port, sizeof(msg->port), "%06u", port);
  sha256((unsigned char *) "jfv47", 5, msg.addr);
}

char		*unpack_sendblock(char *buf, int len)
{
  return (NULL);
}


// Build a SEND_TRANSACTION message
char		*unpack_sendtransaction(char *buf, int len)
{
  return (NULL);
}
