#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // socklen_t
#include <stdio.h>      // perror(), printf(), fprintf()
#include <stdlib.h>     // exit(), strtoul()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // close()

#define MAX_PENDING_CONNS 10


int main(int argc, char *argv[]){
	
  int listenfd;               // listening file descriptor
	struct sockaddr_in srvAddr; // local address
	uint16_t srvPort;           // server port

	if (argc != 2){     /* Test for correct number of arguments */
		fprintf(stderr, "Usage:  %s <srvPort>\n", argv[0]);
		exit(1);
	}
	
	char *srvPortStr = argv[1], *srvPortStrEnd;
  unsigned long srvPortLong = strtoul(srvPortStr, &srvPortStrEnd, 0);
  
  // port check: no conversion at all, extra junk at the end, out of range
  if (srvPortStrEnd == srvPortStr || *srvPortStrEnd != '\0' ||
      srvPortLong < 1024 || srvPortLong > 65535){
    fprintf(stderr,
            "Invalid port %s, please provide integer in 1024-65535 range\n",
             argv[1]);
    exit(1);
  }
  srvPort = (uint16_t) srvPortLong;
	
	/* Create socket for incoming connections */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
		perror("socket() failed");
		exit(1);
	}
	
	/* Construct local address structure */
	memset(&srvAddr, '\0', sizeof(srvAddr));     // zero out struct
	srvAddr.sin_family = AF_INET;                // Internet address family
	srvAddr.sin_addr.s_addr = htonl(INADDR_ANY); // any local address is fine
	srvAddr.sin_port = htons(srvPort);           // local port
	
	/* Bind to the local address */
	if (bind(listenfd, (struct sockaddr *) &srvAddr, sizeof(srvAddr)) < 0){
		perror("bind() failed");
		exit(1);
	}
	
	/* Mark the socket so it will listen for incoming connections */
	if (listen(listenfd, MAX_PENDING_CONNS) < 0){
		perror("listen() failed");
		exit(1);
	}
	
  struct sockaddr_in cliAddr;
  socklen_t cliAddrLen = sizeof(cliAddr);
	
  char rcvBuf[1024];
  
  while (1){ /* Main server loop - forever */
  
    memset(&rcvBuf, 0, sizeof(rcvBuf));
    memset(&cliAddr, 0, sizeof(cliAddr));
    
	  int connfd = accept(listenfd, (struct sockaddr*) &cliAddr, &cliAddrLen);
   
    if (connfd < 0){ // something must've gone wrong if so
      perror("accept() failed");
      continue; // try again
    }
    
    printf("Got ourselves a client!\n");
    
    while (1){ // loop forever

      memset(&rcvBuf, 0, sizeof(rcvBuf));
 
      if (read(connfd, rcvBuf, sizeof(rcvBuf)) < 0){
        perror("read() failed");
        continue;
      }
 
      printf("Echoing back - %s", rcvBuf);
 
      if (write(connfd, rcvBuf, strlen(rcvBuf)+1) < 0)
          perror("write() failed");
    
    } // while(1)
    
    close(connfd); // not reached
	
	} // while(1)
	
	close(listenfd); // not reached
}
