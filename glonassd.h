#ifndef __GLONASSD__
#define __GLONASSD__

#include <stdio.h>	/* FILENAME_MAX */
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include "de.h"

//#define __DEBUG__ (1)

#define CONFIG_DEFAULT "glonassd.conf"
#define STRLEN (512)
#define DIRECTION_IN 0
#define DIRECTION_OUT 1
#define QUEUE_WORKER "/que_worker"  // http://linux.die.net/man/7/mq_overview

// startup parameters
typedef struct {
	char start_path[FILENAME_MAX];
	char config_path[FILENAME_MAX];
	char *cmd;
    char daemon;
} ST_PARAMS;
extern ST_PARAMS stParams;	// glonassd.c

/*
    main configuration
*/
// timer structure
// http://man7.org/linux/man-pages/man2/timer_create.2.html
typedef struct {
	timer_t id;
	time_t start;	// timer fire time
	int period;		// timer fire period
	char script_path[FILENAME_MAX];	// timer's running script file name
} ST_TIMER;
#define TIMERS_MAX 3	// max. timers count

/*
    main configuration structure
    ATTENTION: if change, recompile all, include *.so (pg, galileo, etc...)
*/
typedef struct {
	char listen[INET_ADDRSTRLEN];   // IP-address for listen the gps/glonass terminals
	char transmit[INET_ADDRSTRLEN];	// IP-address for retransmin signals of the terminals
	int log_enable;                 // flag: 0-disable, >0-enable logging & loglevel
	size_t log_maxsize;             // max size of log file in bytes
	char log_file[FILENAME_MAX];    // name log file
	char log_imei[SIZE_TRACKER_FIELD];    // logged imei
	char db_type[STRLEN];           // database type (pg/mysql/oracle etc)
	char db_host[STRLEN];           // database host
	int db_port;                    // database port
	char db_name[STRLEN];           // database name
	char db_schema[STRLEN];         // database schema name
	char db_user[STRLEN];           // database user
	char db_pass[STRLEN];           // database user's password
	int socket_queue;               // listener's socket queue size
	int socket_timeout;             // listener's socket timeout in seconds (max 600)
	int forward_timeout;            // forwarder's socket timeout in seconds (1-5)
	int forward_wait;	            // time between reconnect to server after connection lost
	char forward_files[FILENAME_MAX];    // forwarders files directory
	ST_TIMER timers[TIMERS_MAX];    // timers structure
} ST_CONFIG_SERVER;

// listener structure
typedef struct {
	char name[STRLEN];
	int protocol;		// SOCK_STREAM | SOCK_DGRAM
	int port;
	int enabled;
	int socket;
	int log_all;
	int log_err;
	void *library_handle;	// handle to shared library
	void (*terminal_decode)(char*, int, ST_ANSWER*, void*);	// pointer to decode terminal message function
	int (*terminal_encode)(ST_RECORD*, int, char*, int, void*); // pointer to encode terminal message function
} ST_LISTENER;

// list of the listeners
typedef struct {
	ST_LISTENER *listener;
	int count;
} ST_LISTENERS;

/*
    globals
*/
extern ST_CONFIG_SERVER stConfigServer;	// glonassd.c
extern ST_LISTENERS stListeners;		// glonassd.c
extern int graceful_stop;               // glonassd.c
extern int reconfigure;                 // glonassd.c
extern long GMT_diff;                   // glonassd.c
extern pthread_attr_t worker_thread_attr;      // glonassd.c
extern int attr_init;                   // glonassd.c

/*
    functions
*/
int cleanup(void);                 // glonassd.c

#endif
