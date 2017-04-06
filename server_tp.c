#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

#define SERV_PORT 80

int main(){

  int serverSocket ;
  struct sockaddr_in serv_addr;
  socklen_t optlen ;

  if ( (serverSocket=socket(PF_INET, SOCK_STREAM,0)) <0){
    perror("probleme socket"); exit(1) ;
  }
  printf ("socket créé\n");

  /*
  * Lier l'adresse locale à la socket
  */
  memset (&serv_addr, 0, sizeof(serv_addr) );
  serv_addr.sin_family = AF_INET ;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(SERV_PORT);
  if (bind(serverSocket,(struct sockaddr *)&serv_addr, sizeof(serv_addr) ) <0) {
    perror ("servecho: erreur bind\n");
    exit (1);
  }
  printf("attache créée\n");

  /* Paramétrer le nombre de
  connexion "pending" */
  if (listen(serverSocket, SOMAXCONN) <0) {
    perror ("servecho: erreur listen\n");
    exit (1);
  }
  printf("nombre d'écoutes configuré\n");


  printf("\nDémarrage sur le port %i\n", SERV_PORT);

  int dialogSocket;
  int clilen;
  int noclient = 0;
  struct sockaddr_in cli_addr;
  clilen = sizeof(cli_addr);

  while(1){

    dialogSocket = accept(serverSocket, (struct sockaddr *)&cli_addr, (socklen_t *)&clilen);
    if (dialogSocket < 0) {
     perror("servecho : erreur accept\n");
     exit (1);
    }
    printf("nouvelle connexion (client %d)!\n", noclient);

    if(fork()){
      noclient++;
    }else{
      break;
    }

  }

  char rcv_buffer[1024];
  char buffer[1024];
  int n = 0;

  sprintf(buffer, "Vous êtes le client (%d)\0", noclient);

  send(dialogSocket, buffer, sizeof buffer - 1, 0);

  while(1){

    if((n = recv(dialogSocket, rcv_buffer, sizeof rcv_buffer - 1, 0)) < 0)
    {
        perror("recv()");
        exit(1);
    }

    if(n<=0){
      break;
    }

    rcv_buffer[n] = '\0';

    printf("client(%d): %s \n",noclient,rcv_buffer);

    //Respond the same thing
    sleep(1);
    if(!(rcv_buffer[0]=='4' && rcv_buffer[1]=='2')){
      sprintf(buffer, "Je n'aime pas ce contenu : %s\0", rcv_buffer);
    }else{
      sprintf(buffer, "J'aime ce contenu : %s\0", rcv_buffer);
    }

    send(dialogSocket, buffer, sizeof buffer - 1, 0);

  }
  printf("Client (%d) déconnecté.\n",noclient);

  close(dialogSocket);
  close(serverSocket);

}
