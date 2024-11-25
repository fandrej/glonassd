/*
   logger.c
   daemons's logger
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>	/* syscall */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fcntl.h>        /* mq_open, O_* constants */
#include <unistd.h>		/* sleep */
#include <sys/time.h>
#include <sys/resource.h>
#include <mqueue.h>
#include "glonassd.h"
#include "logger.h"
#include "de.h"
#include "lib.h"


static void writelog(int fHandle, char *msg_buf, int buf_size);
static void setfilesize(char *log_file, size_t log_maxsize);

// logging
void logging(char *template, ...)
{
	static __thread va_list ptr;
	static __thread int len = 0;
	static __thread char message[LOG_MSG_SIZE]= {0};
	static __thread int no_queue = 0;
	static __thread mqd_t log_queue = 0;

	va_start(ptr, template);
	memset(message, 0, LOG_MSG_SIZE);
	len = vsnprintf(message, LOG_MSG_SIZE, template, ptr);
	va_end(ptr);

	log_queue = mq_open(QUEUE_LOGGER, O_WRONLY | O_NONBLOCK);
	if( log_queue < 0 ) {
		//syslog(LOG_NOTICE, "logging: mq_open(%s) error %d: %s\n", QUEUE_LOGGER, errno, strerror(errno));
		syslog(LOG_NOTICE, "%s", message);
	}
    else {
		if( mq_send(log_queue, (const char *)message, len, 0) < 0 ) {
			//syslog(LOG_NOTICE, "logging: mq_send(log_queue) error %d: %s\n", errno, strerror(errno));
			syslog(LOG_NOTICE, "%s", message);
		}
		mq_close(log_queue);
	}

    if( !stParams.daemon )
        fprintf(stderr, "%s\n", message);
}
//------------------------------------------------------------------------------

