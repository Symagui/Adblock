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
#define PACKET_SIZE 4096

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

int sendToRealServer(struct host *dst, char * data){

  struct sockaddr_in serv_addr;

  int sock = socket(PF_INET, SOCK_STREAM, 0);

  /*
   * Remplir la structure serv_addr avec
  l'adresse du serveur reel
   */
  bzero( (char *) &serv_addr,  sizeof(serv_addr) );
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((ushort)dst->port);
  serv_addr.sin_addr = dst->ip;
  if (connect (sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr) ) < 0){
    perror ("cliecho : erreur connect");
    exit (1);
  }


  int n;
  if((n = send(sock, data, sizeof data - 1, 0)) < 0)
  {
      perror("sent()");
      exit(1);
  }

  char buff[PACKET_SIZE];

  if((n = recv(sock, buff, sizeof buff - 1, 0)) < 0)
  {
      perror("recv()");
      exit(1);
  }

  printf("received : %s\n",buff);

  /*while(1){

    if((n = recv(sock, buffer, sizeof buffer - 1, 0)) < 0)
    {
        perror("recv()");
        exit(1);
    }

    printf("server: %s\n", buffer);

    scanf(" %[^\n]s", buffer);

    if((n = send(sock, buffer, sizeof buffer - 1, 0)) < 0)
    {
        perror("sent()");
        exit(1);
    }

    printf("client: %s\n",buffer);

  }*/

  close(sock);

  return 0;

}

char * getValueByKey(char httpHeader[], const char *key){

  int keylen = strlen(key);

  int pos = 0;
  int found = -1;
  while(httpHeader[pos]!='\0'){
    if(strncmp(httpHeader+pos, key, keylen) == 0){
      found = pos+keylen;
      pos = pos+keylen;
    }
    if(found>-1 && httpHeader[pos]=='\n'){

      char *result = malloc(sizeof(char[found-pos+1]));

      for(int i=found; i<pos; i++){
        result[i - found] = httpHeader[i];
        result[i-found+1] = '\0';
      }

      return result;
    }
    pos++;
  }
  return NULL;

}

struct host *getHost(char httpHeader[]){

  struct host *h = malloc(sizeof(struct host));

  char * hostport;
  hostport = getValueByKey(httpHeader, "Host: ");

  char * host = hostport;
  char * port = hostport;
  int int_port = -1;
  int pos = 0;
  while(hostport[pos]!='\0'){
    if(hostport[pos]==':'){
      hostport[pos] = '\0';
      port = hostport + pos + 1;
      int_port = 1;
      break;
    }
    pos++;
  }
  if(int_port<0){
    int_port = 80; //Si on a pas trouvé de port (pas de :) c'est que c'est le port par défaut (80)
  }else{
    int_port = atoi(port);
  }
  printf("Detected : %s %d\n", host, int_port);

  struct hostent ht = *gethostbyname(host);

  h->port = int_port;
  h->ip = *( struct in_addr*)(ht.h_addr_list[0]);

  return h;

}

int ClientManager(int connectionNum, int dialogSocket, struct sockaddr_in cli_addr){

  printf("dialogSocket = %d\n", dialogSocket);

  //Get data
  char rcv_buffer[PACKET_SIZE];
  int n = 0;

  if((n = recv(dialogSocket, rcv_buffer, sizeof rcv_buffer - 1, 0)) < 0)
  {
      perror("recv()");
      exit(1);
  }

  rcv_buffer[n] = '\0';

  printf("client : %d\n",connectionNum);
  printf("data : \n" COL_BLUE "%s \n" COL_RESET,rcv_buffer);

  //Get host
  struct host *host = getHost(rcv_buffer);

  printf("src : %s:%d\n",inet_ntoa(cli_addr.sin_addr),cli_addr.sin_port);
  printf("dst : %s:%d\n", inet_ntoa( *( struct in_addr*)( &host -> ip)), host->port);

  //Relaying to real server
  int respfd = sendToRealServer(host, rcv_buffer);

  return 0;
}


int main(int argc, const char* argv[]){


  ////////////////////////////////////////////////////
  //Start server-browser socket and wait for clients//
  ////////////////////////////////////////////////////
  printf (COL_YELLOW "\nStarting proxy...\n" COL_RESET);

  int serverSocket ;
  struct sockaddr_in serv_addr;
  socklen_t optlen ;

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
    printf("New connexion (num %d)\n", connectionId);

    if(fork()){
      connectionId++;
    }else{
      ClientManager(connectionId, dialogSocket, cli_addr);
      break;
    }

  }

  close(dialogSocket);
  close(serverSocket);

}
