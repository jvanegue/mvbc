#include "node.h"

// Build a SEND_PORT message
char		*build_sendport(bootmap_t portmap, int *len)
{
  unsigned short mapsize = portmap.size();
  int		 totlen = 1 + 6 + mapsize * 6;
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
