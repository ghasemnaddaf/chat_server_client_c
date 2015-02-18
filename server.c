#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#define MAX_NAME_SIZE 128
#define BUF_SIZE 1024
#define MAX_CONN 10
#define HASH(x) x%MAX_CONN
#define DEFAULT_PORT 17777


typedef struct ClientInfo {
	int conn_fd;
	char name[MAX_NAME_SIZE];
	time_t date_joined;
	char pm_buff[BUF_SIZE];
	sem_t pm_buff_full;
	sem_t pm_buff_empty;
} ClientInfo;

void init_ci(ClientInfo *ci, int conn_fd, int id){
	ci->conn_fd = conn_fd;
	int tmp=snprintf(ci->name,10,"unnamed%.2d",id);
	printf("Accepting connection %d with ID: %d (%d chars written).\n",conn_fd,id,tmp);
	ci->date_joined=time(NULL);
	sem_init(&ci->pm_buff_full,1,0);
	sem_init(&ci->pm_buff_empty,1,1);
}

void clear_ci(ClientInfo *ci){
	sem_destroy(&ci->pm_buff_empty);
	ci->pm_buff[0]=0;
	ci->name[0]=0;
	ci->conn_fd=-1;
	sem_post(&ci->pm_buff_full); //so that pm_sender can quit
	sem_destroy(&ci->pm_buff_full);
}

char bcast_buff[BUF_SIZE]; //the broadcast buffer
sem_t bcast_buff_full;
sem_t bcast_buff_empty;
ClientInfo conntab[MAX_CONN]; //connection table

void print_conntab(){
	printf("conntab:\n");
	for (int i=0;i<MAX_CONN;i++){
		printf("#%.2d:%s,%d/%s/%s\n",i,conntab[i].name,conntab[i].conn_fd,ctime(&conntab[i].date_joined),conntab[i].pm_buff);
	}
}

void word_index(char *s,int len, int *start, int *end, int m){  //a simple "split", returns start and end of first, second, ... mth word, if any.
	int i=0;
	int w=0;
	for (;w<m;w++){
		while (i<len && (s[i]==' ' || s[i]=='\t')) //initial white space
			i++;
		start[w]=i;
		while (i<len && s[i]!=' ' && s[i]!='\t') //first word
			i++;
		end[w]=i;
	}
	for (;w<m;w++){
		start[w]=len;
		end[w]=len;
	}
}

void *broadcaster(void *args){ //the broadcaster thread is responsible to read the bcast_buffer and send it to all active connections.
	//ClientInfo *conntab=(ClientInfo *)args;
	char msg[BUF_SIZE];
	while (1){
		sem_wait(&bcast_buff_full);
		memcpy(msg,bcast_buff,strlen(bcast_buff)+1);
		sem_post(&bcast_buff_empty);
		for (int i=0; i<MAX_CONN; i++)
			if (conntab[i].conn_fd!=-1) {
				write(conntab[i].conn_fd, msg, strlen(msg));
				printf("sent to %d: %s\n",conntab[i].conn_fd,msg);
			}
	}
}

void *pm_sender(void *args){ //each connection has a pm_sender thread to read its pb_buff and write it to the conn_fd.
	ClientInfo *me=conntab+(*(int *)args);  //(ClientInfo *)args;
	while(me->conn_fd!=-1){
		sem_wait(&me->pm_buff_full);
		write(me->conn_fd, me->pm_buff, strlen(me->pm_buff));
		sem_post(&me->pm_buff_empty);
	}
}

