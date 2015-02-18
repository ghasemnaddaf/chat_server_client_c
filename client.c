#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h> 
#include <pthread.h>

#define BUF_SIZE 64
int exit_flag;
void *rcv(void *args){
	int sockfd=*(int *)args;
	char recv_buff[BUF_SIZE];
	int n;
	while ( (n = read(sockfd, recv_buff, sizeof(recv_buff)-1)) > 0){
		recv_buff[n++] = '\n';
		recv_buff[n] = '\0';
		if(fputs(recv_buff, stdout) == EOF) {
			printf("\n Error : Fputs error\n");
		}
	}
	if(n < 0){
		printf("Read error\n");
	}
	if (n==0){
		printf("Connection closed\n");
		exit_flag=1;
	}
}

int main(int argc, char *argv[])
{
	char msg[BUF_SIZE];
    int sockfd=0;
    exit_flag=0;
    struct sockaddr_in serv_addr; 

    if(argc != 3)
    {
        printf("\n Usage: %s <ip of server> <port>\n",argv[0]);
        return -1;
    } 

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Error : Could not create socket \n");
        return -1;
    } 

    memset(&serv_addr, '0', sizeof(serv_addr)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2])); 

    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
    {
        printf("\n inet_pton error occurred while trying to convert %s to binary address\n",argv[1]);
        return -1;
    } 

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       printf("\n Error : Connect Failed \n");
       return -1;
    } 

    pthread_t p;
	pthread_create(&p,NULL,&rcv,(void *)(&sockfd));
	while (fgets(msg, BUF_SIZE, stdin)!=NULL && !exit_flag){
		msg[strlen(msg)-1]='\0'; //remove '\n'
		int len=write(sockfd,msg,strlen(msg));
		printf("sent:%s,len=%d\n",msg,len);
	}	
    return 0;
}
