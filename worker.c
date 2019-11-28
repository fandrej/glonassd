/*
    worker.c
    serve gps/glonass terminnals in separate thread
    caution:
	when terminal is retranslated from another server,
	then worker gets many imeis in one connection (volatile imei),
	else worker gets one imei permanetly (constant imei).
    note:
	1. We do not retranslate retranslated terminals.
	2. We retranslate one terminal to MAX_FORWARDS servers only.

    help:
    http://citforum.ru/programming/unix/threads/
    http://citforum.ru/programming/unix/threads_2/
    http://citforum.ru/programming/unix/proc_&_threads/
    https://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Thread_002dLocal.html#Thread_002dLocal
    http://www.redov.ru/kompyutery_i_internet/unix_vzaimodeistvie_processov/p3.php#metkadoc75
    http://www.ibm.com/developerworks/ru/library/l-memory-leaks/
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>	/* syscall */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <unistd.h> /* close, fork, usleep */
#include <errno.h>  /* errno */
#include <pthread.h>
//#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>			/* mq_open, O_* constants */
#include <mqueue.h>
#include "glonassd.h"
#include "forwarder.h"
#include "worker.h"
#include "lib.h"
#include "logger.h"

/*
    utilite functions
*/

// create/close forward socket
static int set_forward_socket(ST_WORKER *config, char *socket_path, int *psocket)
{
	struct sockaddr_un addr_un;

	if( socket_path ) {	// create and connect socket
		*psocket = socket(AF_UNIX, SOCK_DGRAM, 0);
		if( *psocket < 0 ) {
			logging("%s[%ld]: socket(*psocket) error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));
			return 0;
		}

		// connect socket
		memset(&addr_un, 0, sizeof(struct sockaddr_un));
		addr_un.sun_family = AF_UNIX;
		snprintf(addr_un.sun_path, sizeof(addr_un.sun_path), "/%s", socket_path);
		if( connect(*psocket, (struct sockaddr *)&addr_un, sizeof(struct sockaddr_un)) < 0 ) {
			logging("%s[%ld]: connect(*psocket, %s) error %d: %s\n", config->listener->name, syscall(SYS_gettid), addr_un.sun_path, errno, strerror(errno));
			close(*psocket);
			*psocket = BAD_OBJ;
			return 0;
		}

		return 1;
	} else {	// disconnect and close socket
		if( *psocket != BAD_OBJ ) {
			shutdown(*psocket, SHUT_RDWR);
			close(*psocket);
			*psocket = BAD_OBJ;
		}

		return 0;
	}
}
//------------------------------------------------------------------------------

// test for imei retranslation needded
unsigned int test_forward(ST_WORKER *config, char *imei, ST_FORWARD_ATTR *forward_attr)
{
	unsigned int i, j, retval = 0;

	if( imei[0] && stForwarders.count ) {

		// search imei of the terminal in forwarded terminals list
		for(i = 0; i < stForwarders.listcount; i++) {
			if( !strcmp(imei, stForwarders.terminals[i].imei) ) {	// terminal exists in list

				// search needded forwarder by name & get his protocol
				for(j = 0; j < stForwarders.count; j++) {
					if( !strcmp(stForwarders.terminals[i].forward, stForwarders.forwarder[j].name) ) {
						// forwarder exists

						// try to create forward socket
						if( set_forward_socket(config, stForwarders.terminals[i].forward, &forward_attr[retval].forward_socket) ) {
							// encode need?
							forward_attr[retval].forward_encode = (0 != strcmp(stForwarders.forwarder[j].app, config->listener->name));
							// forwarder index in forwarders list
							forward_attr[retval].forward_index = j;

							++retval;
						}

						break;	// stop search forwarders
					}
				}	// for(j = 0;

			}	// if( !strcmp(imei,

			if( retval >= MAX_FORWARDS )
				break;	// maximum forwarder reached
		}	// for(i = 0;

	}	// if( imei && imei[0] && stForwarders.count )

	return retval;
}
//------------------------------------------------------------------------------

/*
    forward data to another server, encode in new terminal protocol
    data - pointer to set of terminal records (ST_RECORD *) or raw terminal data (char *)
    data_size - number of records (in ST_RECORD *) or size of char * in bytes
    encode - encode flag (0 if raw data, not 0 if records)
*/
static void send_data_to_forward(ST_WORKER *config, void *data, int data_size, ST_FORWARD_ATTR *fa)
{
	char forward_buf[SOCKET_BUF_SIZE];	// buffer for forward messages
	ST_FORWARD_MSG *msg = (ST_FORWARD_MSG *)forward_buf;
	size_t full_size;

	if( data && data_size ) {

		if( fa->forward_encode ) {	// data - array of ST_RECORD & data_size - number records in array
			full_size = sizeof(ST_FORWARD_MSG) + sizeof(ST_RECORD) * data_size;

			if( full_size > SOCKET_BUF_SIZE ) {
				// truncate records
				data_size = ceil((SOCKET_BUF_SIZE - sizeof(ST_FORWARD_MSG)) / sizeof(ST_RECORD));
				full_size = sizeof(ST_FORWARD_MSG) + sizeof(ST_RECORD) * data_size;
			}
		}
		else {	// data - char* & data_size - length of the data
			full_size = sizeof(ST_FORWARD_MSG) + data_size;
		}

		memcpy(msg->imei, config->imei, SIZE_TRACKER_FIELD);
		msg->encode = fa->forward_encode;
		msg->len = data_size;

		if( full_size <= SOCKET_BUF_SIZE ) {
			memcpy(&forward_buf[sizeof(ST_FORWARD_MSG)], data, full_size - sizeof(ST_FORWARD_MSG));

			if( send(fa->forward_socket, forward_buf, full_size, 0) <= 0 ) {	// socket error
				logging("%s[%ld]: send(forward_socket) error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));
				set_forward_socket(config, NULL, &fa->forward_socket);	// close socket
			}	// if( send(
			else {
				if( stConfigServer.log_enable > 1 ){
					if( msg->encode )
						logging("%s[%ld]: %s: send to forward %d records, encode=%d\n", config->listener->name, syscall(SYS_gettid), msg->imei, msg->len, msg->encode);
					else
						logging("%s[%ld]: %s: send to forward %d bytes, encode=%d\n", config->listener->name, syscall(SYS_gettid), msg->imei, msg->len, msg->encode);
				}
			}	// else if( send(
		}
		else {
			if( stConfigServer.log_enable )
				logging("%s[%ld]: send_data_to_forward: %s full_size(%ld) > SOCKET_BUF_SIZE\n", config->listener->name, syscall(SYS_gettid), msg->imei, full_size);
		}	// else if( full_size <= SOCKET_BUF_SIZE )

	}	// if( data && data_size )
	else {
		if( stConfigServer.log_enable > 1 ){
			if( data_size )
				logging("%s[%ld]: send_data_to_forward: %s data is NULL\n", config->listener->name, syscall(SYS_gettid), config->imei);
			else
				logging("%s[%ld]: send_data_to_forward: %s data_size <= 0\n", config->listener->name, syscall(SYS_gettid), config->imei);
		}	// if( stConfigServer.log_enable > 1 )
	}	// else if( data && data_size )
}
//------------------------------------------------------------------------------

/*
    write terminal data to DB function
    records - set of terminal records
    count - number of records
*/
static void send_data_to_db(ST_WORKER *config, ST_RECORD *records, unsigned int count)
{
	unsigned int r;

	if( !records || count <= 0 || config->db_queue == BAD_OBJ )
		return;

	for(r = 0; r < count; r++) {	// for all decoded records

		if( records[r].imei[0] ) {	// if IMEI decoded

			// send message into database queue
			if( mq_send(config->db_queue, (const char *)&records[r], sizeof(ST_RECORD), 0) < 0 ) {
				switch(errno) {
				case EAGAIN:
					logging("%s[%ld]: mq_send(config->db_queue) message queue is already full\n", config->listener->name, syscall(SYS_gettid));
					break;
				default:
					logging("%s[%ld]: mq_send(config->db_queue) error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));
				}	// switch(errno)
			}	// if( mq_send(

		}	// if( strlen(records[r].imei) )

	}	// for(r = 0; r < count; r++)
}
//------------------------------------------------------------------------------


/*
    main thread function
    st_worker - pointer to ST_WORKER structure (worker.h)
*/
void *worker_thread(void *st_worker)
{
	static __thread ST_WORKER *config;	// configuration of the worker
	static __thread unsigned int i;
	static __thread unsigned int errors = 0;	// worker error counter
	static __thread unsigned int forward_tested = 0;	// flag: 0 - test for forwarding not fired, 1 - fired
	static __thread unsigned int forward_count = 0;	// flag & count of forwarders's sockets
	static __thread ST_FORWARD_ATTR forward_attr[MAX_FORWARDS];
	static __thread char socket_buf[SOCKET_BUF_SIZE];		// client socket buffer
	static __thread ssize_t bytes_read = 0, bytes_write = 0;	// for socket read/write operations
	static __thread ST_ANSWER answer;	// de.h
	static __thread fd_set rfds;
	static __thread struct timeval tv;
	static __thread char l2fname[FILENAME_MAX];		// terminal log file name
	//static __thread char *client_ip = inet_ntoa(config->client_addr.sin_addr);

	// error handler:
	void exit_worker(void * arg) {
		ST_WORKER *config = (ST_WORKER *)arg;	// configuration of the worker

		// free recources
		if( config ) {
			// close terminal socket
			if( config->client_socket != BAD_OBJ ) {
				shutdown(config->client_socket, SHUT_RDWR); // gracefully
				close(config->client_socket);
			}

			// close forwarding sockets
			if( forward_count ) {
				for( i = 0; i < forward_count; i++) {
					set_forward_socket(config, NULL, &forward_attr[i].forward_socket);
				}
			}

			// close database queue
			if( config->db_queue != BAD_OBJ ) {
				mq_close(config->db_queue);
			}

			// log, if required
			if( stConfigServer.log_enable > 1 ) {
				if( config->imei[0] )   // imei exists
					logging("%s[%ld]: %s shutdown\n", config->listener->name, syscall(SYS_gettid), config->imei);
				else
					logging("%s[%ld]: shutdown\n", config->listener->name, syscall(SYS_gettid));
			}	// if( stConfigServer.log_enable )

			free(config);
		}	// if( config )
		else {
			// only for debug testing time:
			logging("%s[%ld]: imei %s: exit_worker(NULL)\n", config->listener->name, syscall(SYS_gettid), config->imei);
		}
	}
	//------------------------------------------------------------------------------

	// install error handler:
	pthread_cleanup_push(exit_worker, st_worker);

	/*
	    initialization
	*/
	// configuration of the worker
	config = (ST_WORKER *)st_worker;
	if( !config ) {
		logging("worker[%ld]: configuration not passed, exit\n", syscall(SYS_gettid));
		exit_worker(config);
		return NULL;
	}

	// test exists decode functions
	if( !config->listener ) {
		logging("worker[%ld]: listener configuration not defined, exit\n", syscall(SYS_gettid));
		exit_worker(config);
		return NULL;
	}
	if( !config->listener->terminal_decode || !config->listener->terminal_encode ) {
		logging("%s[%ld]: %s.so not loaded, exit\n", config->listener->name, syscall(SYS_gettid), config->listener->name);
		exit_worker(config);
		return NULL;
	}

	// set non-blocking mode
	if( fcntl(config->client_socket, F_SETFL, O_NONBLOCK) < 0 ) {
		logging("%s[%ld]: fcntl(client_socket) error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));
		exit_worker(config);
		return NULL;
	}

	// prepare queue of messages (connect to existing queue)
	config->db_queue = mq_open(QUEUE_WORKER, O_WRONLY | O_NONBLOCK);
	if( config->db_queue < 0 ) {
		logging("%s[%ld]: mq_open(%s) error %d: %s\n", config->listener->name, syscall(SYS_gettid), QUEUE_WORKER, errno, strerror(errno));
		exit_worker(config);
		return NULL;
	}

	// first clear all
	memset(&answer, 0, sizeof(ST_ANSWER));
	// reset forwarding attributes
	memset(forward_attr, 0, sizeof(ST_FORWARD_ATTR) * MAX_FORWARDS);

	/*
	    main cycle - terminal dialog
	*/
	while( 1 ) {	// until socket live

		pthread_testcancel();

		// second - save lastpoint
		memset(&answer, 0, sizeof(ST_ANSWER) - sizeof(ST_RECORD));

		// wait terminal message
		FD_ZERO(&rfds);
		FD_SET(config->client_socket, &rfds);
		tv.tv_sec = stConfigServer.socket_timeout;
		tv.tv_usec = 0;

		switch( select(config->client_socket + 1, &rfds, NULL, NULL, &tv) ) {
		case BAD_OBJ:	// error
			if( stConfigServer.log_enable )
				logging("%s[%ld]: select(client_socket) error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));

			exit_worker(config);
			return NULL;
		case 0:	// timeout
			if( stConfigServer.log_enable > 1 )
				logging("%s[%ld]: %s timeout\n", config->listener->name, syscall(SYS_gettid), config->imei);

			exit_worker(config);
			return NULL;
		}	// switch( select

		// read terminal message
		memset(socket_buf, 0, SOCKET_BUF_SIZE);
		if(config->listener->protocol == SOCK_STREAM) {
			bytes_read = 0;//recv(config->client_socket, socket_buf, SOCKET_BUF_SIZE, 0);

            while( bytes_read < SOCKET_BUF_SIZE && (bytes_write = recv(config->client_socket, &socket_buf[bytes_read], SOCKET_BUF_SIZE-bytes_read, 0)) > 0 ){
                bytes_read += bytes_write;
                usleep(10000);
            }
        }
		else
			bytes_read = recvfrom(config->client_socket, socket_buf, SOCKET_BUF_SIZE, 0, NULL, NULL);

		if( !FD_ISSET(config->client_socket, &rfds) || bytes_read <= 0 ) {	// socket read error or terminal disconnect
			exit_worker(config);
			return NULL;
		}

		// logging all
		if( config->listener->log_all ) {
			if( config->imei[0] )
				snprintf(l2fname, FILENAME_MAX, "%s/logs/%s_%s", stParams.start_path, config->listener->name, config->imei);
			else
				snprintf(l2fname, FILENAME_MAX, "%s/logs/%s", stParams.start_path, config->listener->name);

			log2file(l2fname, socket_buf, bytes_read);
		}	// if( config->listener->log_all )
		else if( stConfigServer.log_imei[0] && stConfigServer.log_imei[0] == config->imei[0] ){
			// log terminal message
			if( !strcmp(stConfigServer.log_imei, config->imei) ){
				snprintf(l2fname, FILENAME_MAX, "%s/logs/%s_prcl", stParams.start_path, config->imei);
				log2file(l2fname, socket_buf, bytes_read);
			}
		}

		// decode terminal message
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	// do not disturb :)
		config->listener->terminal_decode(socket_buf, bytes_read, &answer);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // can disturb :)

		// answer to terminal
		if( answer.size ) {
			errors = 0;

			if(config->listener->protocol == SOCK_STREAM)
				bytes_write = send(config->client_socket, answer.answer, answer.size, 0);
			else
				bytes_write = sendto(config->client_socket, answer.answer, answer.size, 0, (struct sockaddr *)&config->client_addr, sizeof(struct sockaddr_in));

			if( bytes_write <= 0 && stConfigServer.log_enable > 1 )	// socket write error
				logging("%s[%ld]: send to terminal error %d: %s\n", config->listener->name, syscall(SYS_gettid), errno, strerror(errno));
            else {
    			// log answer to terminal
    			if( stConfigServer.log_imei[0] && stConfigServer.log_imei[0] == config->imei[0] ){
    				if( !strcmp(stConfigServer.log_imei, config->imei) ){
    					snprintf(l2fname, FILENAME_MAX, "%s/logs/%s_answ", stParams.start_path, config->imei);
    					log2file(l2fname, answer.answer, answer.size);
    				}
    			}   // if( stConfigServer.log_imei[0]
            }   // else if( bytes_write <= 0
		}	// if( answer.size )
        else
            bytes_write = 0;

		// save terminal data to DB
		if( answer.count ) {
			errors = 0;	// reset errors counter

			if( !config->imei[0] && answer.records[0].imei[0] ) {	// first set config->imei
				strcpy(config->imei, answer.records[0].imei);

				if( stConfigServer.log_enable > 1 )
					logging("%s[%ld]: %s connected\n", config->listener->name, syscall(SYS_gettid), config->imei);
			}

			send_data_to_db(config, answer.records, answer.count);

		}	// if( answer.count )
		else {	// no decoded records

			// no answer, no records - what is the motherfucker?
			if( !answer.size ) {

				if( ++errors > MAX_ERRORS ) {	// too many errors

					if( stConfigServer.log_enable > 1 )
						logging("%s[%ld]: %s no answer, no records, drop", config->listener->name, syscall(SYS_gettid), config->imei);

					// log terminal data to file
					if( config->listener->log_err ) {
						snprintf(answer.answer, SOCKET_BUF_SIZE, "%s/logs/%s", stParams.start_path, config->listener->name);
						log2file(answer.answer, socket_buf, bytes_read);
						memset(answer.answer, 0, SOCKET_BUF_SIZE);
					}	// if( config->listener->log_err )

					exit_worker(config);	// fuck you
					return NULL;

				}	// if( ++errors > MAX_ERRORS )

			}	// if( !answer.size )
			else {
				// identification without records - imei must be set in answer->lastpoint

				if( !config->imei[0] && answer.lastpoint.imei[0] ) {	// imei exists
					strcpy(config->imei, answer.lastpoint.imei);

					if( stConfigServer.log_enable > 1 )
						logging("%s[%ld]: %s connected\n", config->listener->name, syscall(SYS_gettid), config->imei);
				}	// if( answer.lastpoint.imei[0] )

			}	// else if( !answer.size )

		}	// else if( answer.count )

		// socket write error - terminal disconnected or network error
		if( answer.size && bytes_write <= 0 ) {
			exit_worker(config);
			return NULL;
		}

		// test for retranslation
		if( !forward_tested && config->imei[0] ) {	// before not tested & imey exists
			++forward_tested;	// set flag to test fired

			// is forwarding need ?
			forward_count = test_forward(config, config->imei, forward_attr);
		}	// if( !forward_tested && config->imei[0] )

		// forwarding
		if( forward_count ) {
			for( i = 0; i < forward_count; i++) {

				if( forward_attr[i].forward_socket != BAD_OBJ ) {

					if( forward_attr[i].forward_encode ) {	// terminal & forward protocols not equal
						send_data_to_forward(config, answer.records, answer.count, &forward_attr[i]);	// forward decoded records
					}
					else { // terminal & forward protocols is equal
						send_data_to_forward(config, socket_buf, bytes_read, &forward_attr[i]);	// forward raw data
					}
				}	// if( forward_attr[i].forward_socket != BAD_OBJ )

			}	// for( i = 0; i < forward_count; i++)
		}	// forward_count

	}	// while( 1 )

	/*
		shutdown
	*/
	// clear error handler with run it (0 - not run, 1 - run)
	pthread_cleanup_pop(1);

	return NULL;
}
//------------------------------------------------------------------------------
