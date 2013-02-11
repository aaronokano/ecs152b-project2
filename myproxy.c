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
#define ERROR_MALFORMED 8

/* Generic print error and quit */
void error( const char *msg, int code ) {
  printf("%s\n", msg);
  exit( code );
}

char *get_request_line( char *request ) {
  char *request_line;
  int req_line_len;
  if( (req_line_len = strcspn( request, "\r\n" )) == strlen( request ) )
    error( "Improper HTTP request!", ERROR_MALFORMED );
  request_line = (char *) malloc( req_line_len + 1 );
  bzero( request_line, req_line_len + 1 );
  memcpy( request_line, request, req_line_len );
  return request_line;
}

struct addrinfo *parse_url( char *url, char **path ) {
  /* url should contain a URL and the absolute path should be returned in
   * path. The return value of the function will be an addrinfo struct with
   * information about the web server */
  struct addrinfo hints, *res; /* For use with getaddrinfo */
  char *p;
  char *server, *s;
  *path = malloc( 65535 );
  char *port;
  
  bzero( (char *) &hints, sizeof( hints ) );
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = PF_INET;

  /* Parsing will break down URL specified by
   * proto://host[:port]path, where path begins with a /
   * Note that the hostname MUST be present, as we will be closing the
   * connection to the server as soon as a response is received */

  /* Skip over proto field */
  if( (p = strchr( url, '/' )) == NULL)
    error("Invalid URL!", ERROR_MALFORMED);
  if( *(++p) != '/' )
    error("Invalid URL!", ERROR_MALFORMED);
  p++;
  server = (char*)malloc( strcspn( p, "/" ) + 1 );
  s = server;
  while( *p != '/' ) {
    *s = *p;
    p++; s++;
  }
  *s = '\0';
  server = strtok( server, ":" );
  port = strtok( NULL, ":" );
  if( port == NULL ) {
    port = malloc( 3 );
    strcpy( port, "80" );
  }
  *path = p;
  printf("%s\n%s\n%s\n", server, port, *path);
    
  if( getaddrinfo( server, port, &hints, &res ) != 0 )
    error( "getaddrinfo failed!", ERROR_GAI );
  return res;
}

int main( int argc, char *argv[] ) {
  struct sockaddr_in server, client;
  struct addrinfo web_server;
  int sockfd, s;
  int port_no;
  char request[BUFFER_SIZE];
  char *request_line, *path;
  socklen_t size;

  if( argc < 2 )
    error("Usage: myproxy <port>", ERROR_USAGE );
  
  port_no = atoi( argv[1] );

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
    request_line = get_request_line( request );
    printf("%s\n",request_line);
    request_line = strtok( request_line, " " );
    if( strcmp( request_line, "GET" ) != 0 )
      error("Not get", ERROR_MALFORMED);
    request_line = strtok( NULL, " " );
    web_server = parse_url( request_line, &path );
    close( s );
 
  }

  return 0;
}
