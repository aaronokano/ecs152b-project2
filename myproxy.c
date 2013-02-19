#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 65536
#define ERROR_MSG_SIZE 256
#define NUM_HEADERS 5

#define NON_FATAL_ERROR -1
#define ERROR_USAGE     1
#define ERROR_SOCKET    2
#define ERROR_BIND      3
#define ERROR_LISTEN    4

char error_msg[ERROR_MSG_SIZE];
int error_code;

/* Generic print error and quit */
void error( const char *msg, int code ) {
  printf("%s\n", msg);
  exit( code );
}

/* Call this for non-fatal error */
void nf_error( char *msg, int code ) {
  strcpy( error_msg, msg );
  error_code = code;
}

/* Use actual HTTP error codes */
void send_error_to_client( int sockfd ) {
  char msg[512];
  bzero( msg, 512 );
  char reason[32];
  switch( error_code ) {
    case 400:
      strcpy( reason, "BAD REQUEST" );
      break;
    case 501:
      strcpy( reason, "NOT IMPLEMENTED" );
      break;
    case 503:
      strcpy( reason, "SERVICE UNAVAILABLE" );
      break;
    default:
      strcpy( reason, "BROKEN" );
      break;
  }
  snprintf( msg, 512, "HTTP/1.0 %d %s\r\n\r\n%s\r\n", 
      error_code, reason, error_msg );
  send( sockfd, msg, strlen( msg ), 0 );
}

char *get_line( char *request ) {
  char *line;
  int line_len;
  if( (line_len = strcspn( request, "\r\n" )) == strlen( request ) )
    return NULL;
  line = (char *) malloc( line_len + 1 );
  bzero( line, line_len + 1 );
  memcpy( line, request, line_len );
  return line;
}

char *next_line( char *request ) {
  request = strchr( request, '\n' );
  return ++request;
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
  if( (p = strchr( url, '/' )) == NULL) {
    nf_error( "Invalid request!", 400 );
    return NULL;
  }
  if( *(++p) != '/' ) {
    nf_error( "Invalid request!", 400 );
    return NULL;
  }
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
  //printf("%s\n%s\n%s\n", server, port, *path);

  if( getaddrinfo( server, port, &hints, &res ) != 0 ) {
    nf_error( "Could not get address info", 400 );
    free( server );
    return NULL;
  }
  free( server );
  return res;
}

/* Check if Request-header line is a valid HTTP/1.0 header as specified in RFC
 * 1945. Return 1 if it is and 0 otherwise */
int valid_header( char *line ) {
  int i;
  char *validate = strdup( line );
  char *header[NUM_HEADERS] = { "Authorization", "From", "If-Modified-Since",
    "Referer", "User-Agent" };
  strtok( validate, ":" );
  for( i = 0; i < NUM_HEADERS; i++ ) {
    if( strcmp( validate, header[i] ) == 0 ) {
      free( validate );
      return 1;
    }
  }
  return 0;
}

/* Parse request and pack a string with an HTTP/1.0 compatible request. Also,
 * establish connection to web server. Return value is the socket fd for that
 * connection */
int parse( char *request, char *new_req ) {
  char *line, *path;
  int sockfd;
  struct addrinfo *web_server;
  char *p, *new_req_p = new_req;

  /* Grab just Request-line field */
  if( (line = get_line( request )) == NULL ) {
    nf_error( "No Request-line present", 400 );
    return NON_FATAL_ERROR;
  }
  p = strtok( line, " " );
  if( strcmp( p, "GET" ) != 0 ) {
    nf_error( "Not a GET request", 501 );
    return NON_FATAL_ERROR;
  }
  p = strtok( NULL, " " );
  if( (web_server = parse_url( p, &path )) == NULL ) {
    free( line );
    return NON_FATAL_ERROR;
  }
  free( line );
  /* Pack the new request with the information */
  new_req_p += sprintf( new_req_p, "GET %s HTTP/1.0\r\n", path );
  request = next_line( request );
  /* Grab all the valid headers */
  while( 1 ) {
    line = get_line( request );
    if( strcmp( line, "" ) == 0 )
      break;
    if( valid_header( line ) )
      new_req_p += sprintf( new_req_p, "%s\r\n", line );
    free( line );
    request = next_line( request );
  }
  sprintf( new_req_p, "\r\n" );
  if( (sockfd = socket( PF_INET, SOCK_STREAM, 0 )) < 0 ) {
    nf_error("failed to get socket!", 503);
    freeaddrinfo( web_server );
    return NON_FATAL_ERROR;
  }

  if( connect( sockfd, web_server->ai_addr, web_server->ai_addrlen) != 0 ) {
    nf_error("failed to connect to server!", 503);
    freeaddrinfo( web_server );
    return NON_FATAL_ERROR;
  }
  freeaddrinfo( web_server );

  return sockfd;
}

/* Fetch the request. Takes the request array, the client fd, a pointer to the
 * location in the request array to read into, and the remaining length of the
 * array. This is a recursive function */
int get_request( char *request, int client, char *pos, int length ) {
  int read;
  read = recv( client, pos, length, 0 );
  printf("%d\n",read);

  if( read == 0 ) {
    printf("Timeout\n");
    return 0;
  }
  /* If read == length, then the whole buffer is full and we should ignore */
  else if( read == length ) {
    nf_error( "Request too big", 400 );
    send_error_to_client( client );
    return 0;
  }
  /* If something goes wrong */
  else if( read == -1 ) {
    perror("read");
    return 0;
  }
  /* Check if the end of the packet has been read */
  else if( strstr( request, "\r\n\r\n" ) == NULL && 
      strstr( request, "\n\n" ) == NULL ) {
    /* Recurse if there could be more to read */
    return get_request( request, client, pos + read, length - strlen(request) );
  }
  return 1;
}

int main( int argc, char *argv[] ) {
  struct sockaddr_in server, client;
  int web_server_fd;
  int sockfd, s;
  int port_no;
  int read;
  char request[BUFFER_SIZE];
  char new_req[BUFFER_SIZE];
  char data[BUFFER_SIZE];
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
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv))
  {
    perror("setsockopt");
    return -1;
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
    if( get_request( request, s, request, BUFFER_SIZE ) == 1 ) {
      /* Parse, pack, and get socket fd for web server connection */
      if( (web_server_fd = parse( request, new_req )) == NON_FATAL_ERROR ) {
        send_error_to_client(s);
        close( s );
        continue;
      }
      printf("%s\n",new_req);
      printf("Connected to web server.\n");
      send( web_server_fd, new_req, strlen( new_req ), 0 );
      while( 1 ) {
        read = recv( web_server_fd, data, BUFFER_SIZE, 0 );
        if( read <= 0 ) {
          break;
        }
        //printf("Looped: %d\n", read);
        /* Handles the case where browser is directed elsewhere while sending
         * data to it */
        if( send( s, data, read, MSG_NOSIGNAL) == -1 )
          perror("send");
      }
    }
    close( s );

  }
  printf("Outside of main loop - should never happen\n");

  return 0;
}
