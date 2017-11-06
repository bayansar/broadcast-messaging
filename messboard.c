#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>

struct Message{
	char data[256];
};

void error(const char*);
void dostuff (int,char*,sem_t*);

int main(int argc, char *argv[]){

	int maxnmess, maxtotmesslen, portNumber;
	int sockfd, newsockfd, pid,fd;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	char *shrdmem;
	int wrtIndx = 0;
	sem_t *semaphore;

	if(argc != 4){
		printf("Invalid number of argument!\n");
		exit(1);
	}

	sscanf(argv[1], "%d", &maxnmess);
	sscanf(argv[2], "%d", &maxtotmesslen);
	sscanf(argv[3], "%d", &portNumber);

	if(portNumber == 0){
		printf("Invalid port number!");
		exit(1);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		error("ERROR opening socket");
	}

	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portNumber);
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
		error("ERROR on binding");
	}

	listen(sockfd,5);
	clilen = sizeof(cli_addr);

	// Create semaphore to use by writing message to shared memory	
	semaphore = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	sem_init(semaphore,1,1);

	// Create shared memory with mmap
	fd = open("data.bin", O_CREAT | O_RDWR, 0666);
	shrdmem = mmap(0, sizeof(int) + (maxnmess * sizeof(struct Message)), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(shrdmem == MAP_FAILED){
		close(fd);
		error("ERROR on on mmapping the file");
	}
	ftruncate(fd, sizeof(int) + (maxnmess * sizeof(struct Message)));
	memcpy(shrdmem, &wrtIndx, sizeof(int));

	while (1) {
		newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
	        if (newsockfd < 0){ 
			error("ERROR on accept");
		}
		pid = fork();
		if (pid < 0){
			error("ERROR on fork");
		}
		if (pid == 0)  { // child process
			close(sockfd);
			dostuff(newsockfd,shrdmem,semaphore);
			exit(0);
		}else{ // parent process
			close(newsockfd);
		}
	}
	close(sockfd);
	return 0;
}

void error(const char *msg){
	    perror(msg);
	    exit(1);
}

void dostuff (int sock, char* shrdmem, sem_t* semaphore)
{
	int n;
	char buffer[256];
	char *command,*message;
	
	while(1){	
		bzero(buffer,256);
		n = read(sock,buffer,255);
		if (n < 0){
			error("ERROR reading from socket");
		}
		int i;
		int cmdSize=-1,msgSize=0;
		for(i=0; buffer[i] != '\n'; i++){
			if(buffer[i] == ' ' && cmdSize == -1){
				cmdSize = i; 
			}
		}
		msgSize = i - cmdSize - 1;
		if(cmdSize == -1 || msgSize == 0){
			int n = write(sock,"Invalid command!",16);
			if(n<0){
				error("Error writing to socket");
                        }
			continue;
		}
		command = (char *)malloc(cmdSize+1);
		message = (char *)malloc(msgSize+1);
		strncpy(command,buffer,cmdSize);
		strncpy(message,buffer+cmdSize+1,msgSize);
		*(command+cmdSize) = '\0';
		*(message+msgSize) = '\0';

		if(strcmp(command,"SEND") == 0){
			int wrtIndx = 0;
			memcpy(&wrtIndx, shrdmem, sizeof(int));
			// Atomic op start
			sem_wait(semaphore);
			memcpy(shrdmem + sizeof(int) + sizeof(struct Message) * wrtIndx, message, msgSize);
			wrtIndx++;
			memcpy(shrdmem, &wrtIndx, sizeof(int));
			sem_post(semaphore);
			// Atomic op end
			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");
			}
		}else if(strcmp(command,"LIST") == 0){
			char mes[256];
			memcpy(mes,shrdmem + sizeof(int),sizeof(mes));
			int n = write(sock,mes,256);
			if(n<0){
				error("Error writing to socket");
			}	
		}else if(strcmp(command,"BYE") == 0){
			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");	
			}	
			free(command);			
			free(message);
			break;			
		}else{
			int n = write(sock,"Got invalid command",20);
			if(n<0){
				error("Error writing to socket");	
			}	
		}

		free(command);
		free(message);
	}
	close(sock);
	exit(1);
}
