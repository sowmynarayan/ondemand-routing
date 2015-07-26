#define NODEBUG
#include<string.h>
#include<stdlib.h>

#define ODRPATH "/tmp/odr7156"  // well known filename of ODR
#define SERVPATH "/tmp/serv7156" // well known filename of SERVER
#define SERVPORT 6108  //well known port of the server
#define TYPE_RREQ 0
#define TYPE_RREP 1
#define TYPE_DATA 2

struct s_ifList
{
   char name[16];
   char ipaddr[16];
   char hwaddr[18];
   int ifindex;
   struct s_ifList *next;
};
struct s_ifList *ifhead;

struct s_msgtosend
{
    char ipaddr[16];
    int port;
    char sendline[1024];
    int flag;
};

struct s_porttosunpath
{
    int port;
    char sunpath[100];
    struct s_porttosunpath *next;
};
struct s_porttosunpath *ptoshead;

struct s_routingtable
{
    char dstIP[16];
    int iface;
    char macaddr[18];
    int numhops;
    time_t timestamp;
    struct s_routingtable *next;
};
struct s_routingtable *rthead;

struct s_parkedmsgs
{
    char srcIP[16];
    int srcport;
    struct s_msgtosend msgrecvd;
    int numhops;
    char dstIP[16];
    struct s_parkedmsgs *next;
};
struct s_parkedmsgs *parkhead;

struct s_odrmsg
{
    int type;
    int rreqBcastID;
    int rrepAlreadySent;
    char srcIP[16];
    char dstIP[16];
    char rrepIP[16];
    int srcport;
    int dstport;
    int hopcnt;
    int size;
    int rediscovery;
    char payload[1024];
};

struct s_rreqlist
{
    int rreqBcastID;
    char srcIP[16];
    struct s_rreqlist *next;
};

struct s_rreqlist *rrhead;

struct s_rreppark
{
    struct s_odrmsg rrep;
    struct s_rreppark *next;
};

struct s_rreppark *rparkhead;
