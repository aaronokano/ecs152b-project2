#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BUFFER_SIZE 65536

#define ERROR_USAGE  1
#define ERROR_SOCKET 2
#define ERROR_BIND   3
#define ERROR_LISTEN 4
#define ERROR_PIPE   5
#define ERROR_FORK   6
#define ERROR_GAI    7

/* Generic print error and quit */
void error( const char *msg, int code ) {
  printf("%s\n", msg);
  exit( code );
}

/* NOT DONE YET! */
struct addrinfo parse_server( char *request ) {
  struct addrinfo hints, *res; /* For use with getaddrinfo */
  char *server;
  
  bzero( (char *) &hints, sizeof( hints ) );
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = PF_INET;

  if( getaddrinfo( server, 80, &hints, &res ) != 0 )
    error( "getaddrinfo failed!", ERROR_GAI );

}
int main( int argc, char *argv[] ) {
  struct sockaddr_in server, client;
  struct addrinfo web_server; hints, *res; /* For use with getaddrinfo */

  int sockfd, s;
  int port_no;
  char request[BUFFER_SIZE];
  socklen_t size;
  pid_t child_pid;

  if( argc < 2 )
    error("Usage: myproxy <port>", ERROR_USAGE );

  if( (sockfd = socket( PF_INET, SOCK_STREAM, 0 )) < 0 )
    error("Could not create socket!", ERROR_SOCKET);
  /* Change to SO_REUSEADDR to avoid binding errors while debugging */
  int yes = 1;
  if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("setsockopt");
    exit(1);
  }

  /* Do standard binding actions */
  bzero( (char *) &server, sizeof( server ) );
  server.sin_family = PF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons( port_no );

  if( bind( sockfd, (struct sockaddr *) &server, sizeof(server) ) == -1 )
    error("bind failed!", ERROR_BIND);

  if( listen( sockfd, 5 ) == -1 )
    error("listen failed!", ERROR_LISTEN);

  while( 1 ) {
    /* Repeat while server is running */
    printf("Waiting for connection...\n");
    size = sizeof(client);
    if( (s = accept( sockfd, (struct sockaddr *) &client, &size )) == -1 ) {
      perror("accept failed");
      continue;
    }
    printf("Connected\n");
    bzero( request, BUFFER_SIZE );
    recv( s, request, BUFFER_SIZE, 0 );
    printf("%s\n",request);
    close( s );
 
  }

  return 0;
}
