#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#define INITCHILD   -1
#define PRECHILD    5
#define MAXCHILD    50
#define BUFSIZE     4096
#define PIDPATH     "processpool"
#define PORT 33445

int fd1[2];
int fd2[2];

typedef struct processinfo
{
	pid_t pid;
	int status; //1:完成空闲  0:工作
}processinfo;

void  printerror(char *msg)
{
	//perror("%s:\n",msg);
	perror("msg:");
	return ;
}

//设置成守护进程
void daemon_init()
{	
	pid_t pid;
	pid=fork();
	if(pid>0)
	{
        //perror("fork:");
        exit(-1);
    }	
	setsid();
	pid=fork();
	if(pid>0)
	{
        //perror("fork1:");
        exit(-1);
    }    
	umask(0);
    int i;
	for(i=0;i<1024;i++)
    close(i);	   
}

//防止一次只能有一个进程运行
int  isrunning()
{
	int fd;
	fd=open(PIDPATH,O_RDONLY);//只能是O_RDONLY(只读)
	if(fd<0)
	{		
		return -1;
	}
	if(flock(fd,LOCK_EX|LOCK_NB)==-1)//独占锁定（写入的程序）不堵塞
	{
		printf("is running\n");
		return -1;
	}	
	return 0;
}

void dowork(int listenfd)
{
    int clientfd;
    struct sockaddr_in clientaddr;
    int clientsize=sizeof(clientaddr);
    char buf[BUFSIZE];
    int pid=getpid();
    char ischildstep;
    processinfo pinfo;
    pinfo.pid=pid;
	pinfo.status=INITCHILD;

    while(1)
    {
		flock(listenfd,LOCK_EX);
		//占有listenfd监听套接字 第一个客户必定会和我第一个子进程通讯
		//有效的控制顺序。
    	clientfd=accept(listenfd,(struct sockaddr*)&clientaddr,&clientsize);
		flock(listenfd,LOCK_UN);
    	bzero(buf,sizeof(buf));
    	pinfo.status=0;
    	while(write(fd1[1],&pinfo,sizeof(pinfo))<0)//成功连接 写一次管道和控制进程通信第一次
    	{
    		write(fd1[1],&pinfo,sizeof(pinfo));
    	}

    	//加循环
		sprintf(buf,"Connect success");
		send(clientfd,buf,strlen(buf),0);
    	printf("do working pid=%d\n",pid);

		while(1)
		{	
			bzero(buf,sizeof(buf));
			recv(clientfd,buf,sizeof(buf)-1,0);
    		printf("%d recv data:%s\n",getpid(),buf);
			if(strcmp(buf,"bye")==0)
			{
				printf("%d cli quit\n",getpid());
				break;
			}
		}

    	pinfo.status=1;//与客户结束通讯,写管道,第二次和控制进程通讯
    	while(write(fd1[1],&pinfo,sizeof(pinfo))<0)
    	{
    		write(fd1[1],&pinfo,sizeof(pinfo));
    	}

    	while(read(fd2[0],&ischildstep,sizeof(ischildstep))<0)
    	{
    		read(fd2[0],&ischildstep,sizeof(ischildstep));
    	}
    	if(ischildstep=='n')
    	{
    		printf("pid:%d is exit\n", getpid());
    		exit(-1);
    	}
    	if(ischildstep=='y')
    	{
            printf("pid:%d is continue\n", getpid());
		    continue;
    	} 	
    }
}

int main(int argc, char const *argv[])
{	
	//daemon_init();//先后台进程
	/*if(isrunning()<0)
	{		
		return;
	}	*/	
	int listenfd;
	struct sockaddr_in seraddr;
	pid_t pid;
	signal(SIGCHLD,SIG_IGN);//子进程结束信号 忽略
    if(pipe(fd1)<0)
    {
    	return;
    }
    if(pipe(fd2)<0)
    {
    	return;
    }

    bzero(&seraddr,sizeof(seraddr));
    seraddr.sin_family=AF_INET;
    seraddr.sin_port=htons(PORT);
    seraddr.sin_addr.s_addr=htonl(INADDR_ANY);

    listenfd=socket(AF_INET,SOCK_STREAM,0);
    bind(listenfd,(struct sockaddr*)&seraddr,sizeof(seraddr));
    listen(listenfd,1000);

    int i;
    for(i=0;i<PRECHILD;i++)
    {
    	pid=fork();
    	while(pid<0)
    	{
    		pid=fork();
    	}
    	if(pid==0)
    	{
            dowork(listenfd);
    	}
    	if(pid>0)
    	{
    		printf("create child process %d\n",pid);
    	}
    }

    char isexit='n';
    char iscontinue='y';
    int workprocess_num=0;
    int childprocess_num=PRECHILD;
    processinfo pinfo;
    int len;
    while(1)
    {
    	printf("workprocess_num=%d\tchildprocess_num=%d\n",
    		    workprocess_num,childprocess_num);
    	while(read(fd1[0],&pinfo,sizeof(pinfo))<0)
		//管道写不堵塞 读堵塞，读完就等待 只到有新的数据写进来
		//每次子进程写一次管道 while 执行一次
    	{
    		read(fd1[0],&pinfo,sizeof(pinfo));
    	}        	
    	if(pinfo.status==0)
    	{
            workprocess_num++;
            if(workprocess_num>=childprocess_num && childprocess_num<=MAXCHILD)
            {
			    printf("access up,add 1 process.warning!\n");
           	    pid=fork();
           	    while(pid<0)
           	    {
           	    	pid=fork();
           	    }
           	    if(pid==0)
           	    {
                    dowork(listenfd);
           	    }
           	    if(pid>0)
           	    {
                    childprocess_num++;
				    printf("create child process %d\n",pid);
           	    }
            }
    	}
    	else if(pinfo.status==1)
    	{
    		workprocess_num--;
    		if(childprocess_num>(workprocess_num+1)&&childprocess_num>PRECHILD)
			//如果已经超出，并通过应急增加后，当客户已经回到正常情况，删除多余进程
    		{
				printf("overstep the boundary\n");
    			while(write(fd2[1],&isexit,sizeof(isexit))<0)
    			{
    				write(fd2[1],&isexit,sizeof(isexit));
    			}
    			childprocess_num--;
    		}
    		else
    		{
    			while(write(fd2[1],&iscontinue,sizeof(iscontinue))<0)
    			{
    				write(fd2[1],&iscontinue,sizeof(iscontinue));
    			}
    		}
    	}		
    }
	return 0;
}
