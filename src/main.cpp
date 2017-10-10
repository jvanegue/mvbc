#include "node.h"

// global variables
bool		bootstrap = false;
unsigned int	numworkers = 0;
unsigned int	numcores = 0;
std::list<int>	ports;
unsigned int	difficulty = 1;
unsigned int	numtxinblock = DEFAULT_TRANS_PER_BLOCK;

// Print help and exit on error
void help_and_exit(std::string msg, char *str)
{
  std::cerr << "Error : " << msg << std::endl;
  std::cerr << "Syntax: " << std::string(str) << " [-bootstrap | -numtxinblock <num> -numworkers <num> -ports <ports> -difficulty <num> -numcores <num>]"
	    << std::endl;
  exit(-1);
}

// Parse command line parameters
int parse(int argc, char **argv)
{
  int index = 1;
  bool numworkermode = false;
  bool portmode = false;
  bool numtxmode = false;
  bool difficultymode = false;
  bool numcoresmode = false;
  char *str = NULL;
  
  while (index < argc)
    {
      str = argv[index];
      if (!strcmp(str, "-bootstrap"))
	{
	  bootstrap = true;
	  return (0);
	}      
      else if (!strcmp(str, "-numtxinblock"))
	{
	  portmode = false;
	  if (numworkermode || difficultymode || numcoresmode)
	    help_and_exit("Missing parameter value", argv[0]);
	  if (numtxmode)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  numtxmode = true;
	}
      else if (!strcmp(str, "-numcores"))
	{
	  portmode = false;
	  if (numworkermode || difficultymode || numtxmode)
	    help_and_exit("Missing parameter value", argv[0]);
	  if (numtxmode)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  numcoresmode = true;
	}
      
      else if (!strcmp(str, "-numworkers"))
	{
	  portmode = false;
	  if (numworkers != 0 || numworkermode)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  if (numtxmode || difficultymode || numcoresmode)
	    help_and_exit("Invalid parameter", argv[0]);
	  numworkermode = true;
	}
      else if (!strcmp(str, "-ports"))
	{
	  if (ports.size() != 0 || portmode)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  if (numworkermode || numtxmode || difficultymode || numcoresmode)
	    help_and_exit("Invalid parameter", argv[0]);
	  portmode = true;
	}
      else if (!strcmp(str, "-difficulty"))
	{
	  portmode = false;
	  if (difficultymode)
	    help_and_exit("Multiple occurences of option is invalid", argv[0]);
	  if (numworkermode || numtxmode || numcoresmode)
	    help_and_exit("Invalid parameter", argv[0]);
	  difficultymode = true;
	}
      else if (*str >= '0' && *str <= '9')
	{
	  int num = atoi(str);
	  if (numworkermode == true)
	    {
	      numworkers = num;
	      numworkermode = false;
	    }
	  else if (portmode == true)
	    ports.push_back(num);
	  else if (difficultymode)
	    {
	      difficulty = num;
	      difficultymode = false;
	    }
	  else if (numtxmode)
	    {
	      numtxinblock = num;
	      numtxmode = false;
	    }
	  else if (numcoresmode)
	    {
	      numcores = num;
	      numcoresmode = false;
	    }
	  else
	    help_and_exit("Missing option for value", argv[0]);
	}
      else
	help_and_exit("Unknown option", argv[0]);
      index++;
    }

  if (numworkers == 0)
    help_and_exit("No worker or bootstrap specified", argv[0]);
  if (numworkers != ports.size())
    help_and_exit("Number of works should be the same as number of ports", argv[0]);
  return (0);
}


int main(int argc, char **argv)
{
  parse(argc, argv);
  if (bootstrap)
    execute_bootstrap();
  else
    execute_worker(numtxinblock, difficulty, numworkers, numcores, ports);
  return (0);
}
