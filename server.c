#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <regex.h>

#define PROXY_PORT 80
#define MAX_RESPONSE_SIZE 4096
#define MAX_SENDER_SIZE 4096

//Colors
#define COL_RED     "\x1b[31m"
#define COL_GREEN   "\x1b[32m"
#define COL_YELLOW  "\x1b[33m"
#define COL_BLUE    "\x1b[34m"
#define COL_MAGENTA "\x1b[35m"
#define COL_CYAN    "\x1b[36m"
#define COL_RESET   "\x1b[0m"


struct host {
  int port;
  struct in_addr ip;
};

struct header {
  char ssl;
  char method[500];
  char path[500];
  char protocol[500];
  char hostname[500];
  struct host hst;
};

/*
* Find 'key' and return all chars after it until a \r
*/
char * getValueByKey(char httpHeader[], const char *key){

  int keylen = strlen(key);

  int i;
  int pos = 0;
  int found = -1;
  while(httpHeader[pos]!='\0'){
    if(strncmp(httpHeader+pos, key, keylen) == 0){
      found = pos+keylen;
      pos = pos+keylen;
    }
    if(found>-1 && httpHeader[pos]=='\r'){

      char *result = malloc(sizeof(char[pos-found+2]));

      for(i=found; i<pos; i++){
        result[i - found] = httpHeader[i];
        result[i-found+1] = '\0';
      }

      return result;
    }
    pos++;
  }
  return NULL;

}
/*
* Filter ads (return 1 if request an ad)
*/

int isAd(struct header *hd){
  //TODO Faire le filtre
  return 0;
}


/*
* Fill the header struct
*/
void fillHeader(struct header *hd, char *src_hd){


    int size = 0;
    char * currentOffset = src_hd;
    //get method (first thing)
    size = strchr(currentOffset,' ') - currentOffset;
    strncpy(hd->method, currentOffset, size);
    currentOffset += size + 1;


    //SSL ?
    hd->ssl = 0;
    if(strcmp(hd->method, "CONNECT")==0){
      hd->ssl = 1;
    }


    //get path+port (in first line)
    size = strchr(currentOffset,' ') - currentOffset;
    strncpy(hd->path, currentOffset, size);
    currentOffset += size + 1;


    //Get protocol, last word of first line
    size = strchr(currentOffset,'\r') - currentOffset;
    strncpy(hd->protocol, currentOffset, size);
    currentOffset += size + 1;


    //Get host and port
    char * temp_hostport = getValueByKey(currentOffset, "Host: ");
    char * portpos = strchr(temp_hostport,':');
    if(portpos == NULL){//No port defined
      portpos = temp_hostport+strlen(temp_hostport);

      //Set default port
      hd->hst.port = 80; //DEFAULT TODO : change depending on protocol and method !
      if (hd->ssl){
        hd->hst.port= 443; //DEFAULT SSL
      }

    }else{//Port is defined
      hd->hst.port = atoi(portpos+1);
    }
    strncpy(hd->hostname, temp_hostport, portpos-temp_hostport);

    free(temp_hostport);

    //Get IP from hostname
    struct hostent ht = *gethostbyname(hd->hostname);
    hd->hst.ip = *( struct in_addr*)(ht.h_addr_list[0]);


    //Show result
    //printf("\nHEADER :\n");
    //printf("  method '%s'\n", hd->method);
    //printf("  path '%s'\n", hd->path);
    //printf("  protocol '%s'\n", hd->protocol);
    //printf("  hostname '%s'\n", hd->hostname);
    //printf("  ip '%s'\n", inet_ntoa( *( struct in_addr*)( &hd->hst.ip)) );
    //printf("  port '%d'\n\n", hd->hst.port);

}

void header_ok(int client)
{
    char buf[1024] = "HTTP/1.0 200 OK\r\n\r\n\0";
    send(client, buf, strlen(buf), 0);
}

//Function to replace end of file chars by end of string
void repEolByEos(char* line){
    int l = strlen(line);
    while (line[l-1] == '\n'||line[l-1] == '\r'){
        line[--l] = '\0';
    }
}
int getRealServer(struct host *dst){

  struct sockaddr_in serv_addr;

  int sock = socket(PF_INET, SOCK_STREAM, 0);

  bzero( (char *) &serv_addr,  sizeof(serv_addr) );
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((ushort)dst->port);
  serv_addr.sin_addr = dst->ip;
  if (connect (sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr) ) < 0){
    perror ("cliecho : erreur connect");
    exit (1);
  }

  return sock;

}

int httpManager(int dialogSocket, struct header *hd, int respfd){

  //Now we receive data

  FILE* socketfp = fdopen( respfd, "r" );
  char line[10000];
  long contentl = -1;
  int achar = 0;
  while (fgets( line, sizeof(line), socketfp) != (char*) 0){
    if ( strcmp( line, "\n" ) == 0 || strcmp( line, "\r\n" ) == 0 ){
      //End of header
      break;
    }
    //Send each line of the header to the client
    send(dialogSocket, line, strlen(line),0);
    repEolByEos(line);
    if (strncasecmp(line, "Content-Length:", 15) == 0){
      contentl = atol(&(line[15]));
    }
  }
  //const char *connection_close = "Connection: close\r\n";
  //send(dialogSocket, connection_close, strlen(connection_close), 0);
  send(dialogSocket, line, strlen(line), 0);
  // Une fois le header envoye, on envoie le reste byte par byte (pour détecter exactement l'EOF)
  int i;
  for ( i=0; (contentl == -1||i<contentl)&&(achar=getc(socketfp))!=EOF; ++i ){
    send(dialogSocket, &achar, 1, 0);
  }

  close(respfd);

  return 0;
}


