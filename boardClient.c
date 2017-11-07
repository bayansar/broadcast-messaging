#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

void error(const char*);
void *sendCommand(void *vargp);

int isLstCmd = 0;

int main(int argc, char *argv[])
{
    	int sockfd, portNumber, n;
    	struct sockaddr_in serv_addr;
    	struct hostent *server;

    	char buffer[256];
	if(argc != 2){
		printf("Invalid number of argument!\n");
		exit(1);
	}

    	sscanf(argv[1], "%d", &portNumber);
    	sockfd = socket(AF_INET, SOCK_STREAM, 0);

    	if (sockfd < 0){ 
        	error("ERROR opening socket");
	}

    	server = gethostbyname("localhost");
    	if (server == NULL) {
        	fprintf(stderr,"ERROR, no such host\n");
        	exit(0);
    	}

    	bzero((char *) &serv_addr, sizeof(serv_addr));
    	serv_addr.sin_family = AF_INET;
    	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr,server->h_length);
    	serv_addr.sin_port = htons(portNumber);

    	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
        	error("ERROR connecting");
	}

	pthread_t tid;
	pthread_create(&tid, NULL, sendCommand, (void *)&sockfd);

	while(1){
    		bzero(buffer,256);
  	 	n = read(sockfd,buffer,255);
    		if (n < 0){ 
       			error("ERROR reading from socket");
		}
		printf("%s\n",buffer);
		if(isLstCmd){
			break;
		}
	}
	pthread_join(tid, NULL);
    	close(sockfd);
    	return 0;
}

void *sendCommand(void *vargp){
	char buffer[256];
	int sockfd = *((int *) vargp);
	int n;
	while(1){
		bzero(buffer,256);
		fgets(buffer,255,stdin);
		int i,cmdSize=-1;
		char *command;
		for(i=0; buffer[i] != '\n'; i++){
			if(buffer[i] == ' ' && cmdSize == -1){
				cmdSize = i; 
			}
		}
		if(i==0){
			printf("Invalid command!\n");
			continue;
		}
		if(cmdSize == -1){
			cmdSize = i;
		}
		command = (char *)malloc(cmdSize+1);
		strncpy(command,buffer,cmdSize);
		*(command+cmdSize) = '\0';
		n = write(sockfd,buffer,strlen(buffer));
		if(n<0){
			error("ERROR writing to socket");
		}					
		if(strcmp(command,"BYE") == 0){
			isLstCmd = 1;
			free(command);
			break;
		}
		free(command);
	}
	return NULL;
}

void error(const char *msg)
{
    perror(msg);
    exit(0);
}
