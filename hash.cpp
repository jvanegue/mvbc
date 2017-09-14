#include "node.h"

int		sha256(unsigned char *buff, unsigned int len, unsigned char *output)
{
  SHA256_CTX	sha256;

  if (buff == NULL || len == 0)
    return (-1);
  
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, buff, len);
  SHA256_Final(output, &sha256);

  return (0);
}