int httpsManager(int dialogSocket, struct header *fd, int respfd){
  char buf[10000];
  fd_set fdset;
  int maxfdp = respfd+1;  //Most likely
  if (dialogSocket > respfd) maxfdp = dialogSocket + 1; //Just in case
  int err;
  //int i = 1;
  // Timeout de 4 secondes
  struct timeval timeout;
  timeout.tv_sec = 4;
  timeout.tv_usec = 0;
  //Send OK to the browser for ssl connection
  const char *connection_established = "HTTP/1.0 200 Connection established\r\n\r\n";
  send(dialogSocket, connection_established, strlen(connection_established), 0);

  // On crée une connexion entre le client et le serveur dans le cas du ssl (on sert juste de tunnel)
  // Sauf si l'addresse fait partie des filtrées
  for(;;){
    //printf("message number %d for this ssl connection\n", i++);
    FD_ZERO(&fdset);
    FD_SET(dialogSocket, &fdset);
    FD_SET(respfd, &fdset);
    err = select(maxfdp, &fdset, NULL, NULL, &timeout); //TODO set a timeout
    if (err < 0){
      printf(COL_RED "ERROR with select on https connection\n" COL_RESET);
      exit(1);
    }
    if (err == 0){
      printf(COL_YELLOW "WARNING connection timeout\n" COL_RESET);
      const char *connection_timeout = "HTTP/1.0 499 Connection timeout\r\n\r\n";
      send(dialogSocket, connection_timeout, strlen(connection_timeout), 0);
      break;
    }

    if (FD_ISSET(dialogSocket, &fdset)){
      // the client has send something, we must relay (err = 0 means end of connection)
      err = read(dialogSocket, buf, sizeof(buf));
      if (err <= 0){
        //printf("end of connection client side 1\n");
        break;
      }
      err = write(respfd, buf, err);
      if (err <= 0){
        //printf("end of connection server side 1\n");
        break;
      }
    } else if (FD_ISSET(respfd, &fdset)){
      //The server has sent something, we must relay (err = 0 means end of connection)
      err = read(respfd, buf, sizeof(buf));
      if (err <= 0){
        //printf("end of connection server side 2\n");
        break;
      }
      err = write(dialogSocket, buf, err);
      if (err <= 0){
        //printf("end of connection client side 2\n");
        break;
      }
    }
    //break;
  }
  //header_ok(dialogSocket);
  //printf("closed because https!\n");
  return 0;
}


int ClientManager(int connectionNum, int dialogSocket, struct sockaddr_in cli_addr){

  //printf("dialogSocket = %d\n", dialogSocket);

  //Get data
  char rcv_buffer[MAX_SENDER_SIZE];
  int n = 0;

  if((n = recv(dialogSocket, rcv_buffer, sizeof rcv_buffer - 1, 0)) < 0)
  {
      perror("recv()");
      exit(1);
  }

  rcv_buffer[n] = '\0';
  
  //Get host
  struct header *hd;
  hd = malloc(sizeof(struct header));
  fillHeader(hd, rcv_buffer);
  if (isAd(hd)){
    printf("> (INVALID) %s\n", hd->path);
    header_ok(dialogSocket);
    close(dialogSocket);
    return 0;
  } else {
    printf("> (VALID) %s\n", hd->path);
  }

  //Relaying to real server
  int respfd = getRealServer(&hd->hst);

  if(hd->ssl==1){
    httpsManager(dialogSocket, hd, respfd);
  }else{
    send(respfd, rcv_buffer, strlen(rcv_buffer), 0);
    httpManager(dialogSocket, hd, respfd);
  }

  free(hd);
  close(dialogSocket);

  return 0;
}


int main(int argc, const char* argv[]){


  ////////////////////////////////////////////////////
  //Start server-browser socket and wait for clients//
  ////////////////////////////////////////////////////
  printf (COL_YELLOW "\nStarting proxy...\n" COL_RESET);

  int serverSocket ;
  struct sockaddr_in serv_addr;

  if ( (serverSocket=socket(PF_INET, SOCK_STREAM,0)) <0){
    perror(COL_RED "Socket error"); exit(1) ;
  }
  printf (COL_GREEN "Socket OK\n" COL_RESET);

  /*
  * Lier l'adresse locale à la socket
  */
  memset (&serv_addr, 0, sizeof(serv_addr) );
  serv_addr.sin_family = AF_INET ;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(PROXY_PORT);
  int ttl = 1;
  setsockopt(serverSocket, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
  if (bind(serverSocket,(struct sockaddr *)&serv_addr, sizeof(serv_addr) ) <0) {
    perror (COL_RED "Bind error");
    exit (1);
  }
  printf (COL_GREEN "Binding OK\n" COL_RESET);

  /* Paramétrer le nombre de
  connexion "pending" */
  if (listen(serverSocket, SOMAXCONN) <0) {
    perror (COL_RED "Erreur listen\n");
    exit (1);
  }
  printf(COL_GREEN "Max connexion OK (%d)\n" COL_RESET, SOMAXCONN);

  printf(COL_CYAN "\n|| Proxy started on port %i\n\n" COL_RESET, PROXY_PORT);

  /* Tout est ok */
  int dialogSocket;
  int clilen;
  struct sockaddr_in cli_addr;
  clilen = sizeof(cli_addr);

  /* On attend les connexions */
  int connectionId = 0;
  while(1){

    dialogSocket = accept(serverSocket, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen);
    if (dialogSocket < 0) {
     perror(COL_RED "Fatal : Cannot accept this new client\n");
     exit (1);
    }

    if(fork()>0){
      connectionId++;
      close(dialogSocket);
    }else{
      ClientManager(connectionId, dialogSocket, cli_addr);
      break;
    }

  }

  close(serverSocket);

  return 0;

}