void *client(void *args){ //the client thread reads the messages from the conn_fd and handles commands and writes to the pm or bcast buffers
	int exit_flag,cmd_flag;
	int id=*((int *)args);
	ClientInfo *me=conntab+id;
	char recv_buff[BUF_SIZE];
	char send_buff[BUF_SIZE];
	time_t tm;
	memcpy(recv_buff,"Joined.\n",8);
	recv_buff[8]='\0';
	int n=strlen(recv_buff);
	exit_flag=0;
	do {
	    recv_buff[n] = '\0';
		cmd_flag=0;
		printf("msg:%s\n",recv_buff);
		if (recv_buff[0]=='\\' && n>1 && recv_buff[1]!='\\'){
			//COMMAND MODE
			printf("command mode\n");
			cmd_flag=1;
			send_buff[0]=0;
			for (int i=0;i<n;i++)
				recv_buff[i] = tolower(recv_buff[i]);
			int start[3],end[3];
			word_index(recv_buff,n,start,end,3); //we need to parse at most 2 parts for commands, last part is the message
			printf("recv_buff=%s\nstarts:%d %d %d\nends:   %d %d %d\n",recv_buff,start[0],start[1],start[2],end[0],end[1],end[2]);
			if (recv_buff[1]=='?' || strncmp(recv_buff,"\\help",5)==0){
				snprintf(send_buff,sizeof(send_buff),"Available commands (use \\ prefix):\nls (w/who)\t\t\tlist active users\nname (rename) [new_name]\t(re)name yourself. With no arg: see your current name.\npm <id_or_name> <msg>\t\tprivate message id or name.\nbye (quit/exit)\t\t\tsign off.\n");
			} else if (recv_buff[1]=='w' || strncmp(recv_buff,"\\ls",3)==0 || strncmp(recv_buff,"\\who",4)==0){
				int len=0;
				for (int i=0; i<MAX_CONN; i++)
					if (conntab[i].conn_fd!=-1)
						len+=snprintf(send_buff+len,sizeof(send_buff)-len,"#%.2d:%s.\r\n",i,conntab[i].name);
			} else if (strncmp(recv_buff,"\\bye",4)==0 || strncmp(recv_buff,"\\exit",5)==0 || strncmp(recv_buff,"\\quit",5)==0){
				memcpy(send_buff,"Bye!\n",5);
				send_buff[5]='\0';
				exit_flag=1;
			} else if (strncmp(recv_buff,"\\name",5)==0 || strncmp(recv_buff,"\\rename",7)==0){
				int i=0;
				int name_len;
				if (end[1]>start[1]){
					name_len=(end[1]-start[1]>MAX_NAME_SIZE)?MAX_NAME_SIZE:end[1]-start[1];
					memcpy(me->name,recv_buff+start[1],name_len);
					me->name[name_len]='\0';
					if (strncmp(me->name,"admin",5)==0){ //reject "admin" as a name
						memcpy(me->name,"not-admin",9);
						me->name[9]='\0';
					}
					printf("new name=%s\n",me->name);
				} else {
					name_len=strlen(me->name);
				} //we skip third part of the msg if exists
				memcpy(send_buff,"Your name is ",13);
				memcpy(send_buff+13,me->name,name_len);
				send_buff[13+name_len]='\n';
				send_buff[13+name_len+1]='\0';
			} else if (strncmp(recv_buff,"\\pm",3)==0){
				int i=0;
				int pm_sent_num=0;
				char target_name[MAX_NAME_SIZE];
				if (end[1]>start[1]){
					memcpy(target_name,recv_buff+start[1],end[1]-start[1]);
					target_name[end[1]-start[1]]='\0';
					printf("target_name=%s\n",target_name);
					if (start[2]<n){
						tm = time(NULL);
						snprintf(send_buff, sizeof(send_buff), "%.24s", ctime(&tm));
						snprintf(send_buff+24,sizeof(send_buff)-24," [#%.2d:%s] (PM): %s\n",id,me->name,recv_buff+start[2]);
					} else {
						memcpy(send_buff,"no message to pm!\n",18);
						send_buff[18]='\0';
						pm_sent_num=-1;
					}
				} else {
					memcpy(send_buff,"no name to pm!\n",15);
					send_buff[15]='\0';
					pm_sent_num=-1;
				}
				if (pm_sent_num!=-1){
					for (int j=0; j<MAX_CONN; j++){
						char tmp_id[3];
						snprintf(tmp_id,4,"%d",j);
						if (conntab[j].conn_fd!=-1 && (strcmp(conntab[j].name,target_name)==0 || strcmp(tmp_id,target_name)==0)){
							sem_wait(&conntab[j].pm_buff_empty);
							memcpy(conntab[j].pm_buff,send_buff,strlen(send_buff)+1);
							sem_post(&conntab[j].pm_buff_full);
							pm_sent_num++;
						}
					}
					snprintf(send_buff,sizeof(send_buff),"pm sent to %.2d user(s).\n",pm_sent_num);
			  }
			} else {
				memcpy(send_buff,"unknown command!\n",17);
				send_buff[17]='\0';
			}
			//write(connfd, send_buff, strlen(send_buff));
			sem_wait(&me->pm_buff_empty);
			memcpy(me->pm_buff,send_buff,strlen(send_buff)+1);
			sem_post(&me->pm_buff_full);
		} //END COMMAND MODE
		if (exit_flag){
			memcpy(recv_buff,"Left.\n",6);
			recv_buff[6]='\0';
			n=6;
			cmd_flag=0;
		}
		if (!cmd_flag){
			//Broadcast mode
	    	tm = time(NULL);
	    	snprintf(send_buff, sizeof(send_buff), "%.24s", ctime(&tm));
			snprintf(send_buff+24,sizeof(send_buff)-24," [#%.2d:%s]: %s\n",id,me->name,recv_buff);
			int send_len=24+6+strlen(me->name)+3+n;
			send_buff[send_len]='\0';
			sem_wait(&bcast_buff_empty);
			memcpy(bcast_buff,send_buff,send_len+1);
			sem_post(&bcast_buff_full);
		}
	} while (!exit_flag && (n = read(me->conn_fd, recv_buff, sizeof(recv_buff)-1)) > 0);
	if(n < 0){
	    printf("Read error from %d\n",me->conn_fd);
	}
	close(me->conn_fd);
	clear_ci(me);
}

int main(int argc, char *argv[]){
	int listen_fd,conn_fd;
	pthread_t tid_bcaster,tid_client[MAX_CONN],tid_pm_sender[MAX_CONN];

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

	for (int i=0;i<MAX_CONN;i++)
		conntab[i].conn_fd=-1;
	sem_init(&bcast_buff_full,1,0);
	sem_init(&bcast_buff_empty,1,1);
	//we can start the broadcaster thread as early as here:
	pthread_create(&tid_bcaster,NULL,&broadcaster,NULL);

	listen_fd=socket(AF_INET,SOCK_STREAM,0);
	struct sockaddr_in serv_addr;
	memset(&serv_addr,0,sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(argc<2?DEFAULT_PORT:atoi(argv[1]));
	bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	listen(listen_fd, MAX_CONN-1); //0 is reserved for admin
	printf("listening on port %d\n",ntohs(serv_addr.sin_port));
	conn_fd=0;
	do {    	
		//Register the connection first: //First round, register stdin (0) as admin
		int id=conn_fd;
		while (conntab[id=HASH(id)].conn_fd!=-1)
			id++;
		init_ci(conntab+id, conn_fd, id);
		printf("Accepting connection %d with ID: %d.\n",conn_fd,id);
		if (conn_fd==0){
			strcpy(conntab[id].name,"admin");
		}
		print_conntab();
		pthread_create(tid_client+id,NULL,&client,(void *)(&id));
		pthread_create(tid_pm_sender+id,NULL,&pm_sender,(void *)(&id));
		conn_fd = accept(listen_fd, (struct sockaddr*)NULL, NULL);
    } while(1);
    //pthread_kill(tid_bcaster);
}
