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
	int len;
};

struct Follow{
	char* data;
	struct Follow *next;	
};

struct HashObj{
	int pid;
	char word[256];
};

struct Follow* followList;
struct HashObj* shrdHash;

void error(const char*);
void dostuff (int,char*,sem_t*,struct HashObj*);
int hash(char*);

int main(int argc, char *argv[]){

	int maxnmess, maxtotmesslen, portNumber;
	int sockfd, newsockfd, pid,fd;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	char *shrdmem;
	int wrtIndx = 0;
	sem_t *semaphore;
	followList = NULL;

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

	//Create shared memory for hashtable
	shrdHash = (struct HashObj *)mmap(NULL, 10000 * sizeof(struct HashObj), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	int i; 
	for(i=0;i<10000;i++){
		shrdHash[0].pid = 0;
	}

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
			dostuff(newsockfd,shrdmem,semaphore,shrdHash);
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

int hash(char *str){
	unsigned long hash = 5381;
	int c;
	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return abs(hash) % 10000;
}

char** parseInWord(char *str){
	char * pch;	
	pch = strtok (str," ,.-");
	while (pch != NULL){
		printf ("%s\n",pch);
		pch = strtok (NULL, " ,.-");
	}
	return 0;
}

void dostuff (int sock, char* shrdmem, sem_t* semaphore, struct HashObj *shrdHash){
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
		if(cmdSize == -1){
			cmdSize = i;
		}else{
			msgSize = i - cmdSize - 1;
		}
		command = (char *)malloc(cmdSize+1);
		message = (char *)malloc(msgSize+1);
		strncpy(command,buffer,cmdSize);
		strncpy(message,buffer+cmdSize+1,msgSize);
		*(command+cmdSize) = '\0';
		*(message+msgSize) = '\0';

		
		if(strcmp(command,"SEND") == 0){
			int wrtIndx = 0,i=0;
			struct Message tmpMes;

			if(msgSize == 0){
				int n = write(sock,"Empty message!",14);
				if(n<0){
					error("Error writing to socket");
				}
				continue;
			}

			memcpy(&wrtIndx, shrdmem, sizeof(int));
			strcpy(tmpMes.data,message);
			tmpMes.len = msgSize;
			// Atomic op start
			sem_wait(semaphore);
			memcpy(shrdmem + sizeof(int) + sizeof(struct Message) * wrtIndx, &tmpMes, sizeof(struct Message));
			wrtIndx++;
			memcpy(shrdmem, &wrtIndx, sizeof(int));
			sem_post(semaphore);
			// Atomic op end

			int procLst[32768];
			for(i=0;i<32768;i++)
				procLst[i] = 0;
							
			char* pch;	
			pch = strtok (message," ,.-");
			while (pch != NULL){
				printf ("%s\n",pch);
				int indx = hash(pch);
				for(i=indx;shrdHash[i].pid != 0;i++){
					if(strcmp(shrdHash[i].word,pch) == 0){
						procLst[shrdHash[i].pid] = 1;
					}
					if(i==9999)
						i=-1;
				}
				pch = strtok (NULL, " ,.-");
			}
			for(i=0;i<32768;i++){
				if(procLst[i] == 1)
					printf("%d\n",i);
			}

			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");
			}
		}else if(strcmp(command,"LAST") == 0){
			int lstNmbr=0, wrtIndx=0,i=0,resultLen=0;
			char *tailshrdMem,*result;
		 	struct Message *msgLst;

			sscanf(message, "%d", &lstNmbr);
			memcpy(&wrtIndx, shrdmem, sizeof(int));
			if(lstNmbr > wrtIndx || msgSize == 0){
				lstNmbr = wrtIndx;
			}
				
			tailshrdMem = shrdmem + sizeof(int) + (sizeof(struct Message) * wrtIndx);
			msgLst = malloc(sizeof(struct Message) * lstNmbr);
			memcpy(msgLst,tailshrdMem - (sizeof(struct Message) * lstNmbr), sizeof(struct Message) * lstNmbr);
			for(i=0;i<lstNmbr;i++){
				resultLen += msgLst[i].len;
			}
			char c = '\n';
			result = (char *)malloc(resultLen + (lstNmbr - 1));
			for(i=lstNmbr-1;i>=0;i--){
				if(i!=lstNmbr-1){
					strncat(result,&c,1);
				}
				strncat(result,msgLst[i].data,msgLst[i].len);
			}
			int n = write(sock,result,resultLen+(lstNmbr-1));
			if(n<0){
				error("Error writing to socket");
			}
			free(result);	
			free(msgLst);
		}else if(strcmp(command,"FOLLOW") == 0){
			struct Follow *current = followList;
			if(current == NULL){
				current = malloc(sizeof(struct Follow));
				current->data = malloc(msgSize);
				strcpy(current->data,message);
				current->next = NULL;
				followList = current;
			}else{
				while(current->next != NULL){
					current = current->next;
				}
				current->next = malloc(sizeof(struct Follow));
				current->next->data = malloc(msgSize);
				strcpy(current->next->data,message);
				current->next->next = NULL;
			}
			
			int indx = hash(message);
			int i;
			for(i=indx;shrdHash[i].pid != 0 && shrdHash[i].pid != -1;i++){
				if(i == 9999)
					i=-1;
			}
			shrdHash[i].pid = getpid();
		       	strcpy(shrdHash[i].word,message);
	
			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");
			}
		}else if(strcmp(command,"FOLLOWING") == 0){
			struct Follow *current = followList;
			int wordsLen = 0;
			char *result;
			while(current != NULL){
				wordsLen += strlen(current->data) + 1;
				current = current->next;
			}
			result = malloc(wordsLen+1);
			current = followList;
			while(current != NULL){
				strcat(result,current->data);
				strcat(result," ");
				current = current->next;
			}
			int n = write(sock,result,wordsLen);
			if(n<0){
				error("Error writing to socket");
			}
			free(result);
		}else if(strcmp(command,"UNFOLLOW") == 0){
			struct Follow *current = followList;
			struct Follow *prev = NULL;
			while(current != NULL && strcmp(message,current->data) != 0){
				prev = current;
				current = current->next;
			}
			if(prev == NULL){
				followList = current->next;	
			}else{
				prev->next = current->next;
			}
			free(current);
			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");	
			}	
		}
		else if(strcmp(command,"BYE") == 0){
			int n = write(sock,"<ok>",4);
			if(n<0){
				error("Error writing to socket");	
			}	
			free(command);			
			free(message);
			break;			
		}else{
			printf("%s\n",command);
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
