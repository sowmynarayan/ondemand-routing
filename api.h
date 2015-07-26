#include "common.h"

void msg_send(int sockfd, char *ipaddr, int port, char *sendline, int flag)
{
    struct sockaddr_un odraddr;
    struct s_msgtosend odrsend = {};
    strcpy(odrsend.ipaddr,ipaddr);
    strcpy(odrsend.sendline,sendline);
    odrsend.port = port;
    odrsend.flag = flag;

    bzero(&odraddr, sizeof(odraddr)); /* fill in server's address */
    odraddr.sun_family = AF_LOCAL;
    strcpy(odraddr.sun_path, ODRPATH);

    Sendto(sockfd, (char *)&odrsend, sizeof(struct s_msgtosend), 0, (SA *)&odraddr, sizeof(struct sockaddr_un));
}

void msg_recv(int sockfd, char *recvline, char *ipaddr, int *port)
{
    struct sockaddr_un odraddr;
    int n, odrlen;
    struct s_msgtosend odrrecv = {};

    bzero(&odraddr, sizeof(odraddr)); /* fill in server's address */
    odraddr.sun_family = AF_LOCAL;
    strcpy(odraddr.sun_path, ODRPATH);

    odrlen = sizeof(odraddr);
    n = Recvfrom(sockfd, (char *)&odrrecv, sizeof(odrrecv), 0, (SA*)&odraddr, &odrlen);

    strcpy(ipaddr, odrrecv.ipaddr);
    strcpy(recvline, odrrecv.sendline);
    *port = odrrecv.port;
}
