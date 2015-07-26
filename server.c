#include    "unp.h"
#include    "api.h"
#include    <sys/utsname.h>

void send_time(int, SA*, socklen_t);
char *findhostname(char *);
char *myname(void);

int main(int argc, char **argv)
{
    int                 sockfd;
    struct sockaddr_un  servaddr, cliaddr;

    printf("Server started...\n");
    sockfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);

    unlink(SERVPATH);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sun_family = AF_LOCAL;
    strcpy(servaddr.sun_path, SERVPATH);

    Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));

    send_time(sockfd, (SA *) &cliaddr, sizeof(cliaddr));
    unlink(SERVPATH);
}

void send_time(int sockfd, SA *pcliaddr, socklen_t clilen)
{
    int         n,port;
    socklen_t   len;
    char        mesg[MAXLINE],sendline[MAXLINE],ipaddr[16];
    time_t      ticks;
    struct      utsname myname;

    if(uname(&myname) < 0)
    {
        printf("Uname error!\n");
        return;
    }
    
    len = clilen;
    for(;;)
    {
        msg_recv(sockfd, mesg, ipaddr, &port);
        printf("Server at %s received a request from %s\n",myname.nodename,findhostname(ipaddr));

        ticks = time(NULL);
        snprintf(sendline, sizeof(sendline), "%.24s", ctime(&ticks));
        msg_send(sockfd, ipaddr, port, sendline, 0);
        printf("Server at %s responded to %s with timestamp\n",myname.nodename,findhostname(ipaddr));
    }
}

char *findhostname(char *ipaddr)
{
    struct hostent *he;
    struct in_addr addrinfo;
    inet_pton(AF_INET, ipaddr, &addrinfo);
    he = gethostbyaddr(&addrinfo, sizeof(addrinfo), AF_INET);

    return(he->h_name);
}
