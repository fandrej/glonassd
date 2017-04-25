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
#define CONNECT_SOCKET_TIMEOUT (5)	// socket timeout in seconds for connect
#define POLL_SOCKET_TIMEOUT (3)		// socket timeout in seconds for poll (select)
#define OUT_SOCKET_WAIT_TIME (60)	// wait time for outer socket reconnect

/*
    thread locals
*/
static __thread unsigned long long int disconnect_time = 0;	// time in seconds when out socket disconnected
static __thread int out_connected = 0;					// out connection flag

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

	for(i = 0; i < stForwarders.listcount; i++) {
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
		for(i = 0; i < stForwarders.listcount; i++) {
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
    config_name - name of the forwarder
    imei - IMEI saved terminal
    content - encoded data
    content_size - size of data
*/
static void data_save(char *config_name, char *imei, char *content, ssize_t content_size)
{
	int fHandle;
	time_t t;
	char fName[FILENAME_MAX];
	ST_FORWARD_MSG msg;

	if( content && content_size ) {
		time(&t);
		snprintf(fName, FILENAME_MAX, "%s/%s_%llu.bin",
				 stConfigServer.forward_files,
				 config_name,
				 (unsigned long long)t);

		if( (fHandle = open(fName, O_CREAT | O_WRONLY | O_NOATIME | O_TRUNC, S_IRWXU | S_IRGRP | S_IROTH)) != -1 ) {
			// create header
			memset(&msg, 0, sizeof(ST_FORWARD_MSG));	// msg.encode = 0
			if(imei && imei[0]) {
				memcpy(msg.imei, imei, SIZE_TRACKER_FIELD);
			}
			msg.len = content_size;
			// write header
			write(fHandle, &msg, sizeof(ST_FORWARD_MSG));
			// write data
			write(fHandle, content, content_size);

			close(fHandle);

			if( stConfigServer.log_enable > 1 )
				logging("forwarder[%s][%ld]: data_save: written %ld bytes to file\n", config_name, syscall(SYS_gettid), content_size);
		}
		else {
			logging("forwarder[%s][%ld]: data_save: open(%s) error %d: %s\n", config_name, syscall(SYS_gettid), fName, errno, strerror(errno));
		}
	}	// if( content && content_size )
	else if( stConfigServer.log_enable > 1 )
		logging("forwarder[%s][%ld]: data_save: content is NULL or content_size=%ld\n", config_name, syscall(SYS_gettid), content_size);
}
//---------------------------------------------------------------------------

// set up out connected socket
static int set_out_socket(ST_FORWARDER *config, int create)
{
	struct sockaddr_in addr_in;
	struct timeval tv = {0};

	disconnect_time = seconds();

	if( create ) {	// create socket
		if( stConfigServer.log_enable > 1 )
			logging("forwarder[%s][%ld]: start connect to remote host %s:%d\n", config->name, syscall(SYS_gettid), config->server, config->port);

		if( config->sockets[OUT_SOCKET] == -1 ) {
			config->sockets[OUT_SOCKET] = socket(AF_INET, config->protocol, 0);
			if( config->sockets[OUT_SOCKET] < 0 ) {
				logging("forwarder[%s][%ld]: socket() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				return 0;
			}

			// set non-blocking mode
			if( fcntl(config->sockets[OUT_SOCKET], F_SETFL, O_NONBLOCK) < 0 ) {
				logging("forwarder[%s][%ld]: fcntl(OUT_SOCKET, O_NONBLOCK) error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
				close(config->sockets[OUT_SOCKET]);
				config->sockets[OUT_SOCKET] = -1;
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
				config->sockets[OUT_SOCKET] = -1;
				return 0;
			}
		}	// if( config->sockets[OUT_SOCKET] == -1 )

		// connect socket to external address
		memset(&addr_in, 0, sizeof(struct sockaddr_in));
		addr_in.sin_family = AF_INET;
		addr_in.sin_port = htons(config->port);
		inet_aton(config->server, &addr_in.sin_addr);
		if( connect(config->sockets[OUT_SOCKET], (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) < 0 ) {
			if( errno != EINPROGRESS ) {	// non-blocking socket, connection not in progress (error)
				logging("forwarder[%s][%ld]: connect(%s:%d) error %d: %s\n", config->name, syscall(SYS_gettid), config->server, config->port, errno, strerror(errno));
				close(config->sockets[OUT_SOCKET]);
				config->sockets[OUT_SOCKET] = -1;
			}
			// else connection in progress, see result of poll()
		}
		/* 24.04.17
		else
			out_connected = 1;
		*/

	}	// if( create )
	else {	// destroy socket
		out_connected = 0;
		shutdown(config->sockets[OUT_SOCKET], SHUT_RDWR);
		close(config->sockets[OUT_SOCKET]);
		config->sockets[OUT_SOCKET] = -1;

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

	if( !bufer ){
		if( stConfigServer.log_enable > 1 )
			logging("forwarder[%s][%ld]: process_terminal %s: bufer is NULL\n", config->name, syscall(SYS_gettid), msg->imei);
		return;
	}

	if( !size ){
		if( stConfigServer.log_enable > 1 )
			logging("forwarder[%s][%ld]: process_terminal %s: size = 0\n", config->name, syscall(SYS_gettid), msg->imei);
		return;
	}

	msg = (ST_FORWARD_MSG *)bufer;
	if( !msg->len ){
		if( stConfigServer.log_enable > 1 )
			logging("forwarder[%s][%ld]: process_terminal %s: msg->len = 0\n", config->name, syscall(SYS_gettid), msg->imei);
		return;
	}

	if( msg->encode ) {	// encode need, data = ST_RECORD*, msg->len = number of the records in data
		/* check: terminal authentificated or no on remote server,
		if no (ST_FORWARD_TERMINAL[imei][config->name].logged == 0) then set msg->len = -1 * msg->len
		else msg->len not change
		*/
		if( !terimal_logged(msg->imei, config->name) )
			msg->len *= -1;

		//if( strcmp(msg->imei, "868204004255146")==0 )
		//	logging("forwarder[%s][%ld]: process_terminal: %s msg->len=%d\n", config->name, syscall(SYS_gettid), msg->imei, msg->len);

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
				if( stConfigServer.log_enable > 1 )
				//if( strcmp(msg->imei, "868204004255146")==0 )
					logging("forwarder[%s][%ld]: process_terminal %s: sended %ld bytes to remote server\n", config->name, syscall(SYS_gettid), msg->imei, sended);
			}
		}	// if( out_connected )

		if( sended <= 0 ) {
			// save buffer to file for send later
			data_save(config->name, msg->imei, config->buffers[OUT_WRBUF], data_len);
		}

		//if( strcmp(msg->imei, "868204004255146")==0 )
			//log2file("/opt/glonassd/logs/868204004255146", config->buffers[OUT_WRBUF], data_len);
	}	// if( data_len )
	else {
		if( stConfigServer.log_enable > 1 )
			logging("forwarder[%s][%ld]: process_terminal %s: data_len = 0\n", config->name, syscall(SYS_gettid), msg->imei);
	}	// else if( data_len )
}
//------------------------------------------------------------------------------

// set up inner listener socket
static int set_listen_socket(ST_FORWARDER *config)
{

	// create socket
	config->sockets[IN_SOCKET] = socket(AF_UNIX, SOCK_DGRAM, 0);
	if( config->sockets[IN_SOCKET] < 0 ) {
		logging("forwarder[%s][%ld]: socket() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
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
	unsigned int cnt = 1;

	// test out socket and reconnect if disconnected
	if( config->sockets[OUT_SOCKET] == -1 && (seconds() - disconnect_time >= OUT_SOCKET_WAIT_TIME)) {
		set_out_socket(config, 1);
	}

	memset(&config->pollset, 0, CNT_SOCKETS * sizeof(struct pollfd));

	config->pollset[IN_SOCKET].fd = config->sockets[IN_SOCKET];
	config->pollset[IN_SOCKET].events = POLLIN + POLLRDHUP;
	config->pollset[IN_SOCKET].revents = 0;

	if( config->sockets[OUT_SOCKET] != -1 ) {
		config->pollset[OUT_SOCKET].fd = config->sockets[OUT_SOCKET];
		config->pollset[OUT_SOCKET].events = POLLIN + POLLRDHUP;
		if( !out_connected )
			config->pollset[OUT_SOCKET].events += POLLOUT;
		config->pollset[OUT_SOCKET].revents = 0;
		++cnt;
	}

	//                        milliseconds
	return poll(config->pollset, cnt, POLL_SOCKET_TIMEOUT * 1000);
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
	static __thread ssize_t bytes_read = 0;
	static __thread char fName[FILENAME_MAX];
	static __thread struct dirent *result;

	// eror handler:
	void exit_forwarder_thread(void * arg) {
		unsigned int i;

		// clear sockets
		for(i = 0; i < CNT_SOCKETS; i++ ) {
			if( config->sockets[i] != -1 ) {
				shutdown(config->sockets[i], SHUT_RDWR);
				close(config->sockets[i]);
				config->sockets[i] = -1;
			}
		}

		/*  When no longer required, the socket pathname,
		    should be deleted using unlink(2) or remove(3)
		*/
		if( strlen(config->addr_un.sun_path) )
			unlink(config->addr_un.sun_path);

		if( config->data_dir )
			closedir(config->data_dir);

		if( config->dir_item )
			free(config->dir_item);

		terimal_reset_logged(config->name);

		logging("forwarder %s[%ld] destroyed\n", config->name, syscall(SYS_gettid));
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
	config->sockets[IN_SOCKET] = -1;
	if( !set_listen_socket(config) ) {
		exit_forwarder_thread(NULL);
		return NULL;
	}

	// set outer server socket
	config->sockets[OUT_SOCKET] = -1;
	set_out_socket(config, 1);	// if not connected, will retry

	memset(&answer, 0, sizeof(ST_ANSWER));

	logging("forwarder %s[%ld] started\n", config->name, syscall(SYS_gettid));

	/*
	    main cycle
	*/
	while( 1 ) {

		pthread_testcancel();

		i = wait_sockets(config);
		switch( i ) {
		case -1:	// poll() error
			logging("forwarder[%s][%ld]: poll() error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
			exit_forwarder_thread(NULL);
			return NULL;
		case 0:	// timeout

			switch( config->sockets[OUT_SOCKET] ) {
			case -1:

				if( seconds() - disconnect_time >= OUT_SOCKET_WAIT_TIME ) {	// reconnect if disconnected
					set_out_socket(config, 1);
				}

				break;
			default:

				if( stConfigServer.log_enable > 1 )
					logging("forwarder[%s][%ld]: read and send saved parcels\n", config->name, syscall(SYS_gettid));

				/* read saved parcels and send to destination */
				if( config->data_dir ) {
					rewinddir(config->data_dir);	// resets the position of the directory stream to the beginning of the directory
					i = 0;	// number of files to read

					// iterate files in directory
					while( !readdir_r(config->data_dir, config->dir_item, &result) && result != NULL ) {
						// is file name OK & contain this forwarder name & contain ".bin"?
						if( strlen(config->dir_item->d_name)
								&& strstr(config->dir_item->d_name, config->name)
								&& strstr(config->dir_item->d_name, ".bin") ) {

							// generate full file name
							snprintf(fName, FILENAME_MAX, "%s/%s", stConfigServer.forward_files, config->dir_item->d_name);

							// open file for read
							if( (fHandle = open(fName, O_RDONLY | O_NOATIME)) != -1 ) {

								bytes_read = read(fHandle, config->buffers[IN_RDBUF], SOCKET_BUF_SIZE);
								if( bytes_read > 0 )
									process_terminal(config, config->buffers[IN_RDBUF], bytes_read);

								close(fHandle);

								if( stConfigServer.log_enable > 1 )
									logging("forwarder[%s][%ld]: send saved file %s\n", config->name, syscall(SYS_gettid), config->dir_item->d_name);
							}	// if( (fHandle = open(fName
							else if( stConfigServer.log_enable > 1 )
								logging("forwarder[%s][%ld]: read saved file %s error: %d: %s\n", config->name, syscall(SYS_gettid), config->dir_item->d_name, errno, strerror(errno));

							// delete file
							unlink(fName);

							if( ++i >= CNT_FILES_SEND )
								break;	// max files reached, cancel
						}	// if( strlen(dir_item->d_name) &&
					}	// while( (dir_item = readdir(data_dir)) != NULL )

				}	// if( data_dir )
				else {
					logging("forwarder[%s][%ld]: directory %s is not opened\n", config->name, syscall(SYS_gettid), stConfigServer.forward_files);
				}	// else if( data_dir )

			}	// switch( config->sockets[OUT_SOCKET] )

			break;
		default:	// the number of structures which have nonzero revents fields

			if( config->pollset[OUT_SOCKET].revents ) {
				// messages from remote server
				i = OUT_SOCKET;

				if( (config->pollset[i].revents & POLLOUT) || (config->pollset[i].revents & POLLWRNORM) ) {
					out_connected = !getsockopt(config->sockets[OUT_SOCKET], SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) && !so_error;
					if( out_connected )
						logging("forwarder[%s][%ld]: remote host %s:%d connected\n", config->name, syscall(SYS_gettid), config->server, config->port);
					else
						logging("forwarder[%s][%ld]: connecting to remote host %s:%d socket error %d: %s\n", config->name, syscall(SYS_gettid), config->server, config->port, so_error, strerror(so_error));
				}
				else if( (config->pollset[i].revents & POLLIN) || (config->pollset[i].revents & POLLRDNORM) ) {

					if( out_connected ) {

						// receive data
						memset(config->buffers[OUT_RDBUF], 0, SOCKET_BUF_SIZE);

						bytes_read = recv(config->sockets[OUT_SOCKET], config->buffers[OUT_RDBUF], SOCKET_BUF_SIZE, 0);
						if( bytes_read > 0 ) { // data
							if( stConfigServer.log_enable > 1 )
								logging("forwarder[%s][%ld]: received %ld bytes from remote host %s\n", config->name, syscall(SYS_gettid), bytes_read, config->server);

						    // decode server answer
						    if( config->terminal_decode ) {
								pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	// do not disturb :)
								config->terminal_decode(config->buffers[OUT_RDBUF], bytes_read, &answer);
								pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // can disturb :)
						    }	// if( config->terminal_decode )

						    if( answer.size ) {
								// send answer to terminal
								/*
								memcpy(config->buffers[OUT_WRBUF], answer.answer, answer.size);
								*/
						    }	// if( answer.size )

						}	// if( bytes_read > 0 )
						else { // nothing respond or error or close remote connection
							if( errno ) {
								logging("forwarder[%s][%ld]: remote host %s:%d error %d: %s\n", config->name, syscall(SYS_gettid), config->server, config->port, errno, strerror(errno));
								set_out_socket(config, 0);
							}
						}	// else if( bytes_read > 0 )

					}	// if( out_connected )
					else {
						if( stConfigServer.log_enable > 1 )
							logging("forwarder[%s][%ld]: events %u from socket, but socket not connected\n", config->name, syscall(SYS_gettid), config->pollset[i].revents);
					}

				}	// else if( (config->pollset[i].revents & POLLIN)
				else {	// error or close remote connection
					set_out_socket(config, 0);
					logging("forwarder[%s][%ld]: remote host %s:%d error %d: %s, revents=%u\n", config->name, syscall(SYS_gettid), config->server, config->port, errno, strerror(errno), config->pollset[i].revents);
				}

			}	// if( config->pollset[OUT_SOCKET].revents )
			else if( config->pollset[IN_SOCKET].revents ) {

				// messages from workers
				i = IN_SOCKET;

				if( (config->pollset[i].revents & POLLIN) || (config->pollset[i].revents & POLLRDNORM) ) {	// receive data

					memset(config->buffers[IN_RDBUF], 0, SOCKET_BUF_SIZE);
					bytes_read = recv(config->sockets[IN_SOCKET], config->buffers[IN_RDBUF], SOCKET_BUF_SIZE, 0);
					if( bytes_read > 0 ) {
						process_terminal(config, config->buffers[IN_RDBUF], bytes_read);
					}	// if( bytes_read > 0 )
					else {
						if( errno )
							logging("forwarder[%s][%ld]: local socket recv error %d: %s\n", config->name, syscall(SYS_gettid), errno, strerror(errno));
						else if( stConfigServer.log_enable > 1 )
							logging("forwarder[%s][%ld]: local socket recv return 0 bytes\n", config->name, syscall(SYS_gettid));
					}

				}	// if( config->pollset[i].revents & POLLIN )
				else {
					logging("forwarder[%s][%ld]: local socket error %d: %s, revents=%u\n", config->name, syscall(SYS_gettid), errno, strerror(errno), config->pollset[i].revents);
				}

			}	// if( config->pollset[IN_SOCKET].revents )

		}	// switch( wait_sockets(config) )

	}	// while(1)

	// clear error handler with run it (0 - not run, 1 - run)
	pthread_cleanup_pop(1);

	return NULL;
}	// void *forwarder_thread
//------------------------------------------------------------------------------
