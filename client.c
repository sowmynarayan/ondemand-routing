#include    "unp.h"
#include    <stdio.h>
#include    "api.h"
#include    <sys/utsname.h>

char *findhostname(char *);

int main(int argc, char **argv)
{
    int    sockfd, n, ret, rport, fp, resentonce = 0, rediscover = 0;
    char    sendline[MAXLINE], recvline[MAXLINE + 1], input[100], ipaddr[16], ripaddr[16];
    char template[] = "/tmp/tmp.XXXXXX";
    char fname[100];
    struct sockaddr_un  cliaddr;
    struct hostent *host;
    struct timeval rem_time;
    struct utsname myname;
    fd_set rset;
    FD_ZERO(&rset);

    if(uname(&myname) < 0)
    {
        printf("Uname error! Quitting\n");
        return(-1);
    }

    sockfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);
    if( (fp = mkstemp(template)) < 0)
    {
        printf("Error creating template file in client! Quitting the program\n");
        return(-1);
    }
    close(fp);
    unlink(template);

    bzero(&cliaddr, sizeof(cliaddr));       /* bind an address for us */
    cliaddr.sun_family = AF_LOCAL;
    //strcpy(cliaddr.sun_path, tmpnam(NULL));
    strncpy(cliaddr.sun_path, template, strlen(template));

    Bind(sockfd, (SA *) &cliaddr, SUN_LEN(&cliaddr));

    do
    {
        resentonce = 0;
        rediscover = 0;
        printf("Enter server name: vm1, vm2 ... vm10 OR type exit to quit\n");
        ret = scanf("%s",input);
        if(strcmp(input,"exit") == 0)
            break;

        if(strncmp(input,"vm",2) != 0)
        {
            printf("Invalid input! Please try again\n");
            continue;
        }

        if( (host = gethostbyname(input)) == NULL )
        {
            printf("Invalid input! Please try again\n");
            continue;
        }
        if( inet_ntop(AF_INET,*(host->h_addr_list),ipaddr,sizeof(ipaddr)) == NULL )
        { 
            printf("inet_ntop error!\n");
            return(-1);
        }

        strcpy(sendline, "TimeReq");
        
        resend:
        printf("Client at node %s sending request to server at %s\n", myname.nodename, findhostname(ipaddr));
        msg_send(sockfd, ipaddr, SERVPORT, sendline, 0);
        
        FD_SET(sockfd, &rset);
        rem_time.tv_sec = 5;
        rem_time.tv_usec = 0;
        Select((sockfd+1), &rset, NULL, NULL, &rem_time);

        if(FD_ISSET(sockfd,&rset))
        {
            msg_recv(sockfd, recvline, ripaddr, &rport);
            printf("Client at node %s received from %s: \" %s \"\n", myname.nodename, findhostname(ripaddr), recvline);
        }
        else
        {
            if(resentonce == 0)
            {
                printf("Client at node %s timed out on response from server at %s!\n", myname.nodename, findhostname(ipaddr));
                printf("Resending request with force rediscovery flag set!\n");
                rediscover = 1;
                resentonce = 1;
                goto resend;
            }
            else
            {
                printf("No response even with forced rediscovery! Giving up \n");
                continue;
            }
        }

    }while(1);

    //Delete the tmp file created by client
    unlink(cliaddr.sun_path);
    exit(0);
}

char *findhostname(char *ipaddr)
{
    struct hostent *he;
    struct in_addr addrinfo;
    inet_pton(AF_INET, ipaddr, &addrinfo);
    he = gethostbyaddr(&addrinfo, sizeof(addrinfo), AF_INET);

    return(he->h_name);
}
