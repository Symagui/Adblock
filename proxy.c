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
#include <ctype.h>

#define PROXY_DEFAULT_PORT 80
#define MAX_URLSIZE 2048
#define TIMEOUT 4
#define FILTERFILE "./mask.txt"


#define MAX_SENDER_SIZE 1024

//Colors
#define COL_RED     "\x1b[31m"
#define COL_GREEN   "\x1b[32m"
#define COL_YELLOW  "\x1b[33m"
#define COL_BLUE    "\x1b[34m"
#define COL_MAGENTA "\x1b[35m"
#define COL_CYAN    "\x1b[36m"
#define COL_RESET   "\x1b[0m"

static int _proxy_port = PROXY_DEFAULT_PORT;

struct host {
  int port;
  char *sport;
  int addr_family;
  int addr_len;
  struct sockaddr_in6 addr;
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

struct slist {
  char value[MAX_URLSIZE];
  struct slist *_prev;
};

struct slist *_filters;

int loadFilters(const char *filename){
  int size = 0;
	FILE *fp = fopen(filename, "rb");

  char * line = NULL;
  size_t len = 0;
  ssize_t read;

  if (fp == NULL){
    printf(COL_YELLOW "  Warning : File '%s' not found\n" COL_RESET, filename);
    return 0;
  }

  while ((read = getline(&line, &len, fp)) != -1) {

    struct slist *prev = _filters;
    struct slist *new = malloc(sizeof(struct slist));
    strncpy(new->value, line, MAX_URLSIZE);
    new->_prev = prev;
    _filters = new;

    size++;

  }

  fclose(fp);
  if (line)
      free(line);

  printf(COL_BLUE "  File '%s' loaded (%d masks)\n" COL_RESET, filename, size);

	return size;
}

int contain(char *str, char *needle){

  int nlen = strlen(needle);
  int str_len = strlen(str);

  char * pos;
  for(pos = str; pos<str+str_len-nlen+1; pos++){
    if(strncmp(pos, needle, nlen-1)==0){
      return 1;
    }
  }
  return 0;
}

int isAd(struct header *hd){

  //To long url for filter file
  if(strlen(hd->path)>MAX_URLSIZE){
    return 1;
  }

  struct slist *res;
  for(res=_filters; res!=NULL; res=res->_prev){
    if(contain(hd->path,res->value)){
      return 1;
    }
  }

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
		memset(hd->method, 0, sizeof(hd->method));
    strncpy(hd->method, currentOffset, size);
    currentOffset += size + 1;


    //SSL ?
    hd->ssl = 0;
    if(strcmp(hd->method, "CONNECT")==0){
      hd->ssl = 1;
    }


    //get path+port (in first line)
    size = strchr(currentOffset,' ') - currentOffset;
		memset(hd->path, 0, 500* sizeof(char));
    strncpy(hd->path, currentOffset, size);
    currentOffset += size + 1;


    //Get protocol, last word of first line
    size = strchr(currentOffset,'\r') - currentOffset;
		memset(hd->protocol, 0, 500*sizeof(char));
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
		memset(hd->hostname, 0, 500*sizeof(char));
    strncpy(hd->hostname, temp_hostport, portpos-temp_hostport);

    free(temp_hostport);

    //Get IP from hostname
    struct addrinfo hints;
    struct addrinfo *result;
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = PF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* TCP for HTTP server */
    char sport[16];
    sprintf( sport, "%d", (int) hd->hst.port );
		int test;
    if((test = getaddrinfo(hd->hostname,sport, &hints, &result)) != 0){
			printf("ERROR : getaddrinfo : %s\n", gai_strerror(test));
      //TODO Server not found
      exit(0);
    }

    //Fill family and addr (priority to ipv4 because it works better)
    struct addrinfo *res;
    for(res=result;res!=NULL;res=res->ai_next){

      hd->hst.addr_family = res->ai_family;
      hd->hst.addr_len = res->ai_addrlen;
      (void) memmove( &hd->hst.addr, res->ai_addr, hd->hst.addr_len );
      if(res->ai_family==AF_INET){ //IPv4 so keep this !
        break;
      }

    }

    //Show result
    //printf("\nHEADER :\n");
    //printf("  method '%s'\n", hd->method);
    //printf("  path '%s'\n", hd->path);
    //printf("  protocol '%s'\n", hd->protocol);
    //printf("  hostname '%s'\n", hd->hostname);
    //printf("  ip '%s'\n", inet_ntoa(*((struct in_addr*)(hd->hst.addr->ai_addr))));
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

  int sock = socket(dst->addr_family,SOCK_STREAM,0);

  if (connect(sock, (struct sockaddr*) &dst->addr, dst->addr_len) != 0){
      perror("error connect\n");
      exit(0);
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
  timeout.tv_sec = TIMEOUT;
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
  char rcv_buffer[4096];
  int n = 1;
  while(n>0){

    if((n = recv(dialogSocket, rcv_buffer, sizeof(rcv_buffer) - 1, 0)) < 0)
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
      printf(">" COL_RED " (INVALID)" COL_RESET " %s\n", hd->path);
      header_ok(dialogSocket);
      close(dialogSocket);
      return 0;
    } else {
      printf("> " COL_GREEN "(VALID) " COL_RESET " %s\n", hd->path);
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

  }

  close(dialogSocket);

  return 0;
}


/*
 * Show the manual
*/
void help(){

	printf("SIMPLE PROXY\n\n");
	printf("NOM\n		proxy - Filtrage de publicité et cache de données\n\n");
	printf("SYNOPSIS\n		(sudo) ./proxy [OPTION]... [FICHIER FILTRE, FICHIER FILTRE, ...]\n\n");
	printf("DESCRIPTION\n		Créé un proxy sur le port 80 (si non défini).\n\n");
	printf("OPTIONS\n		Toutes les options se combinent.\n\n");
	printf("		-h\n			Affichage de ce message.\n\n");
  printf("		-p\n			Modification du port du proxy.\n\n");
	printf("VALEUR DE RETOUR\n		Affichage des urls transmises et erreurs.\n\n");
	printf("EXEMPLE\n		sudo ./proxy -p 400 mask.txt\n Démarre un proxy sur le port 400 en utilisant les filtres du fichier mask.txt\n\n");

}


int main(int argc, char* const* argv){

  int c;
	while ((c = getopt (argc, argv, "p:h")) != -1){
		switch (c) {
      case 'h':
        help();
        exit(0);
        break;
			case 'p':
				_proxy_port = atoi(optarg);
				if(!isdigit(optarg[0]) || _proxy_port<0){
					printf(COL_RED "No valid argument given for -p option, set to default.\n" COL_RESET);
					_proxy_port = PROXY_DEFAULT_PORT;
				}
				break;
			default: // if ? encoutered, then there is an illegal opt
				printf(COL_RED "This option (%s) isn't supported, please refer to the ptar manual with %s -h\n" COL_RESET,argv[optind-2],argv[0]);
				exit(1); //Error
		}
	}

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
  serv_addr.sin_family = PF_INET ;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(_proxy_port);
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


  //Load filters
  int fsize = 0;
  printf(COL_BLUE "Loading filters...\n" COL_RESET);

  int arg_i;
  for(arg_i=optind; arg_i<argc; arg_i++){
    fsize += loadFilters(argv[arg_i]);
  }

  printf(COL_GREEN "%d filters loaded\n" COL_RESET, fsize);


  //Start proxy
  printf(COL_CYAN "\n|| Proxy started on port %i\n\n" COL_RESET, _proxy_port);

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
