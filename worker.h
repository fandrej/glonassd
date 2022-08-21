#ifndef __WORKER__
#define __WORKER__

#include <mqueue.h>
#include "glonassd.h"

// max times reading 0 bytes from client socket
#define MAX_ERRORS (3)
#define MAX_FORWARDS (3)

// forwarder's attributes structure
typedef struct {
	int forward_socket; // socket for forwarding
	int forward_encode;	// flag for encode data into another protocol for forward
	int forward_index;	// index of forwarder in forwarders list
} ST_FORWARD_ATTR;

// worker structure
typedef struct {
	pthread_t thread;	// thread ID
	int client_socket;	// client (gps/glonass terminal) socket
	struct sockaddr_in client_addr;
	char ip[SIZE_TRACKER_FIELD];	// IP-address of terminal
	char imei[SIZE_TRACKER_FIELD];	// may be volatile!!!
	ST_LISTENER *listener;	// pointer to listener structure
	mqd_t db_queue;		// Posix IPC queue, created in database module (e.g. pg.c for PostgreSQL)
} ST_WORKER;

void *worker_thread(void *st_worker);

#endif
