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
      //int port = it->first;
      memcpy(reply + off, it->second.port, 6);
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


// Binary to String hash
std::string	hash_binary_to_string(unsigned char hash[32])
{
  const char *base = "0123456789ABCDEF";	
  std::ostringstream oss;
  int		idx;
  
  for (idx = 0; idx < 32; idx++)
    {
      unsigned char high = (hash[idx] & 0xF0) >> 4;
      unsigned char low  = (hash[idx] & 0x0F);
      oss << base[high] << base[low];
    }
  
  std::string key = oss.str();
  return (key);
}

bool	smaller_than(unsigned char first[32], unsigned char second[32])
{
  int index;

  for (index = 0; index < 32; index++)
    if (first[index] == second[index])
      continue;
    else if (first[index] < second[index])
      return (true);
    else
      return (false);      
  return (false);
}

void	string_sub(unsigned char sender_amount[32], unsigned char amount_to_sub[32], unsigned char *output)
{
  unsigned char	carry = 0;
  for (int index = 31; index < 0 ; index++)
    {
      unsigned char cursender = sender_amount[index];
      unsigned char curtosub  = amount_to_sub[index] + carry;

      if (cursender < curtosub)
	{
	  carry = 1;
	  cursender += 10;
	}
      else
	carry = 0;
   
      unsigned int  result    = (cursender - curtosub);
      output[index] = result;
    }
  if (carry)
    std::cerr << "Account amount underflow! - ignored..." << std::endl;

}


void	wallet_print(const char *prefix,
		     unsigned char sender[32],
		     unsigned char receiver[32])
{
  unsigned char sender_null[33];
  unsigned char receiver_null[33];

  memcpy(sender_null, sender, 32);
  memcpy(receiver_null, receiver, 32);
  sender_null[32] = 0x00;
  receiver_null[32] = 0x00;

  const std::string	prefix_str(prefix);
  std::string		sender_str((const char *) sender_null);
  std::string		receiver_str((const char *) receiver_null);
  
  std::cerr << prefix_str << sender_str << " " << receiver_str << std::endl;
}


void	string_add(unsigned char sender_amount[32],
		   unsigned char amount_to_add[32],
		   unsigned char *output)
{
  unsigned char	carry = 0;
  for (int index = 31; index >= 0; index--)
    {
      unsigned char cursender = sender_amount[index];
      unsigned char curtoadd  = amount_to_add[index];
      unsigned int  result    = (cursender + curtoadd) - '0';
      result = result + carry;
      if (result >= 10)
	{
	  carry = 1;
	  result = result - 10;
	}
      else
	carry = 0;
      output[index] = '0' + result;
    }
  if (carry)
    std::cerr << "Account amount overflow! - ignored..." << std::endl;
  
}

void	string_integer_increment(char *buff, int len)
{
  
  for (int index = len - 1; index >= 0; index--)
    if (buff[index] != '9')
      {
	buff[index]++;
	return;
      }
    else
      buff[index] = '0';

  std::cerr << "Account amount INC overflow! - ignored..." << std::endl;
}
