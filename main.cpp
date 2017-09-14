#include "node.h"

bool bootstrap = false;
unsigned int numworkers = 0;
std::list<int> ports;

void help_and_exit(std::string msg, char *str)
{
  std::cerr << "Error : " << msg << std::endl;
  std::cerr << "Syntax: " << std::string(str) << " [-bootstrap | -numworkers <num> -ports <ports>]"
	    << std::endl;
  exit(-1);
}

int parse(int argc, char **argv)
{
  int index = 1;
  bool numworkermode = false;
  bool portmode = false;
  char *str = NULL;
  
  while (index < argc)
    {
      str = argv[index];
      if (!strcmp(str, "-bootstrap"))
	{
	  bootstrap = true;
	  return (0);
	}
      else if (!strcmp(str, "-numworkers"))
	{
	  if (numworkers != 0)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  numworkermode = true;
	  portmode = false;
	}
      else if (!strcmp(str, "-ports"))
	{
	  if (ports.size() != 0)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  portmode = true;
	  numworkermode = false;
	}
      else if (*str >= '0' && *str <= '9')
	{
	  int num = atoi(str);
	  if (numworkermode == true)
	    numworkers = num;
	  else if (portmode == true)
	    ports.push_back(num);
	}
      else
	help_and_exit("Unknown option", argv[0]);
      index++;
    }


  if (numworkers == 0)
    help_and_exit("No worker or bootstrap specified", argv[0]);
  if (numworkers != ports.size())
    help_and_exit("Number of works should be the same as number of ports", argv[0]);
}


int main(int argc, char **argv)
{
  parse(argc, argv);
  if (bootstrap)
    execute_bootstrap();
  else
    execute_worker(numworkers, ports);
  return (0);
}
