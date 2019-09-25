#include <sys/types.h>  // getaddrinfo()
#include <sys/socket.h> // socket(), connect(), getaddrinfo()
 #include <arpa/inet.h> // inet_ntop()
#include <netinet/in.h> // sockaddr_in
#include <netdb.h>      // getaddrinfo()
#include <stdio.h>      // perror()
#include <stdlib.h>     // exit()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // read(), write()

#define MAX_LINE 1024

int main(int argc, char *argv[]){

  int clientfd;                 // client file descriptor
  struct addrinfo *srvAddrInfo; // server's address info
  
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
             srvPortStr);
    exit(1);
  }

  /* Find an IP address for the server */
  if (getaddrinfo("127.0.0.1", srvPortStr, NULL, &srvAddrInfo) != 0){
  	perror("getaddrinfo() failed");
  	exit(1);
  }
  
  /* Create socket for connecting to server */
  if ((clientfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
    perror("socket() failed");
    exit(1);
  }
  
  struct sockaddr_in *srvAddr = (struct sockaddr_in *) srvAddrInfo->ai_addr;
  uint16_t srvPort = ntohs(srvAddr->sin_port);

  char srvIpAddrStr[128];
  inet_ntop(AF_INET, &(srvAddr->sin_addr.s_addr), srvIpAddrStr,
   					sizeof(srvIpAddrStr));
  printf("Connecting to %s:%d...\n", srvIpAddrStr, srvPort);

  if (connect(clientfd, srvAddrInfo->ai_addr, srvAddrInfo->ai_addrlen)){
    perror("connect() failed");
    exit(1);
  }
  
  freeaddrinfo(srvAddrInfo); // free up memory
 
  while(1){
    char sndBuf[MAX_LINE], rcvBuf[MAX_LINE];
    
    memset(&sndBuf, '\0', MAX_LINE);
    memset(&rcvBuf, '\0', MAX_LINE);
    
    if (fgets(sndBuf, sizeof(sndBuf), stdin) == NULL)
      continue; // nothing to send
 
    if (write(clientfd, sndBuf, strlen(sndBuf)+1) < 0){
      perror("write() error");
      break;
    }
    
    if (read(clientfd, rcvBuf, sizeof(rcvBuf)) < 0){
      perror("read() error");
      break;
    }

    printf("Received: %s", rcvBuf);
  }
  
  close(clientfd);
}
