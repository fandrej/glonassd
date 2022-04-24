/*
    forwarder.c
    forward packets from this server to another, repack if needded

    help:
    http://rsdn.ru/article/unix/sockets.xml
    http://man7.org/linux/man-pages/man7/unix.7.html
    http://man7.org/linux/man-pages/man2/sendmsg.2.html
    http://www.faqs.org/faqs/unix-faq/socket/
    http://www.opennet.ru/cgi-bin/opennet/man.cgi?topic=select&category=2
    http://www.opennet.ru/man.shtml?topic=poll&category=2
    http://man7.org/linux/man-pages/man2/poll.2.html
    http://www.redov.ru/kompyutery_i_internet/osnovy_programmirovanija_v_linux/p19.php#metkadoc18
    http://www.cyberforum.ru/c-linux/thread241842.html
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>	/* syscall */
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <unistd.h> /* close, fork */
#include <errno.h>  /* errno */
#include <pthread.h> /* syslog */
#include <time.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "glonassd.h"
#include "forwarder.h"
#include "lib.h"
#include "de.h"
#include "logger.h"

/*
    treads shared globals
*/
#define CONNECT_SOCKET_TIMEOUT (5)	// socket timeout in seconds for connect()

/*
    thread locals
*/
static __thread unsigned long long int disconnect_time = 0;	// time in seconds when out socket disconnected
static __thread int out_connected = 0;						// out connection established flag
static __thread int log_server_answer = 0;					// flag for log remote server to file
static __thread int files_saved = 0;					    // count of saved (not passed) parcels

/*
    utility functions
*/

/*
reset logged flag to "no" for all terminals
called when socket to remote server disconnect
*/
static void terimal_reset_logged(char *forward_name)
{
	unsigned int i;

	for(i = 0; i < stForwarders.listcount; ++i) {
		if( forward_name[0] == stForwarders.terminals[i].forward[0] )
		{
			if( !strcmp(forward_name, stForwarders.terminals[i].forward) )
			{
				stForwarders.terminals[i].logged = 0;
			}
		}
	}	// for(i = 0; i < stForwarders.listcount; i++)
}
//------------------------------------------------------------------------------

/*
test: logged terminal on remote server or not
always set logged flag to "yes"
*/
static int terimal_logged(char *imei, char *forward_name)
{
	unsigned int i, retval = 1;

	if( imei ){
		for(i = 0; i < stForwarders.listcount; ++i) {
			if( forward_name[0] == stForwarders.terminals[i].forward[0] && imei[0] == stForwarders.terminals[i].imei[0] )
			{
				if( !strcmp(forward_name, stForwarders.terminals[i].forward) && !strcmp(imei, stForwarders.terminals[i].imei) )
				{
					retval = stForwarders.terminals[i].logged;
					stForwarders.terminals[i].logged = 1;
					break;
				}
			}
		}	// for(i = 0; i < stForwarders.listcount; i++)
	}	// if( imei )

	return retval;
}
//------------------------------------------------------------------------------

/*
    save forwarding data to file (!!! data encoded to required protocol !!!)
    config - config of the forwarder
    imei - IMEI saved terminal
    content_size - size of data
*/
static int data_save(ST_FORWARDER *config, char *imei, ssize_t content_size)
{
	int fHandle;
	time_t t;
	char fName[FILENAME_MAX];
	ST_FORWARD_MSG msg;
    int cnt_files = 0;

	if( config->buffers[OUT_WRBUF] && content_size ) {
		time(&t);
		snprintf(fName, FILENAME_MAX, "%s/%s_%llu.bin",
				 stConfigServer.forward_files,
				 config->name,
				 (unsigned long long)t);

		if( (fHandle = open(fName, O_CREAT | O_WRONLY | O_NOATIME | O_TRUNC, S_IRWXU | S_IRGRP | S_IROTH)) != -1 ) {
			// create header
			memset(&msg, 0, sizeof(ST_FORWARD_MSG));	// msg.encode = 0
			if(imei && imei[0]) {
				memcpy(msg.imei, imei, SIZE_TRACKER_FIELD);
			}
			msg.len = content_size;
			// write header
			if( !write(fHandle, &msg, sizeof(ST_FORWARD_MSG)) ) {}
			// write data
			if( !write(fHandle, config->buffers[OUT_WRBUF], content_size) ){}
            else {
                ++cnt_files;
            }

			close(fHandle);

			if( config->debug ) {
				logging("forwarder[%s][%ld]: data_save: written %ld bytes to file\n", config->name, syscall(SYS_gettid), content_size);
            }
		}
		else if( stConfigServer.log_enable && config->debug ) {
			logging("forwarder[%s][%ld]: data_save: open(%s) error %d: %s\n", config->name, syscall(SYS_gettid), fName, errno, strerror(errno));
        }

	}	// if( config->buffers[OUT_WRBUF] && content_size )
	else if( config->debug ) {
		logging("forwarder[%s][%ld]: data_save: config->buffers[OUT_WRBUF] is NULL or content_size=%ld\n", config->name, syscall(SYS_gettid), content_size);
    }

    return cnt_files;
}
//---------------------------------------------------------------------------

