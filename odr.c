#include    <string.h>
#include	"hw_addrs.h"
#include    "common.h"
#include    "unp.h"
#include    <sys/socket.h>
#include    <linux/if_packet.h>
#include    <linux/if_ether.h>
#include    <linux/if_arp.h>

#define FIRSTCLIPORT 30000
#define ETH_FRAME_LEN 1514
#define USID_PROTO 7156
#define NOTFOUND -1
#define FOUND 1
#define SAME 2
#define HIGHER 3

int bcastid;
int staleness;

/* FUNCTION PROTOTYPES BEGIN HERE */
/* ****************************** */

void createIfList(void);
void addToIfList(char *, char *, char *, int);
void printIfaces(void);
void addToPtoSTable(int, char *);
void printpTosTable(void);
char* getMyIP(void);
char* findhostname(char *);
void addToRoutingTable(char *, int , char *, int, time_t);
void updateTimestamp(char *);
int lookupRoutingTable(char *, char *, int *);
int getHopCount(char *);
void printRoutingTable(void);
void deleteFromRoutingTable(char *);
void addtoRREQList(int , char *);
void deletefromRREQList(char *);
void floodRREQ(int, char *, char *, int, int, int, int, int);
void processRREQ(int, int, struct s_odrmsg *, int, char *);
void sendApplicationMessage(int, char *, int, struct s_msgtosend, char *, int, int); 
void parkApplicationMessage(char *, int, struct s_msgtosend, int, char *);
void processODRDataPkt(int, int, struct s_odrmsg *, int, char *);
void sendRREP(int, char *, char *, int, int, char *, int);
void processRREP(int, int, struct s_odrmsg *, int, char *);
void deletefromPark(char *);
void parkRREP(struct s_odrmsg);
void deletefromRREPpark(char *);

/* FUNCTION PROTOTYPES END HERE */
/* **************************** */

