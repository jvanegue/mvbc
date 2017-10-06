#include "node.h"


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
	std::cerr << fst << " IS SMALLER THAN " << snd << std::endl;
	return (true);
      }
    else
      {
	std::cerr << fst << " IS NOT SMALLER THAN " << snd << std::endl;
	return (false);
      }

  // Should never happen
  std::cerr << "SMALL THAN: case should not happen - passing" << std::endl;
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
  if (carry)
    std::cerr << "WARNING: SUB Account amount underflow (not enough funds)" << std::endl;

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
  if (carry)
    std::cerr << "WARNING: ADD Account amount overflow! (wallet is full)" << std::endl;
  
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
int	async_send(int fd, char *buff, int len, const char *errstr)
{
  int	offset = 0;
  
 retry:
  int sent = send(fd, buff + offset, len - offset, 0);
  if (sent < 0)
    {
      std::cerr << std::string(errstr) << " FAILED at sending data after " << offset << " bytes when expected was " << len << std::endl;
      exit(-1);
    }
  if (sent != len - offset)
    {
      offset += sent;
      goto retry;
    }
  return (len);
}



// Perform asynchronous read and retry until socket is ready
int	async_read(int fd, char *buff, int len, const char *errstr)
{
  int	offset = 0;
  
 retry:
  int rd = read(fd, buff + offset, len - offset);
  if (rd < 0)
    {
      std::cerr << std::string(errstr) << " FAILED at reading data after " << offset << " bytes when expected was " << len << std::endl;
      exit(-1);
    }
  if (rd != len - offset)
    {
      offset += rd;
      goto retry;
    }
  return (len);
}