// set up out connected socket
static int set_out_socket(ST_FORWARDER *config, int create)
{
	struct sockaddr_in addr_in;
	struct timeval tv = {0};

	disconnect_time = seconds();

	if( create ) {	// create socket
		if( config->debug ) {
			logging("forwarder[%s][%ld]: start connect to remote host %s:%d\n", config->name, syscall(SYS_gettid), config->server, config->port);
        }

		if( config->sockets[OUT_SOCKET] == BAD_OBJ ) {
			config->sockets[OUT_SOCKET] = socket(AF_INET, config->protocol, 0);
			if( config->sockets[OUT_SOCKET] < 0 ) {
				logging("forwarder[%s][%ld]: socket() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				return 0;
			}

			// set non-blocking mode
			if( fcntl(config->sockets[OUT_SOCKET], F_SETFL, O_NONBLOCK) < 0 ) {
				logging("forwarder[%s][%ld]: fcntl(OUT_SOCKET, O_NONBLOCK) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				close(config->sockets[OUT_SOCKET]);
				config->sockets[OUT_SOCKET] = BAD_OBJ;
				return 0;
			}

			// set reuse address for reconnect
			if (setsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
				logging("forwarder[%s][%ld]: setsockopt(SO_REUSEADDR) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
			}

#ifdef SO_REUSEPORT
			if (setsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0) {
				logging("forwarder[%s][%ld]: setsockopt(SO_REUSEPORT) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
			}
#endif

			// set connect timeout
			// http://stackoverflow.com/questions/15243988/connect-returns-operation-now-in-progress-on-blocking-socket
			tv.tv_sec = CONNECT_SOCKET_TIMEOUT;
			if( (setsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0)
					|| (setsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval)))
			  ) {
				logging("forwarder[%s][%ld]: setsockopt(SO_RCVTIMEO) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				/*
				    close(config->sockets[OUT_SOCKET]);
				    config->sockets[OUT_SOCKET] = -1;
				    return 0;
				*/
			}	// if( (setsockopt(config->sockets[OUT_SOCKET]


			// bind socket to internal address
			memset(&addr_in, 0, sizeof(struct sockaddr_in));
			addr_in.sin_family = AF_INET;
			addr_in.sin_port = 0;
			inet_aton(stConfigServer.transmit, &addr_in.sin_addr);
			if( bind(config->sockets[OUT_SOCKET], (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) < 0 ) {
				logging("forwarder[%s][%ld]: bind(%s) error %d: %s\n", config->name, syscall(SYS_gettid), stConfigServer.transmit, errno, strerror(errno));
				close(config->sockets[OUT_SOCKET]);
				config->sockets[OUT_SOCKET] = BAD_OBJ;
				return 0;
			}
		}	// if( config->sockets[OUT_SOCKET] == BAD_OBJ )

		// connect socket to external address
		memset(&addr_in, 0, sizeof(struct sockaddr_in));
		addr_in.sin_family = AF_INET;
		addr_in.sin_port = htons(config->port);
		inet_aton(config->server, &addr_in.sin_addr);
		if( connect(config->sockets[OUT_SOCKET], (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) < 0 ) {
			if( errno != EINPROGRESS ) {	// non-blocking socket, connection not in progress (error)
				logging("forwarder[%s][%ld]: connect(%s:%d) error %d: %s\n", config->name, syscall(SYS_gettid), config->server, config->port, errno, strerror(errno));
				close(config->sockets[OUT_SOCKET]);
				config->sockets[OUT_SOCKET] = BAD_OBJ;
			}
			// else connection in progress, see result of poll()
		}	// if( connect(

	}	// if( create )
	else {	// destroy socket
		out_connected = 0;	// reset connetion established flag
		shutdown(config->sockets[OUT_SOCKET], SHUT_RDWR);
		close(config->sockets[OUT_SOCKET]);
		config->sockets[OUT_SOCKET] = BAD_OBJ;

		terimal_reset_logged(config->name);
	}

	return( create ? (config->sockets[OUT_SOCKET] != BAD_OBJ) : 1);
}
//------------------------------------------------------------------------------

/*
    process terminal data
    bufer - ST_FORWARD_MSG*
    size - length of the bufer
*/
static void process_terminal(ST_FORWARDER *config, char *bufer, ssize_t size)
{
	ST_FORWARD_MSG *msg;
	ssize_t data_len = 0, sended = 0;
	char l2fname[FILENAME_MAX];		// terminal log file name

	if( !bufer ){
		if( config->debug ) {
			logging("forwarder[%s][%ld]: process_terminal %s: bufer is NULL\n", config->name, syscall(SYS_gettid), msg->imei);
        }
		return;
	}

	if( !size ){
		if( config->debug ) {
			logging("forwarder[%s][%ld]: process_terminal %s: size = 0\n", config->name, syscall(SYS_gettid), msg->imei);
        }
		return;
	}

	msg = (ST_FORWARD_MSG *)bufer;
	if( !msg->len ){
		if( config->debug ) {
			logging("forwarder[%s][%ld]: process_terminal %s: msg->len = 0\n", config->name, syscall(SYS_gettid), msg->imei);
        }
		return;
	}

	if( msg->encode ) {	// encode need, data = ST_RECORD*, msg->len = number of the records in data
		/* check: terminal authentificated or no on remote server,
		if no (ST_FORWARD_TERMINAL[imei][config->name].logged == 0) then set msg->len = -1 * msg->len
		else msg->len not change
		*/
		if( !terimal_logged(msg->imei, config->name) )
			msg->len *= -1;

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	// do not disturb :)
		data_len = config->terminal_encode((ST_RECORD*)&bufer[sizeof(ST_FORWARD_MSG)], msg->len, config->buffers[OUT_WRBUF], SOCKET_BUF_SIZE);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // can disturb :)
	}
	else {	// data = raw terminal data, msg->len = data size
		// copy data part to out buffer
		memcpy(config->buffers[OUT_WRBUF], &bufer[sizeof(ST_FORWARD_MSG)], msg->len);
		data_len = msg->len;
	}	// else if(msg->encode)

	if( data_len ) {
		if( out_connected ) {
			sended = send(config->sockets[OUT_SOCKET], config->buffers[OUT_WRBUF], data_len, 0);
			if( sended <= 0 ) {	// socket error or disconnect
				logging("forwarder[%s][%ld]: process_terminal: send() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				set_out_socket(config, 0);	// disconnect outer socket
			}	// if( sended <= 0 )
			else {

				// log terminal message to remote server
				if( stConfigServer.log_imei[0] && stConfigServer.log_imei[0] == msg->imei[0] ){
					if( !strcmp(stConfigServer.log_imei, msg->imei) ){
						snprintf(l2fname, FILENAME_MAX, "%s/logs/%s_%s_parcel", stParams.start_path, msg->imei, config->name);
						log2file(l2fname, config->buffers[OUT_WRBUF], data_len);
						// flag for log remote server answer to file,
						// if forwarder protocol EGTS, then flag = EGTS_RECORD_HEADER.RN (record number)
						// else 1 simply
						if( strstr(config->name, "egts") )
							log_server_answer = *(uint16_t*)&config->buffers[OUT_WRBUF][13];	// EGTS_RECORD_HEADER.RN
						else
							log_server_answer = 1;
					}	// if( !strcmp(stConfigServer.log_imei, msg->imei) )
				}	// if( stConfigServer.log_imei[0]

				if( config->debug ) {
					logging("forwarder[%s][%ld]: process_terminal %s: sended %ld bytes to remote server\n", config->name, syscall(SYS_gettid), msg->imei, sended);
                }
			}	// else if( sended <= 0 )
		}	// if( out_connected )

		if( sended <= 0 )	// save buffer to file for send later
			files_saved += data_save(config, msg->imei, data_len);

	}	// if( data_len )
	else if( config->debug ) {
		logging("forwarder[%s][%ld]: process_terminal %s: data_len=%ld\n", config->name, syscall(SYS_gettid), msg->imei, data_len);
    }
}
//------------------------------------------------------------------------------

// set up inner listener socket
static int set_listen_socket(ST_FORWARDER *config)
{
	// create socket
	config->sockets[IN_SOCKET] = socket(AF_UNIX, SOCK_DGRAM, 0);
	if( config->sockets[IN_SOCKET] < 0 ) {
		logging("forwarder[%s][%ld]: listen socket() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
		return 0;
	}

	// set non-blocking mode
	if( fcntl(config->sockets[IN_SOCKET], F_SETFL, O_NONBLOCK) < 0 ) {
		logging("forwarder[%s][%ld]: fcntl(IN_SOCKET, O_NONBLOCK) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
		close(config->sockets[IN_SOCKET]);
		config->sockets[IN_SOCKET] = -1;
		return 0;
	}

	// bind socket
	memset(&config->addr_un, 0, sizeof(struct sockaddr_un));
	config->addr_un.sun_family = AF_UNIX;
	snprintf(config->addr_un.sun_path, 108, "/%s", config->name);	// http://man7.org/linux/man-pages/man7/unix.7.html
	unlink(config->addr_un.sun_path);	// if exists because crash
	if( bind(config->sockets[IN_SOCKET], (struct sockaddr *)&config->addr_un, sizeof(struct sockaddr_un)) < 0 ) {
		logging("forwarder[%s][%ld]: bind(%s) error %d: %s\n", config->name, syscall(SYS_gettid), config->addr_un.sun_path, errno, strerror(errno));
		close(config->sockets[IN_SOCKET]);
		config->sockets[IN_SOCKET] = -1;
	}

	return(config->sockets[IN_SOCKET] != -1);
}
//------------------------------------------------------------------------------


// wait sockets activity
static int wait_sockets(ST_FORWARDER *config)
{
	unsigned int cnt;
	struct timeval tv;

	// test out socket and reconnect if disconnected
	if( config->sockets[OUT_SOCKET] == BAD_OBJ && (seconds() - disconnect_time >= stConfigServer.forward_wait)) {
		set_out_socket(config, 1);
	}

	FD_ZERO(&config->fdset[0]);	// read
	FD_ZERO(&config->fdset[1]);	// write
	FD_SET(config->sockets[IN_SOCKET], &config->fdset[0]);

	if( config->sockets[OUT_SOCKET] != BAD_OBJ ) {
		if( out_connected )
			FD_SET(config->sockets[OUT_SOCKET], &config->fdset[0]);	// read
		else
			FD_SET(config->sockets[OUT_SOCKET], &config->fdset[1]);	// write
	}

	tv.tv_sec = stConfigServer.forward_timeout;
	tv.tv_usec = 0;

	cnt = config->sockets[OUT_SOCKET] > config->sockets[IN_SOCKET] ? config->sockets[OUT_SOCKET] : config->sockets[IN_SOCKET];

	return select(cnt + 1, &config->fdset[0], &config->fdset[1], NULL, &tv);
}
//------------------------------------------------------------------------------


/*
    main thread function
*/
void *forwarder_thread(void *st_forwarder)
{
	static __thread ST_FORWARDER *config;				// configuration
	static __thread ST_ANSWER answer;
	static __thread int i, fHandle, so_error;
	static __thread socklen_t so_error_len = sizeof(int);
	static __thread ssize_t tmp, bytes_read = 0;
	static __thread char fName[FILENAME_MAX];
	static __thread struct dirent *result;

	// eror handler:
	void exit_forwarder_thread(void * arg) {
		unsigned int i;

		// clear sockets
		for(i = 0; i < CNT_SOCKETS; i++ ) {
			if( config->sockets[i] != BAD_OBJ ) {
				shutdown(config->sockets[i], SHUT_RDWR);
				close(config->sockets[i]);
				config->sockets[i] = BAD_OBJ;
			}
		}

		/*  When no longer required, the socket pathname,
		    should be deleted using unlink(2) or remove(3)
		*/
		if( strlen(config->addr_un.sun_path) )
			unlink(config->addr_un.sun_path);

		if( config->data_dir )
			closedir(config->data_dir);

		terimal_reset_logged(config->name);

		logging("forwarder[%s][%ld] destroyed\n", config->name, syscall(SYS_gettid));
	}	// exit_forwarder_thread

	// install error handler:
	pthread_cleanup_push(exit_forwarder_thread, NULL);

	/*
	    initialization
	*/
	config = (ST_FORWARDER *)st_forwarder;
	if( !config ) {
		logging("forwarder[%ld]: configuration not passed, exit\n", syscall(SYS_gettid));
		exit_forwarder_thread(NULL);
		return NULL;
	}

	// set inner listener socket
	config->sockets[IN_SOCKET] = BAD_OBJ;
	if( !set_listen_socket(config) ) {
		exit_forwarder_thread(NULL);
		return NULL;
	}

	// set outer server socket
	config->sockets[OUT_SOCKET] = BAD_OBJ;
	set_out_socket(config, 1);	// if not connected, will retry

	memset(&answer, 0, sizeof(ST_ANSWER));

	logging("forwarder[%s][%ld] started\n", config->name, syscall(SYS_gettid));

	/*
	    main cycle
	*/
	while( 1 ) {

		pthread_testcancel();

		switch( wait_sockets(config) ) {
		case -1:	// select() error

			logging("forwarder[%s][%ld]: select() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
			exit_forwarder_thread(NULL);
			return NULL;

		case 0:	// timeout

			/* read saved parcels and send to destination */
			if( files_saved && out_connected && config->data_dir ) {
				rewinddir(config->data_dir);	// resets the position of the directory stream to the beginning of the directory
				i = 0;	// number of files to read

				// iterate files in directory
				while( (result = readdir(config->data_dir)) != NULL ) { // CPU usage increase
					// is file name OK & contain this forwarder name & contain ".bin"?
					if( strlen(result->d_name)
							&& strstr(result->d_name, config->name)
							&& strstr(result->d_name, ".bin") ) {

						// generate full file name
						snprintf(fName, FILENAME_MAX, "%s/%s", stConfigServer.forward_files, result->d_name);

                        /*
                        if( config->debug ){
            				logging("forwarder[%s][%ld]: read file %s\n", config->name, syscall(SYS_gettid), result->d_name);
                        }
                        */

						// open file for read
						if( (fHandle = open(fName, O_RDONLY | O_NOATIME)) != -1 ) {

							bytes_read = read(fHandle, config->buffers[IN_RDBUF], SOCKET_BUF_SIZE);
							if( bytes_read > 0 ) {
								process_terminal(config, config->buffers[IN_RDBUF], bytes_read);
                            }
                            /*
    						else if( config->debug ) {
    							logging("forwarder[%s][%ld]: file is empty\n", config->name, syscall(SYS_gettid));
                            }
                            */

							close(fHandle);

							if( config->debug ){
								logging("forwarder[%s][%ld]: send saved file %s\n", config->name, syscall(SYS_gettid), result->d_name);
                            }
						}	// if( (fHandle = open(fName
						else if( config->debug ) {
							logging("forwarder[%s][%ld]: read file %s error: %d: %s\n", config->name, syscall(SYS_gettid), result->d_name, errno, strerror(errno));
                        }

						// delete file
						unlink(fName);

						if( ++i >= CNT_FILES_SEND )
							break;	// max files reached, cancel
					}	// if( strlen(result->d_name) &&
				}	// while( (result = readdir(config->data_dir)) != NULL )

                if( i < files_saved )
                    files_saved -= i;
                else
                    files_saved = 0;

                if( config->debug ){
                    logging("forwarder[%s][%ld]: %d files processed\n", config->name, syscall(SYS_gettid), i);
                }

			}	// if( out_connected && config->data_dir )
			else if( !config->data_dir && config->debug ){
				logging("forwarder[%s][%ld]: directory '%s' is bad\n", config->name, syscall(SYS_gettid), stConfigServer.forward_files);
			}	// else if( data_dir )
            else if( !out_connected && config->debug ){
				logging("forwarder[%s][%ld]: not connected to %s:%d\n", config->name, syscall(SYS_gettid), config->server, config->port);
            }

			break;
		default:	// number of ready file descriptors

			// OUT_SOCKET

			if( FD_ISSET(config->sockets[OUT_SOCKET], &config->fdset[1]) ) {
				// connection to remote server complete

				if( !getsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) )
					out_connected = !so_error;

				if( out_connected ) {
					logging("forwarder[%s][%ld]: remote host %s:%d connected\n", config->name, syscall(SYS_gettid), config->server, config->port);
                }
				else {
					switch(so_error){
					case 0:		// Sucess
						if( errno != EINPROGRESS ){
							logging("forwarder[%s][%ld]: remote host %s:%d connection error %d: %s\n", config->name, syscall(SYS_gettid), config->server, config->port, errno, strerror(errno));
							set_out_socket(config, 0);
						}
						break;
					default:
						logging("forwarder[%s][%ld]: remote host %s:%d %s (%d)\n", config->name, syscall(SYS_gettid), config->server, config->port, strerror(so_error), so_error);
						set_out_socket(config, 0);
					}	// switch(so_error)
				}	// else if( out_connected )

			}	// if( FD_ISSET(config->sockets[OUT_SOCKET], &config->fdset[1]) )
			else if( FD_ISSET(config->sockets[OUT_SOCKET], &config->fdset[0]) ) {
				// has incoming messages from remote server

				if( out_connected ){
					// receive data
					memset(config->buffers[OUT_RDBUF], 0, SOCKET_BUF_SIZE);

					bytes_read = 0;
                    while( bytes_read < SOCKET_BUF_SIZE && (tmp = recv(config->sockets[OUT_SOCKET], &config->buffers[OUT_RDBUF][bytes_read], SOCKET_BUF_SIZE-bytes_read, 0)) > 0 ){
                        bytes_read += tmp;
                        usleep(25000);
                    }

                    if( bytes_read > 0 ) { // data

						// decode server answer
						if( config->terminal_decode ) {
							pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	// do not disturb :)
							config->terminal_decode(config->buffers[OUT_RDBUF], bytes_read, &answer, NULL);
							pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // can disturb :)
						}	// if( config->terminal_decode )

						if( answer.size ) {
							// send answer to terminal
						}	// if( answer.size )

						// log remote server answer to file
						if( log_server_answer ){
							// if forwarder protocol EGTS, then flag = EGTS_RECORD_HEADER.RN (record number)
							// else 1 simply
							if( strstr(config->name, "egts") ){
								// and log_server_answer == EGTS_RECORD_HEADER.RN
								if( *(uint16_t*)&config->buffers[OUT_RDBUF][14] == log_server_answer ){	// EGTS_SR_RECORD_RESPONSE.CRN
									log_server_answer = 0;	// and reset log flag

									snprintf(fName, FILENAME_MAX, "%s/logs/%s_answer", stParams.start_path, config->name);
									log2file(fName, config->buffers[OUT_RDBUF], bytes_read);
								}
							}
							else {
								log_server_answer = 0;	// and reset log flag

								snprintf(fName, FILENAME_MAX, "%s/logs/%s_answer", stParams.start_path, config->name);
								log2file(fName, config->buffers[OUT_RDBUF], bytes_read);
							}
						}	// if( log_server_answer )

					}	// if( bytes_read > 0 )
					else {
						logging("forwarder[%s][%ld]: remote host %s:%d %s (%d)\n", config->name, syscall(SYS_gettid), config->server, config->port, strerror(errno), errno);
						set_out_socket(config, 0);
					}
				}	// if( out_connected )

			}	// if( FD_ISSET(config->sockets[OUT_SOCKET], &config->fdset[0]) )


			// IN_SOCKET

			if( FD_ISSET(config->sockets[IN_SOCKET], &config->fdset[0]) ) {
				// messages from workers
				memset(config->buffers[IN_RDBUF], 0, SOCKET_BUF_SIZE);
				bytes_read = recv(config->sockets[IN_SOCKET], config->buffers[IN_RDBUF], SOCKET_BUF_SIZE, 0);
				if( bytes_read > 0 )
					process_terminal(config, config->buffers[IN_RDBUF], bytes_read);
				// else worker terminated

			}	// if( FD_ISSET(config->sockets[IN_SOCKET], &config->fdset[0]) )

		}	// switch( wait_sockets(config) )

	}	// while(1)

	// clear error handler with run it (0 - not run, 1 - run)
	pthread_cleanup_pop(1);

	return NULL;
}	// void *forwarder_thread
//------------------------------------------------------------------------------