int main(int argc, char **argv)
{
    struct sockaddr_un odraddr, recvfromaddr, sendtoaddr;
    int sockfd, pfsock, maxfd, n, odrlen, slen, iface;
    int cliport = FIRSTCLIPORT;
    char recvline[MAXLINE+1], sendpath[256], myip[16],dstmac[18];
    struct s_msgtosend recdmsg={},tobesent={};
    struct sockaddr_ll saddr;
    struct s_odrmsg *recdodrpkt;
    void *buffer = (void *)malloc(ETH_FRAME_LEN);
    fd_set rset;

    if(argc < 2)
    {
        printf("Usage error! Syntax: ./odr <staleness> \n");
        exit(-1);
    }
    else
        staleness = atoi(argv[1]);

    sockfd = Socket(AF_LOCAL, SOCK_DGRAM, 0);
    pfsock = Socket(PF_PACKET, SOCK_RAW, htons(USID_PROTO));

    unlink(ODRPATH);
    bzero(&odraddr,sizeof(odraddr));
    odraddr.sun_family = AF_LOCAL;
    strcpy(odraddr.sun_path, ODRPATH);
    Bind(sockfd, (SA *) &odraddr, sizeof(odraddr));

    // Initialize all linked lists head to NULL
    ptoshead = NULL;
    rthead = NULL;
    parkhead = NULL;
    rrhead = NULL;
    ifhead = NULL;
    rparkhead = NULL;
    bcastid = 0;
    printf("ODR started...\n");
    createIfList();
    printIfaces();

    strcpy(myip, getMyIP());
    printf("My canonical IP is %s\n\n",myip);
    strcpy(sendpath,"");
    addToPtoSTable(SERVPORT,SERVPATH);
    odrlen = sizeof(odraddr);

    for(;;)
    {
        FD_ZERO(&rset);
        FD_SET(sockfd,&rset);
        FD_SET(pfsock,&rset);
        maxfd = max(sockfd,pfsock) + 1;

        if( (select(maxfd,&rset,NULL,NULL,NULL)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
            {
                printf("Error in select, errno : %s ! Exitting the program\n",strerror(errno));
                exit(-1);
            }
        }
        if(FD_ISSET(sockfd,&rset))
        {
            odrlen = sizeof(recvfromaddr);
            n = recvfrom(sockfd, (char *)&recdmsg, sizeof(struct s_msgtosend), 0, (SA *)&recvfromaddr, &odrlen);
            strcpy(tobesent.sendline, recdmsg.sendline);
            tobesent.flag = recdmsg.flag;

            #ifdef DEBUG
            printf("ODR received API message %s for %s:%d,from filename:%s\n",
                    recdmsg.sendline,recdmsg.ipaddr,recdmsg.port,recvfromaddr.sun_path);
            #endif

            if( (tobesent.port = portFromTable(recvfromaddr.sun_path)) == -1) // Entry to client port not yet added to pTos Map
            {
                addToPtoSTable(cliport, recvfromaddr.sun_path);
                #ifdef DEBUG
                printf("Adding %s to pTOs table,len %ld\n",recvfromaddr.sun_path,strlen(recvfromaddr.sun_path));
                printpTosTable();
                #endif
                tobesent.port = cliport++;
            }

            bzero(&sendtoaddr, sizeof(sendtoaddr));
            sendtoaddr.sun_family = AF_LOCAL;
            n = pathFromTable(recdmsg.port, sendpath);
            strcpy(sendtoaddr.sun_path, sendpath);

            if(recdmsg.flag == 1)
                deleteFromRoutingTable(recdmsg.ipaddr);

            if(strcmp(myip, recdmsg.ipaddr) == 0)
            {
                /* The message is meant for the same IP as ODR. So no need to look up routing table */
                strcpy(tobesent.ipaddr, myip);
                #ifdef DEBUG
                printf("Sending msg %s for ip %s:%d,to file:%s\n",
                        tobesent.sendline, tobesent.ipaddr, tobesent.port,sendtoaddr.sun_path);
                #endif
                n = sendto(sockfd, (char *)&tobesent, sizeof(struct s_msgtosend), 0, (SA *)&sendtoaddr, sizeof(sendtoaddr));
                if(n < 0)
                {
                    printf("ODR unable to send, endpoint unreachable! Server/Client possibly not running\n");
                    continue; // Back to select
                }
            }
            else
            {
                if(lookupRoutingTable(recdmsg.ipaddr, dstmac, &iface) == FOUND)
                {
                    sendApplicationMessage(pfsock, myip, tobesent.port, recdmsg, dstmac, iface, 1);
                    #ifdef DEBUG
                    printf("Message sent to ODR!\n");
                    #endif
                }
                else
                {
                    parkApplicationMessage(myip, tobesent.port, recdmsg, 1, recdmsg.ipaddr); //We will send this message after RREP is recieved.
                    #ifdef DEBUG
                    printf("Parked msg from %s:%d,hop:%d to %s:%d,msg:%s\n",
                            myip,tobesent.port,1,recdmsg.ipaddr,recdmsg.port,recdmsg.sendline);
                    printf("Destination IP not found in routing table, flooding RREQ\n");
                    #endif
                    //Send a routing request for the dst canonical IP.
                    floodRREQ(pfsock, myip, recdmsg.ipaddr, 1, 0, -1, bcastid++, recdmsg.flag);
                }
            }
        }

        if(FD_ISSET(pfsock,&rset))
        {
            #ifdef DEBUG
            printf("Message recieved from ODR!\n");
            #endif
            slen = sizeof(saddr);

            if( (n = recvfrom(pfsock, buffer, ETH_FRAME_LEN, 0, (SA *)&saddr, &slen)) < 0)
            {
                printf("Error receiving packet, errno: %s !Exitting the program\n",strerror(errno));
                exit(-1);
            }
            else
            {
                char macaddr[18];
                #ifdef DEBUG
                printf("Received packet from interface %d\n",saddr.sll_ifindex);
                printf("From src mac: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
                        saddr.sll_addr[0],saddr.sll_addr[1],saddr.sll_addr[2],saddr.sll_addr[3],saddr.sll_addr[4],saddr.sll_addr[5]);
                #endif
                sprintf(macaddr,"%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
                        saddr.sll_addr[0],saddr.sll_addr[1],saddr.sll_addr[2],saddr.sll_addr[3],saddr.sll_addr[4],saddr.sll_addr[5]);
                void *data = buffer + 14;
                recdodrpkt = (struct s_odrmsg*) data;

                switch(recdodrpkt->type)
                {
                    case TYPE_RREQ:
                        processRREQ(sockfd, pfsock, recdodrpkt, saddr.sll_ifindex, macaddr);
                        break;
                    case TYPE_RREP:
                        processRREP(sockfd, pfsock, recdodrpkt, saddr.sll_ifindex, macaddr);
                        break;
                    case TYPE_DATA:
                        processODRDataPkt(sockfd, pfsock, recdodrpkt, saddr.sll_ifindex, macaddr);
                        break;
                    default:
                        printf("ODR received possibly corrupt packet! Ignoring received message\n");
                }
            }
        } //end of FD_ISSET
    } //end of for(;;)
    return 0;
}

void createIfList()
{
	struct hwa_info	*hwa, *hwahead;
	struct sockaddr	*sa;
	char   *ptr, name[16],ipaddr[16],hwaddr[18],tmp[4];
	int    i, prflag,ifindex;

	for (hwahead = hwa = Get_hw_addrs(); hwa != NULL; hwa = hwa->hwa_next) 
    {
        strcpy(name,hwa->if_name);
		
		if ( (sa = hwa->ip_addr) != NULL)
            strcpy(ipaddr,(char *)Sock_ntop_host(sa, sizeof(*sa)));
				
		prflag = 0;
		i = 0;
		do {
			if (hwa->if_haddr[i] != '\0') {
				prflag = 1;
				break;
			}
		} while (++i < IF_HADDR);

        strcpy(hwaddr,"");
		if (prflag) {
			ptr = hwa->if_haddr;
			i = IF_HADDR;
			do {
                int j;
				sprintf(tmp,"%.2x%s", *ptr++ & 0xff, (i == 1) ? "" : ":");
                if(i == 1)
                    strncat(hwaddr,tmp,2);
                else
                    strncat(hwaddr,tmp,3);
			} while (--i > 0);
		}

        hwaddr[18] = 0;
        ifindex = hwa->if_index;
        addToIfList(name,ipaddr,hwaddr,ifindex);
	}

	free_hwa_info(hwahead);
    return;
}

char *getMyIP()
{
    struct s_ifList *temp=ifhead;
    if(temp == NULL)
        return;
    while(temp != NULL)
    {
        if(strcmp(temp->name, "eth0") == 0)
            return(temp->ipaddr);
        temp = temp->next;
    }
    return;
}

char *findhostname(char *ipaddr)
{
    struct hostent *he;
    struct in_addr addrinfo;
    inet_pton(AF_INET, ipaddr, &addrinfo);
    he = gethostbyaddr(&addrinfo, sizeof(addrinfo), AF_INET);

    return(he->h_name);
}

void addToIfList(char *name, char *ipaddr, char *hwaddr, int ifindex)
{
    struct s_ifList *temp;
    struct s_ifList *newnode =
        (struct s_ifList*)malloc(sizeof(struct s_ifList));

    newnode->ifindex = ifindex;
    strcpy(newnode->name,name);
    strcpy(newnode->ipaddr,ipaddr);
    strcpy(newnode->hwaddr,hwaddr);
    if(ifhead!=NULL)
    {
        temp=ifhead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        ifhead = newnode;
    }
    newnode->next=NULL;
}

void printIfaces()
{
    struct s_ifList *temp=ifhead;
    printf("Interfaces on this node:\n");
    if(temp==NULL)
        return;
    while(temp != NULL)
    {
        printf("%d: IF Name:%s\tHW Addr:%s\tIP Addr:%s\n",
                temp->ifindex, temp->name, temp->hwaddr, temp->ipaddr);
        temp=temp->next;
    }
    printf("\n");
}

void updateTimestamp(char *dstIP)
{
    struct s_routingtable *temp=rthead;
    if(temp==NULL)
        return;
    while(temp != NULL)
    {
        if(strcmp(temp->dstIP,dstIP) == 0)
        {
            temp->timestamp = time(NULL);
            return;
        }
        temp=temp->next;
    }
    return;
}

void addToRoutingTable(char *dstIP, int iface, char *macaddr, int numhops, time_t timestamp)
{
    struct s_routingtable *temp;
    struct s_routingtable *newnode =
        (struct s_routingtable*)malloc(sizeof(struct s_routingtable));

    newnode->iface = iface;
    newnode->numhops = numhops;
    newnode->timestamp = timestamp;
    strcpy(newnode->dstIP,dstIP);
    strcpy(newnode->macaddr,macaddr);

    if(rthead!=NULL)
    {
        temp=rthead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        rthead = newnode;
    }
    newnode->next=NULL;
    printf("After adding route to %s, routing table is as below:\n",dstIP);
    printf("===============\n");
    printRoutingTable();
    printf("===============\n");
}

int getHopCount(char *dstIP)
{
    struct s_routingtable *temp=rthead;
    if(temp==NULL)
        return -1;
    while(temp != NULL)
    {
        if(strcmp(temp->dstIP,dstIP) == 0)
        {
            return temp->numhops;
        }
        temp=temp->next;
    }
    return -1;
}

int lookupRoutingTable(char *dstIP, char *dstMac, int *iface)
{
    int todel = 0;
    time_t now;
    struct s_routingtable *temp=rthead;
    if(temp==NULL)
        return NOTFOUND;
    while(temp != NULL)
    {
        if(strcmp(temp->dstIP,dstIP) == 0)
        {
            time(&now);
            if( difftime(now, temp->timestamp) > staleness)
            {
                #ifdef DEBUG
                printf("Deleting stale entry\n");
                #endif
                todel = 1;
                break;
            }
            strcpy(dstMac,temp->macaddr);
            *iface = temp->iface;
            return FOUND;
        }
        temp=temp->next;
    }
    if(todel == 1)
        deleteFromRoutingTable(dstIP);
    return NOTFOUND;
}

void deleteFromRoutingTable(char *srcIP)
{
    struct s_routingtable *temp,*todelete;
    temp = rthead;
    if(temp == NULL)
        return;
    if(strcmp(rthead->dstIP,srcIP) == 0)
    {
        rthead=temp->next;
        todelete = temp;
    }
    else
    {
        while(temp->next !=NULL)
        {
            if( (strcmp(temp->next->dstIP,srcIP) == 0) )
            {
                todelete = temp->next;
                temp->next = todelete->next;
                break;
            }
            temp = temp->next;
        }
    }
    todelete = NULL;
    free(todelete);
}

void printRoutingTable()
{
    struct s_routingtable *temp=rthead;
    if(temp==NULL)
        return;
    printf("Destination IP\tInterface\tNexthop MAC\t\tNumhops\tTimestamp\n");
    while(temp != NULL)
    {
        printf("%s\t%d\t\t%s\t%d\t%s\n",
                temp->dstIP,temp->iface,temp->macaddr,temp->numhops,ctime(&(temp->timestamp)));
        temp=temp->next;
    }
    printf("\n");
}

void addToPtoSTable(int port, char *sunpath)
{
    struct s_porttosunpath *temp;
    struct s_porttosunpath *newnode =
        (struct s_porttosunpath*)malloc(sizeof(struct s_porttosunpath));

    newnode->port = port;
    strncpy(newnode->sunpath,sunpath,strlen(sunpath));
    newnode->sunpath[16] = 0;
    if(ptoshead!=NULL)
    {
        temp=ptoshead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        ptoshead = newnode;
    }
    newnode->next=NULL;
}

int portFromTable(char *sunpath)
{
    struct s_porttosunpath *temp=ptoshead;
    if(temp==NULL)
        return(-1);
    while(temp != NULL)
    {
        if(strcmp(temp->sunpath, sunpath) == 0)
            return(temp->port);
        temp=temp->next;
    }
    return (-1);
}

int pathFromTable(int port,char *sunpath)
{
    struct s_porttosunpath *temp=ptoshead;
    if(temp==NULL)
        return(-1);
    while(temp != NULL)
    {
        if(temp->port == port)
        {
            strcpy(sunpath, temp->sunpath);
            return(1);
        }
        temp=temp->next;
    }
    return(-1);
}

void printpTosTable()
{
    struct s_porttosunpath *temp=ptoshead;
    printf("P to S table:\n");
    if(temp==NULL)
        return;
    while(temp != NULL)
    {
        printf("p:%d,spath:%s\n",temp->port,temp->sunpath);
        temp=temp->next;
    }
    printf("\n");
}

void parkApplicationMessage(char *srcip, int srcport, struct s_msgtosend recdmsg, int numhops, char *dstip)
{
    struct s_parkedmsgs *temp;
    struct s_parkedmsgs *newnode =
        (struct s_parkedmsgs*)malloc(sizeof(struct s_parkedmsgs));

    newnode->srcport = srcport;
    newnode->msgrecvd = recdmsg;
    newnode->numhops = numhops;
    strcpy(newnode->dstIP, dstip);
    strcpy(newnode->srcIP, srcip);
    if(parkhead!=NULL)
    {
        temp=parkhead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        parkhead = newnode;
    }
    newnode->next=NULL;
}

void sendApplicationMessage(int pfsock, char *srcip, int sport, struct s_msgtosend recdmsg, char *dstmac, int iface, int numhops)
{
    int i;
    char srcname[100], dstname[100], myname[100];
    struct s_ifList *curr = ifhead;
    struct s_odrmsg odrmsg;
    struct sockaddr_ll saddress;
    void* buffer = (void*)malloc(ETH_FRAME_LEN);
    unsigned char* etherhead = buffer;
    unsigned char* data = buffer + 14;
    struct ethhdr *eh = (struct ethhdr *)etherhead;
    int send_result = 0;
    unsigned char src_mac[6]; // = {0x00, 0x01, 0x02, 0xFA, 0x70, 0xAA};
    unsigned char dest_mac[6]; // = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /*prepare sockaddr_ll*/
    saddress.sll_family   = PF_PACKET;    
    saddress.sll_protocol = htons(ETH_P_IP);  
    saddress.sll_hatype   = ARPHRD_ETHER;
    saddress.sll_pkttype  = PACKET_OTHERHOST;
    saddress.sll_halen    = ETH_ALEN;     

    for(;curr!=NULL;curr=curr->next)
    {
        if(curr->ifindex != iface)
            continue;
        sscanf(curr->hwaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &src_mac[0],&src_mac[1],&src_mac[2],&src_mac[3],&src_mac[4],&src_mac[5]);
        sscanf(dstmac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &dest_mac[0],&dest_mac[1],&dest_mac[2],&dest_mac[3],&dest_mac[4],&dest_mac[5]);
        saddress.sll_ifindex = curr->ifindex;
        for(i=0;i<6;i++)
            saddress.sll_addr[i] = src_mac[i];
        saddress.sll_addr[6] = 0x00;
        saddress.sll_addr[7] = 0x00;

        memcpy((void*)buffer, (void*)dest_mac, ETH_ALEN);
        memcpy((void*)(buffer+ETH_ALEN), (void*)src_mac, ETH_ALEN);
        eh->h_proto = htons(USID_PROTO);
        /*fill the frame with data*/
        odrmsg.type = TYPE_DATA;
        strcpy(odrmsg.srcIP, srcip);
        odrmsg.srcport = sport;
        strcpy(odrmsg.dstIP, recdmsg.ipaddr);
        odrmsg.dstport = recdmsg.port;
        odrmsg.hopcnt = numhops;
        odrmsg.size = sizeof(recdmsg.sendline);
        strcpy(odrmsg.payload, recdmsg.sendline);

        memcpy((void *)data, (void *)&odrmsg, sizeof(struct s_odrmsg));
        /*send the packet*/
        send_result = sendto(pfsock, buffer, ETH_FRAME_LEN, 0, 
                (struct sockaddr*)&saddress, sizeof(saddress));
        if (send_result == -1)
        {
            printf("Send error, errno:%s! Quitting the program\n",strerror(errno));
            continue;
        }
        strcpy(srcname, findhostname(odrmsg.srcIP));
        strcpy(dstname, findhostname(odrmsg.dstIP));
        strcpy(myname, findhostname(getMyIP()));
        printf("ODR at node %s : sending frame hdr src %s , dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
                myname, myname, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
        printf("\t\tODR msg type %d src %s dest %s\n", odrmsg.type, srcname, dstname);
    }
    return;
}

void floodRREQ(int pfsock, char *srcip, char* dstip, int numhops, int alreadysent, int dontfloodindex, int broadcast, int rediscovery)
{
    int i;
    char srcname[100], dstname[100], myname[100];
    struct s_ifList *curr = ifhead;
    struct s_odrmsg odrmsg;
    struct sockaddr_ll saddress;
    void* buffer = (void*)malloc(ETH_FRAME_LEN);
    unsigned char* etherhead = buffer;
    unsigned char* data = buffer + 14;
    struct ethhdr *eh = (struct ethhdr *)etherhead;
    int send_result = 0;
    unsigned char src_mac[6]; // = {0x00, 0x01, 0x02, 0xFA, 0x70, 0xAA};
    unsigned char dest_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /*prepare sockaddr_ll*/
    saddress.sll_family   = PF_PACKET;    
    saddress.sll_protocol = htons(ETH_P_IP);  
    saddress.sll_hatype   = ARPHRD_ETHER;
    saddress.sll_pkttype  = PACKET_OTHERHOST;
    saddress.sll_halen    = ETH_ALEN;     

    for(;curr!=NULL;curr=curr->next)
    {
        //Flood all ifaces except eth0,lo and the interface where the RREQ was received from
        if( (strcmp(curr->name, "eth0") == 0) || (strcmp(curr->name, "lo") == 0) 
            || curr->ifindex == dontfloodindex)
            continue;
        sscanf(curr->hwaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &src_mac[0],&src_mac[1],&src_mac[2],&src_mac[3],&src_mac[4],&src_mac[5]);
        saddress.sll_ifindex = curr->ifindex;
        for(i=0;i<6;i++)
            saddress.sll_addr[i] = src_mac[i];
        saddress.sll_addr[6] = 0x00;
        saddress.sll_addr[7] = 0x00;

        memcpy((void*)buffer, (void*)dest_mac, ETH_ALEN);
        memcpy((void*)(buffer+ETH_ALEN), (void*)src_mac, ETH_ALEN);
        eh->h_proto = htons(USID_PROTO);
        /*fill the frame with data*/
        odrmsg.type = TYPE_RREQ;
        strcpy(odrmsg.srcIP, srcip);
        strcpy(odrmsg.dstIP, dstip);
        odrmsg.hopcnt = numhops;
        odrmsg.rreqBcastID = broadcast;
        odrmsg.rrepAlreadySent = alreadysent;
        odrmsg.rediscovery = rediscovery;
        strcpy(odrmsg.payload,"");

        memcpy((void *)data, (void *)&odrmsg, sizeof(struct s_odrmsg));
        /*send the packet*/
        send_result = sendto(pfsock, buffer, ETH_FRAME_LEN, 0, 
                (struct sockaddr*)&saddress, sizeof(saddress));
        if (send_result == -1)
        {
            printf("Send error, errno:%s! Quitting the program\n",strerror(errno));
            continue;
        }
        strcpy(srcname, findhostname(odrmsg.srcIP));
        strcpy(dstname, findhostname(odrmsg.dstIP));
        strcpy(myname, findhostname(getMyIP()));
        printf("ODR at node %s : sending frame hdr src %s , dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
                myname, myname, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
        printf("\t\tODR msg type %d src %s dest %s\n", odrmsg.type, srcname, dstname);
    }
    return;
}

void processODRDataPkt(int sockfd, int pfsock, struct s_odrmsg *recdodrpkt, int iface, char *macaddr)
{
    struct s_msgtosend tobesent={};
    struct sockaddr_un sendtoaddr;
    char dstmac[18];
    int n, outiface;
    char sendpath[256];

    strcpy(sendpath,"");

    #ifdef DEBUG
    printf("We received:type:%d from srcIP:%s, srcport:%d dstip:%s dstport:%d hc:%d size: %d msg: %s\n",
            recdodrpkt->type,recdodrpkt->srcIP,recdodrpkt->srcport,recdodrpkt->dstIP,recdodrpkt->dstport,
            recdodrpkt->hopcnt,recdodrpkt->size,recdodrpkt->payload);
    #endif

    if( lookupRoutingTable(recdodrpkt->srcIP, dstmac, &outiface) == NOTFOUND)
        addToRoutingTable(recdodrpkt->srcIP, iface, macaddr, recdodrpkt->hopcnt, time(NULL));
    else
        updateTimestamp(recdodrpkt->srcIP);

    if( (strcmp(recdodrpkt->dstIP, getMyIP())) == 0 )
    {
        strcpy(tobesent.ipaddr, recdodrpkt->srcIP);
        tobesent.port = recdodrpkt->srcport;
        strcpy(tobesent.sendline, recdodrpkt->payload);

        bzero(&sendtoaddr, sizeof(sendtoaddr));
        sendtoaddr.sun_family = AF_LOCAL;
        n = pathFromTable(recdodrpkt->dstport, sendpath);
        strcpy(sendtoaddr.sun_path, sendpath);

        n = sendto(sockfd, (char *)&tobesent, sizeof(struct s_msgtosend), 0, (SA *)&sendtoaddr, sizeof(sendtoaddr));
        if(n < 0)
        {
            printf("ODR unable to send, endpoint unreachable! Server/Client possibly not running\n");
            return;
        }
    }
    else
    {
        int numhops = recdodrpkt->hopcnt + 1;
        strcpy(tobesent.ipaddr, recdodrpkt->dstIP);
        tobesent.port = recdodrpkt->dstport;
        strcpy(tobesent.sendline, recdodrpkt->payload);

        strcpy(dstmac,"");
        if( lookupRoutingTable(recdodrpkt->dstIP, dstmac, &outiface) == FOUND)
        {
            /* Forward the message as dictated by the routing table */
            sendApplicationMessage(pfsock, recdodrpkt->srcIP, recdodrpkt->srcport, tobesent, dstmac, outiface,numhops);
        }
        else
        {
            /* No routing entry. Park and send RREQ */ 
            strcpy(tobesent.ipaddr, recdodrpkt->dstIP);
            tobesent.port = recdodrpkt->dstport;
            strcpy(tobesent.sendline, recdodrpkt->payload);
            parkApplicationMessage(recdodrpkt->srcIP, recdodrpkt->srcport, tobesent, recdodrpkt->hopcnt, recdodrpkt->dstIP);
            #ifdef DEBUG
            printf("Parked msg from %s:%d to %s:%d,hop:%d,msg:%s\n",
                recdodrpkt->srcIP,recdodrpkt->srcport,recdodrpkt->dstIP,tobesent.port,recdodrpkt->hopcnt,tobesent.sendline);
            #endif
            //We will send this message after RREP is recieved.
            floodRREQ(pfsock, getMyIP(), recdodrpkt->dstIP, 1, 0, -1, bcastid++, 0);
        }
    }
}

void processRREQ(int sockfd, int pfsock, struct s_odrmsg *rreqmsg, int iface, char *macaddr)
{
    int outiface, find, newsource;
    char dstmac[18];
    #ifdef DEBUG
    printf("Received RREQ %d from %s for %s ,flag: %d, numhops:%d rediscovery:%d\n",
            rreqmsg->rreqBcastID, rreqmsg->srcIP, rreqmsg->dstIP, rreqmsg->rrepAlreadySent, rreqmsg->hopcnt, rreqmsg->rediscovery);
    #endif

    if(strcmp(rreqmsg->srcIP, getMyIP()) == 0)
    {
        #ifdef DEBUG
        printf("Received my own RREQ! Ignoring\n");
        #endif
        return;
    }

    if(rreqmsg->rediscovery == 1)
    {
        deleteFromRoutingTable(rreqmsg->srcIP); //This will force us to add the current route
        deleteFromRoutingTable(rreqmsg->dstIP); //This will force us to flood RREQ till the destination
    }

    /* First add the reverse route if required */
    find = lookupRREQList(rreqmsg->srcIP,rreqmsg->rreqBcastID);
    if( find == NOTFOUND || find == HIGHER ) 
    {
        if(find == HIGHER)
            deleteFromRoutingTable(rreqmsg->srcIP);
        /* First rreq we received for this source. Send RREP or forward RREQ as necessary */
        addtoRREQList(rreqmsg->rreqBcastID, rreqmsg->srcIP);
        addToRoutingTable(rreqmsg->srcIP, iface, macaddr, rreqmsg->hopcnt, time(NULL));
        newsource = 1;
    }
    else if(find == SAME)
    {
       #ifdef DEBUG
       printf("Received RREQ with same bcastid,check hopcnt.\n");
       #endif
       if(getHopCount(rreqmsg->srcIP) > rreqmsg->hopcnt)
       {
            //This is a better route. Make this the new route
            deleteFromRoutingTable(rreqmsg->srcIP);
            addToRoutingTable(rreqmsg->srcIP, iface, macaddr, rreqmsg->hopcnt, time(NULL));
            newsource = 1;
       }
       else
       {
           updateTimestamp(rreqmsg->srcIP);
           newsource = 0;
       }
    }
    else if(find == FOUND)
    {
        #ifdef DEBUG
        printf("This is a RREQ with lower bcast id,so just updating timestamp\n");
        #endif
        updateTimestamp(rreqmsg->srcIP);
        newsource = 0;
    }

    /* Now send RREP or flood RREQ further as required */
    if( (lookupRoutingTable(rreqmsg->dstIP, dstmac, &outiface) == FOUND)
            || (strcmp(rreqmsg->dstIP,getMyIP()) == 0) )
    {
        char rrepIP[16];
        strcmp(rreqmsg->dstIP,getMyIP()) == 0 ? strcpy(rrepIP, getMyIP()) : strcpy(rrepIP, rreqmsg->dstIP);
        if(rreqmsg->rrepAlreadySent != 1)
        {
            #ifdef DEBUG
            printf("We have a route for dst! Will send a RREP\n");
            #endif
            sendRREP(pfsock, rrepIP , rreqmsg->srcIP, 1, iface, macaddr, rreqmsg->rediscovery);
        }
        //This is a new source,so flood the RREQ with alreadySentFlag set to 1
        if( newsource == 1 && (strcmp(rreqmsg->dstIP,getMyIP()) != 0) )
        {
            #ifdef DEBUG
            printf("Dst not me, flooding further!\n");
            #endif
            floodRREQ(pfsock, rreqmsg->srcIP, rreqmsg->dstIP, (rreqmsg->hopcnt)+1, 1 ,iface, rreqmsg->rreqBcastID, rreqmsg->rediscovery);
        }
        else
        {
            #ifdef DEBUG
            printf("I am the destination! Sent RREP and no further flooding\n");
            #endif
        }
    }
    else
    {
        #ifdef DEBUG
        printf("No route to destination available, continue to flood rreq\n");
        #endif
        //Flood RREQ on all ifaces except the iface we received it on
        floodRREQ(pfsock, rreqmsg->srcIP, rreqmsg->dstIP, (rreqmsg->hopcnt)+1,
                    rreqmsg->rrepAlreadySent, iface, rreqmsg->rreqBcastID, rreqmsg->rediscovery);
    }
}

void addtoRREQList(int bcastid, char *srcIP)
{
    struct s_rreqlist *temp;
    struct s_rreqlist *newnode =
        (struct s_rreqlist*)malloc(sizeof(struct s_rreqlist));

    newnode->rreqBcastID = bcastid;
    strcpy(newnode->srcIP, srcIP);
    if(rrhead!=NULL)
    {
        temp=rrhead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        rrhead = newnode;
    }
    newnode->next=NULL;
}

int lookupRREQList(char *srcIP, int bcastid)
{
    struct s_rreqlist *temp=rrhead;
    if(temp==NULL)
        return NOTFOUND;
    while(temp != NULL)
    {
        if(strcmp(temp->srcIP,srcIP) == 0)
        {
            if(bcastid < temp->rreqBcastID)
                return FOUND;
            else if(bcastid > temp->rreqBcastID)
            {
                deletefromRREQList(srcIP);
                return HIGHER;
            }
            else if(bcastid == temp->rreqBcastID)
                return SAME;
        }
        temp=temp->next;
    }
    return NOTFOUND;
}

void deletefromRREQList(char *srcIP)
{
    struct s_rreqlist *temp,*todelete;
    temp = rrhead;
    if(temp == NULL)
        return;
    if(strcmp(rrhead->srcIP,srcIP) == 0)
    {
        rrhead=temp->next;
        todelete = temp;
    }
    else
    {
        while(temp->next !=NULL)
        {
            if( (strcmp(temp->next->srcIP,srcIP) == 0) )
            {
                todelete = temp->next;
                temp->next = todelete->next;
                break;
            }
            temp = temp->next;
        }
    }
    todelete = NULL;
    free(todelete);
}

void sendRREP(int pfsock, char *srcIP, char *dstIP, int numhops, int iface, char *macaddr, int rediscovery)
{
    int i;
    char srcname[100], dstname[100], myname[100];
    struct s_ifList *curr = ifhead;
    struct s_odrmsg odrmsg;
    struct sockaddr_ll saddress;
    void* buffer = (void*)malloc(ETH_FRAME_LEN);
    unsigned char* etherhead = buffer;
    unsigned char* data = buffer + 14;
    struct ethhdr *eh = (struct ethhdr *)etherhead;
    int send_result = 0;
    unsigned char src_mac[6]; // = {0x00, 0x01, 0x02, 0xFA, 0x70, 0xAA};
    unsigned char dest_mac[6]; // = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /*prepare sockaddr_ll*/
    saddress.sll_family   = PF_PACKET;    
    saddress.sll_protocol = htons(ETH_P_IP);  
    saddress.sll_hatype   = ARPHRD_ETHER;
    saddress.sll_pkttype  = PACKET_OTHERHOST;
    saddress.sll_halen    = ETH_ALEN;     

    for(;curr!=NULL;curr=curr->next)
    {
        if(curr->ifindex != iface)
            continue;
        sscanf(curr->hwaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &src_mac[0],&src_mac[1],&src_mac[2],&src_mac[3],&src_mac[4],&src_mac[5]);
        sscanf(macaddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &dest_mac[0],&dest_mac[1],&dest_mac[2],&dest_mac[3],&dest_mac[4],&dest_mac[5]);
        saddress.sll_ifindex = curr->ifindex;
        for(i=0;i<6;i++)
            saddress.sll_addr[i] = src_mac[i];
        saddress.sll_addr[6] = 0x00;
        saddress.sll_addr[7] = 0x00;

        memcpy((void*)buffer, (void*)dest_mac, ETH_ALEN);
        memcpy((void*)(buffer+ETH_ALEN), (void*)src_mac, ETH_ALEN);
        eh->h_proto = htons(USID_PROTO);
        /*fill the frame with data*/
        odrmsg.type = TYPE_RREP;
        strcpy(odrmsg.srcIP, srcIP);
        strcpy(odrmsg.dstIP, dstIP);
        odrmsg.hopcnt = numhops;
        odrmsg.rediscovery = rediscovery;

        memcpy((void *)data, (void *)&odrmsg, sizeof(struct s_odrmsg));
        /*send the packet*/
        send_result = sendto(pfsock, buffer, ETH_FRAME_LEN, 0, 
                (struct sockaddr*)&saddress, sizeof(saddress));
        if (send_result == -1)
        {
            printf("Send error, errno:%s! Quitting the program\n",strerror(errno));
            continue;
        }
        strcpy(srcname, findhostname(odrmsg.srcIP));
        strcpy(dstname, findhostname(odrmsg.dstIP));
        strcpy(myname, findhostname(getMyIP()));
        printf("ODR at node %s : sending frame hdr src %s , dest: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
                myname, myname, dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
        printf("\t\tODR msg type %d src %s dest %s\n", odrmsg.type, srcname, dstname);
    }
    return;
}

void deletefromPark(char *srcIP)
{
    struct s_parkedmsgs *temp,*todelete;
    temp = parkhead;
    if(temp == NULL)
        return;
    if(strcmp(parkhead->dstIP,srcIP) == 0)
    {
        parkhead=temp->next;
        todelete = temp;
    }
    else
    {
        while(temp->next !=NULL)
        {
            if( (strcmp(temp->next->dstIP,srcIP) == 0) )
            {
                #ifdef DEBUG
                printf("Deleting parked %s\n",temp->next->dstIP);
                #endif
                todelete = temp->next;
                temp->next = todelete->next;
                break;
            }
            temp = temp->next;
        }
    }
    todelete = NULL;
    free(todelete);
}

void parkRREP(struct s_odrmsg rrep)
{
    struct s_rreppark *temp;
    struct s_rreppark *newnode =
        (struct s_rreppark*)malloc(sizeof(struct s_rreppark));

    newnode->rrep = rrep;
    if(rparkhead!=NULL)
    {
        temp=rparkhead;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        rparkhead = newnode;
    }
    newnode->next=NULL;
}

void deletefromRREPPark(char *srcIP)
{
    struct s_rreppark *temp,*todelete;
    temp = rparkhead;
    if(temp == NULL)
        return;
    if(strcmp(rparkhead->rrep.dstIP,srcIP) == 0)
    {
        rparkhead=temp->next;
        todelete = temp;
    }
    else
    {
        while(temp->next !=NULL)
        {
            if( (strcmp(temp->next->rrep.dstIP,srcIP) == 0) )
            {
                #ifdef DEBUG
                printf("Deleting parked %s\n",temp->next->rrep.dstIP);
                #endif
                todelete = temp->next;
                temp->next = todelete->next;
                break;
            }
            temp = temp->next;
        }
    }
    todelete = NULL;
    free(todelete);
}

void processRREP(int sockfd, int pfsock, struct s_odrmsg *rrepmsg, int iface, char *macaddr)
{
    int outiface;
    char dstmac[16];
    struct s_parkedmsgs *curr;
    struct s_rreppark *rcurr;
    #ifdef DEBUG
    printf("Received RREP of %s originated by %s, hopcnt %d, rediscovery:%d\n",
             rrepmsg->srcIP, rrepmsg->dstIP, rrepmsg->hopcnt, rrepmsg->rediscovery);
    #endif

    if(rrepmsg->rediscovery == 1)
        deleteFromRoutingTable(rrepmsg->srcIP); //Force to add the current route

    if( lookupRoutingTable(rrepmsg->srcIP, dstmac, &outiface) == NOTFOUND)
        addToRoutingTable(rrepmsg->srcIP, iface, macaddr, rrepmsg->hopcnt, time(NULL));
    else
    {
        if(getHopCount(rrepmsg->srcIP) > rrepmsg->hopcnt)
        {
            //This is a better route. Make this the new route
            deleteFromRoutingTable(rrepmsg->srcIP);
            addToRoutingTable(rrepmsg->srcIP, iface, macaddr, rrepmsg->hopcnt, time(NULL));
        }
        else
            updateTimestamp(rrepmsg->srcIP);
    }

    outiface = -1;
    strcpy(dstmac,"");

    if(strcmp(rrepmsg->dstIP,getMyIP()) == 0)
    {
        #ifdef DEBUG
        printf("I received RREP, sending out parked messages\n");
        #endif
        curr = parkhead;
        rcurr = rparkhead;

        //Send out all parked RREPs
        while(rcurr != NULL)
        {
            if(strcmp(rcurr->rrep.dstIP, rrepmsg->srcIP) == 0)
            {
                #ifdef DEBUG
                printf("Sending RREP from %s to %s,hop %d\n", 
                        rcurr->rrep.srcIP,rcurr->rrep.dstIP,rcurr->rrep.hopcnt);
                #endif
                sendRREP(pfsock, rcurr->rrep.srcIP, rcurr->rrep.dstIP, (rcurr->rrep.hopcnt)+1, iface, macaddr, rcurr->rrep.rediscovery);
                deletefromRREPPark(rrepmsg->srcIP);
            }
            rcurr = rcurr->next;
        }
        
        //Send out all parked data messages
        while(curr != NULL)
        {
            if(strcmp(curr->dstIP, rrepmsg->srcIP) == 0)
            {
                #ifdef DEBUG
                printf("Sending from %s:%d to %s:%d msg:%s, hop:%d\n",
                    curr->srcIP,curr->srcport,curr->msgrecvd.ipaddr,curr->msgrecvd.port,curr->msgrecvd.sendline,curr->numhops);
                #endif
                sendApplicationMessage(pfsock, curr->srcIP, curr->srcport, curr->msgrecvd, macaddr, iface, (curr->numhops)+1);
                deletefromPark(rrepmsg->srcIP);
            }
            curr = curr->next;
        }

    }
    else
    {
        if( lookupRoutingTable(rrepmsg->dstIP, dstmac, &outiface) == FOUND)
        {
            #ifdef DEBUG
            printf("I have a route to forward RREP to %s on %s, %d\n",rrepmsg->dstIP,dstmac,outiface);
            #endif
            sendRREP(pfsock, rrepmsg->srcIP, rrepmsg->dstIP, (rrepmsg->hopcnt)+1, outiface, dstmac, rrepmsg->rediscovery);
        }
        else
        {
            printf("Staleness value too low! Parking RREP and flooding RREQ\n");
            #ifdef DEBUG
            printf("No route to forward RREP.Need to park and flood rreq\n");
            #endif
            parkRREP(*rrepmsg);
            floodRREQ(pfsock, getMyIP(), rrepmsg->dstIP, 1, 0, -1, bcastid++, 0);
        }
    }
}
