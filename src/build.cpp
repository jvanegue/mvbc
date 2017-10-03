#include "node.h"

// Build a SEND_PORT message
char		*pack_sendport(bootmap_t portmap, int *len)
{
  unsigned int	 mapsize = portmap.size();
  unsigned int	 totlen = 1 + 6 + mapsize * 6;
  char		 *reply = (char *) malloc(totlen);
  int            off = 0;
  
  if (reply == NULL)
    FATAL("malloc");

  fprintf(stderr, "pack_sendport: mapsize = %u \n", mapsize);
  
  reply[0] = OPCODE_SENDPORTS;

  char buff[7];

  memset(buff, 0x00, sizeof(buff));
  snprintf(buff, sizeof(buff), "%06u", mapsize);
  memcpy(reply + 1, buff, 6);
  off = 7;

  for (bootmap_t::iterator it = portmap.begin(); it != portmap.end(); it++)
    {
      memcpy(reply + off, it->config.port, 6);
      off += 6;
    }
  
  //fprintf(stderr, "PACKED SEND_PORT = %s \n", reply);

  fprintf(stderr, "Sending portnum = ");
  unsigned int idx;
  for (idx = 0; idx < mapsize; idx++)
    {
      char buff[7];
      int off = 1 + 6 + (idx * 6);
      memset(buff, 0x00, sizeof(buff));
      memcpy(buff, reply + off, 6);
      
      fprintf(stderr, "Port %u: %03u %03u %03u %03u %03u %03u \n",
	      idx, buff[0], buff[1], buff[2], buff[3], buff[4], buff[5]);
    }
  fprintf(stderr, "\n");
  
  *len = totlen;
  return (reply);
}


// Build the boot msg from workers to bootstrap node
void		pack_bootmsg(unsigned short port, bootmsg_t *msg)
{
  char strport[7];
  snprintf((char *) strport, sizeof(strport), "%06hu", port);
  memcpy(msg->port, strport, 6);
  sha256((unsigned char *) "jfv47", 5, msg->addr);
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