// logger thread function
void *log_thread_func(void *arg)
{
	int fHandle = BAD_OBJ;
	mqd_t queue_log = BAD_OBJ;	// Posix IPC queue of messages from workers
	char msg_buf[LOG_MSG_SIZE];
	struct mq_attr queue_attr;
	ssize_t msg_size;
	struct rlimit rlim;

	// eror handler:
	void exit_logger(void * arg) {

		if( queue_log != BAD_OBJ ) {

			// save messages from queue
			if( mq_getattr(queue_log, &queue_attr) == 0 && queue_attr.mq_curmsgs > 0 ) {
				memset(msg_buf, 0, LOG_MSG_SIZE);

				while( (msg_size = mq_receive(queue_log, msg_buf, LOG_MSG_SIZE, NULL)) > 0 ) {
					writelog(fHandle, msg_buf, msg_size);
					memset(msg_buf, 0, LOG_MSG_SIZE);
				}   // while
			}   // if

			// destroy queue
			mq_close(queue_log);
			mq_unlink(QUEUE_LOGGER);
		}   // if( queue_log != BAD_OBJ )

		msg_size = snprintf(msg_buf, LOG_MSG_SIZE, "logger[%ld] destroyed\n", syscall(SYS_gettid));
		writelog(fHandle, msg_buf, msg_size);

		if( fHandle != BAD_OBJ )
			close(fHandle);
	}	// exit_logger

	// install eror handler:
	pthread_cleanup_push(exit_logger, arg);


	// calculate messages queue size
	memset(&queue_attr, 0, sizeof(struct mq_attr));
	// Max. message size (bytes)
	queue_attr.mq_msgsize = LOG_MSG_SIZE;

	// get limit to queue size in bytes
	// calc Max. # of messages on queue
	if( !getrlimit(RLIMIT_MSGQUEUE, &rlim) )
	    queue_attr.mq_maxmsg = (long) min(rlim.rlim_cur, rlim.rlim_max) / queue_attr.mq_msgsize / 10;
    else
	    queue_attr.mq_maxmsg = (long) 819200 / queue_attr.mq_msgsize / 10;

	// create messages queue
	//mq_unlink(QUEUE_LOGGER);
    // queue files located in: /dev/mqueue
	queue_log = mq_open(QUEUE_LOGGER, O_RDONLY | O_CREAT, S_IWGRP | S_IWUSR, &queue_attr);
	if( queue_log < 0 ) {
		syslog(LOG_NOTICE, "logger[%ld]: mq_open(%s) error %d: %s\n", syscall(SYS_gettid), QUEUE_LOGGER, errno, strerror(errno));
		exit_logger(arg);
		return NULL;
	}

	// control log-file size
	setfilesize(stConfigServer.log_file, stConfigServer.log_maxsize);

	// open log-file
	if( (fHandle = open(stConfigServer.log_file,
							  O_APPEND | O_CREAT | O_RDWR,
							  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == BAD_OBJ ) {
		syslog(LOG_NOTICE, "logger[%ld]: open(%s) error %d: %s\n", syscall(SYS_gettid), stConfigServer.log_file, errno, strerror(errno));
		/*
		   exit_logger(arg);
		   return NULL;

		   logging to syslog
		*/
	}

	logging("\n");
	logging("glonassd[%ld] started", getpid());
	logging("logger[%ld]: started\n", syscall(SYS_gettid));

	// wait messages
	while( 1 ) {
		pthread_testcancel();

		memset(msg_buf, 0, LOG_MSG_SIZE);
		msg_size = mq_receive(queue_log, msg_buf, LOG_MSG_SIZE, NULL);
		if( msg_size > 0 )
			writelog(fHandle, msg_buf, msg_size);

	}	// while( 1 )


	// clear error handler with run it (0 - not run, 1 - run)
	pthread_cleanup_pop(1);

	return NULL;
}
//------------------------------------------------------------------------------

static void writelog(int fHandle, char *logmsg, int len)
{
	char buf[LOG_MSG_SIZE];
	int loglen = 0;
	time_t t;
	struct tm local;

	if( logmsg && len > 0 ) {
		t = time(NULL);
		localtime_r(&t, &local);

		memset(buf, 0, LOG_MSG_SIZE);

		if( logmsg[len-1] == 10 ) {
			loglen = snprintf(buf, LOG_MSG_SIZE, "%02d.%02d.%02d %02d:%02d:%02d %s",
									local.tm_mday, local.tm_mon+1, local.tm_year-100,
									local.tm_hour, local.tm_min, local.tm_sec,
									logmsg);
		} else {
			loglen = snprintf(buf, LOG_MSG_SIZE, "%02d.%02d.%02d %02d:%02d:%02d %s\n",
									local.tm_mday, local.tm_mon+1, local.tm_year-100,
									local.tm_hour, local.tm_min, local.tm_sec,
									logmsg);
		}
	}   // if( len > 0 )

	if( loglen > 0 ) {
		if( fHandle != BAD_OBJ ) {
			if( write(fHandle, buf, loglen) < 1 ) {
				syslog(LOG_NOTICE, "logger[%ld]: write(%d) error %d: %s\n", syscall(SYS_gettid), loglen, errno, strerror(errno));
				syslog(LOG_NOTICE, "%s", buf);
			}
		}   // if( fHandle != BAD_OBJ )
		else {
			syslog(LOG_NOTICE, "%s", buf);
		}   // if( fHandle != BAD_OBJ )
	}   // if( loglen > 0 )
}
//------------------------------------------------------------------------------

static void setfilesize(char *log_file, size_t log_maxsize)
{
	struct stat filestat;
	char buf[FILENAME_MAX];

	if( !stat(log_file, &filestat) ) {
		if( filestat.st_size >= log_maxsize ) {
			// create old-file-name
			memset(buf, 0, FILENAME_MAX);
			snprintf(buf, FILENAME_MAX, "%s.old", log_file);
			// delete old file with old-file-name & rename current file to old-file-name
			unlink(buf);
			if( rename(log_file, buf) ) {
				syslog(LOG_NOTICE, "logger[%ld]: rename(%s, %s) error %d: %s\n", syscall(SYS_gettid), log_file, buf, errno, strerror(errno));
				if( unlink(log_file) )
					syslog(LOG_NOTICE, "logger[%ld]: unlink(%s) error %d: %s\n", syscall(SYS_gettid), log_file, errno, strerror(errno));
			}
		}   // if( filestat.st_size >= log_maxsize )
	}   // if( !stat(log_file, &filestat) )
	else {
		if( errno != 2 )	//  No such file or directory (after delete file, ex.)
			syslog(LOG_NOTICE, "logger[%ld]: stat(%s) error %d: %s\n", syscall(SYS_gettid), log_file, errno, strerror(errno));
	}
}
//------------------------------------------------------------------------------
