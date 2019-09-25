#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>

#include "sircs.h"
#include "debug.h"
#include "irc-proto.h"


void usage() {
  fprintf(stderr, "sircs [-h] [-D debugLevel] <port>\n");
  exit(-1);
}


int main(int argc, char *argv[] ){
  extern char *optarg;
  extern int optind;
  int ch;
    
  while ((ch = getopt(argc, argv, "hD:")) != -1)
    switch (ch){
      case 'D':
        if (set_debug(optarg))
          exit(-1);
        break;
      case 'h':
        default: /* FALLTHROUGH */
          usage();
    }
  
  argc -= optind;
  argv += optind;

  if (argc < 1) {
    usage();
  }
  
  char* endArg0Ptr;
  unsigned long portLong = strtoul(argv[0], &endArg0Ptr, 0);
  
  // port check: no conversion at all, extra junk at the end, out of range
  if (argv[0] == endArg0Ptr || *endArg0Ptr != '\0' ||
      portLong < 1024 || portLong > 65535){
    fprintf(stderr,
            "Invalid port %s, please provide integer in 1024-65535 range\n",
     				argv[0]);
  	exit(-1);
  }
  
  uint16_t port = (uint16_t) portLong;
    
  DPRINTF(DEBUG_INIT, "Simple IRC server listening on port %d for new users\n",
      port);

  /* Start your engines here! */
  
  // void handleLine(...);
    
  return 0;
}

