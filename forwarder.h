#ifndef __FORWARDER__
#define __FORWARDER__

#define _GNU_SOURCE
#include <poll.h>
#include <sys/un.h>
#include "de.h"

#define IN_SOCKET   0
#define OUT_SOCKET  1
#define CNT_SOCKETS 2

#define IN_RDBUF	0
#define IN_WRBUF	1
#define OUT_RDBUF	2
#define OUT_WRBUF	3
#define CNT_SOCBUF  4
// max number of saved parcels to send
#define CNT_FILES_SEND (10)

// configuration of the forward server
typedef struct {
    pthread_t thread;
    int port;
    int protocol;								// SOCK_STREAM | SOCK_DGRAM
    char name[STRLEN];					// name of the forwarder
    char server[STRLEN];				// IP or DNS-name of the servfer to
    char app[STRLEN];						// protocol of the messages
    void *library_handle;	// handle to shared library
    void (*terminal_decode)(char*, int, ST_ANSWER*);	// pointer to decode terminal message function
    int (*terminal_encode)(ST_RECORD*, int, char*, int); // pointer to encode terminal message function
    int sockets[CNT_SOCKETS];		// sockets
    struct pollfd pollset[CNT_SOCKETS];	// pull of the sockets
    struct sockaddr_un addr_un;			// unix socket struct
    char buffers[CNT_SOCBUF][SOCKET_BUF_SIZE];	// read & write buffers for sockets
} ST_FORWARDER;

// forwarding terminals list
typedef struct {
    char imei[SIZE_TRACKER_FIELD];
    char forward[SIZE_TRACKER_FIELD];	// name of the forwarder
} ST_FORWARD_TERMINAL;

// list of forward servers
typedef struct {
    ST_FORWARDER *forwarder;
    int count;
    ST_FORWARD_TERMINAL *list;	// list of forwarding terminals
    int listcount;
} ST_FORWARDERS;
extern ST_FORWARDERS stForwarders;		// glonassd.c

// message structure between worker & forward threads
typedef struct {
    char imei[SIZE_TRACKER_FIELD];	// ID of the terminal
    int encode;	// encode need flag
    int len;		// data length
    // char data[len];	// data
} ST_FORWARD_MSG;

void *forwarder_thread(void *st_forwarder);

#endif
