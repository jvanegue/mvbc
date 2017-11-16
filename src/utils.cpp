#include "node.h"

// Network lock
extern pthread_mutex_t  sockmap_lock;
extern sockmap_t	rsockmap;
extern sockmap_t	wsockmap;

//pthread_mutex_t		net_lock = PTHREAD_MUTEX_INITIALIZER;

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


// Compare two integers stored on 32B arrays
bool	smaller_than(unsigned char first[32], unsigned char second[32])
{
  int index;

  std::string fst = tag2str(first);
  std::string snd = tag2str(second);
  
  for (index = 0; index < 32; index++)
    if (first[index] == second[index])
      continue;
    else if (first[index] < second[index])
      {
	//std::cerr << fst << " IS SMALLER THAN " << snd << std::endl;
	return (true);
      }
    else
      {
	//std::cerr << fst << " IS NOT SMALLER THAN " << snd << std::endl;
	return (false);
      }

  // Happen when both values are equal
  //std::cerr << "FST = " << fst << std::endl;
  //std::cerr << "SND = " << snd << std::endl;
  return (false);
}


// Return true if value is zero, false if not
bool	is_zero(unsigned char tag[32])
{
  for (int idx = 0; idx < 32; idx++)
    if (tag[idx] != '0')
      return (false);
  return (true);
}



// Sub integers encoded with 32B arrays
void	string_sub(unsigned char sender_amount[32],
		   unsigned char amount_to_sub[32],
		   unsigned char *output)
{
  unsigned char	carry = 0;
  for (int index = 31; index >= 0; index--)
    {
      unsigned char cursender = sender_amount[index] - '0';
      unsigned char curtosub  = amount_to_sub[index] + carry - '0';
      if (cursender < curtosub)
	{
	  carry = 1;
	  cursender += 10;
	}
      else
	carry = 0;
   
      unsigned int  result    = (cursender - curtosub);

      if (result > 9)
	std::cerr << "sub result is bigger than 10 - this should never happen!" << std::endl;
      
      output[index] = result + '0';
    }
  
  //if (carry)
  //std::cerr << "WARNING: SUB Account amount underflow (not enough funds)" << std::endl;

}

// Translate the 32B array representation into a C++ string
std::string	tag2str(unsigned char str[32])
{
  unsigned char str_null[33];

  memcpy(str_null, str, 32);
  str_null[32] = 0x00;

  const std::string realstr((const char *) str_null);

  return (realstr);

}


// Print wallet information
void	wallet_print(const char *prefix,
		     unsigned char sender[32],
		     unsigned char amount[32],
		     unsigned char receiver[32])
{
  unsigned char sender_null[33];
  unsigned char receiver_null[33];
  unsigned char amount_null[32];

  memcpy(sender_null, sender, 32);
  memcpy(receiver_null, receiver, 32);
  memcpy(amount_null, amount, 32);
  
  sender_null[32] = 0x00;
  receiver_null[32] = 0x00;
  amount_null[32] = 0x00;

  const std::string	prefix_str(prefix);
  std::string		sender_str((const char *) sender_null);
  std::string		receiver_str((const char *) receiver_null);
  std::string		amount_str((const char *) amount_null);
  
  std::cerr << prefix_str << "SENDER AMOUNT " << sender_str
	    << " to transfer " << amount_str << " to RECEIVER CURAMOUNT: "
	    << receiver_str << std::endl;
}


// Addition over integers encoded on 32B arrays
void	string_add(unsigned char sender_amount[32],
		   unsigned char amount_to_add[32],
		   unsigned char *output)
{
  unsigned char	carry = 0;
  for (int index = 31; index >= 0; index--)
    {
      unsigned char cursender = sender_amount[index] - '0';
      unsigned char curtoadd  = amount_to_add[index] - '0';
      unsigned int  result    = (cursender + curtoadd) + carry;
      if (result > 9)
	{
	  carry = 1;
	  result = result - 10;
	}
      else
	carry = 0;
      output[index] = '0' + result;
    }
  
  //if (carry)
  //std::cerr << "WARNING: ADD Account amount overflow! (wallet is full)" << std::endl;
  
}

// Increment integer in 32B encoding 
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


// Perform asynchronous send with retry until socket is ready
int	async_send(int fd, char *buff, int len, const char *errstr, bool verb)
{
  //int	offset = 0;

  // If there is already data pending, just append to it instead of trying to send
  pthread_mutex_lock(&sockmap_lock);
  if (wsockmap.find(fd) != wsockmap.end())
    {
      std::string str = wsockmap[fd];
      str.append(buff, len);
      wsockmap[fd] = str;
      pthread_mutex_unlock(&sockmap_lock);

      if (verb)
	std::cerr << "data to send was cached in outbound wsockmap" << std::endl;
      
      return (0);
    }
  pthread_mutex_unlock(&sockmap_lock);
  
  //std::cerr << "Acquiring net lock..." << std::endl;
  //pthread_mutex_lock(&net_lock);
  //std::cerr << "Acquired net lock..." << std::endl;
  
  int sent = send(fd, buff, len, 0);
  if (sent < 0)
    {
      //std::cerr << std::string(errstr) << " FAILED at sending data after "
      // << offset << " bytes when expected was " << len << std::endl;
      //usleep(100);

      pthread_mutex_lock(&sockmap_lock);
      std::string str("");
      str.append(buff, len);
      wsockmap[fd] = str;

      if (verb)
	std::cerr << "data to send was cached in outbound wsockmap after failed send" << std::endl;
      
      pthread_mutex_unlock(&sockmap_lock);
      sent = 0;
    }
  else if (sent != len)
    {

      pthread_mutex_lock(&sockmap_lock);
      std::string str("");
      str.append(buff + sent, len - sent);
      wsockmap[fd] = str;

      if (verb)
	std::cerr << "data to send was partially cached in outbound wsockmap" << std::endl;
      
      pthread_mutex_unlock(&sockmap_lock);
      // offset += sent;
    }

  // We sent everything - cleanup rsockmap for this socket
  else
    {
      pthread_mutex_lock(&sockmap_lock);

      if (verb)
	std::cerr << "data was fully sent - closing sock" << std::endl;
      
      wsockmap.erase(fd);
      pthread_mutex_unlock(&sockmap_lock);
    }
  
  //pthread_mutex_unlock(&net_lock);
  //std::cerr << "Released net lock..." << std::endl;
    
  return (sent);
}



// Perform asynchronous read and retry until socket is ready
int	async_read(int fd, char *buff, int len, const char *errstr)
{
  int	offset = 0;

  //std::cerr << "Acquiring net lock..." << std::endl;
  //pthread_mutex_lock(&net_lock);
  //std::cerr << "Acquired net lock..." << std::endl;
  
 retry:
  int rd = read(fd, buff + offset, len - offset);
  //  if (rd < 0 && errno == EINTR)
  //  goto retry;
  //if (rd < 0 && (errno == EGAIN || errno == EWOULDBLOCK)
  if (rd == 0 && errno != EWOULDBLOCK)
    return (0);
  
  if (rd < 0)
    {
      // here add to the send cache and return
      goto retry;
    }
  if (rd != len - offset)
    {
      offset += rd;
      // here add to the send cache and return
      goto retry;
    }

  //pthread_mutex_unlock(&net_lock);
  //std::cerr << "Released net lock..." << std::endl;
    
  return (len);
}
