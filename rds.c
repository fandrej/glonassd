/*
   rd.c
   REDIS library:
   a) writer (db_thread):
   1. connect to REDIS
   3. create messages queue
   5. wait message from workers, where message contain decoded gps/glonass terminal data (coordinates, etc)
   7. write message data to REDIS
   13. goto 5.

   b) timer_function:
   run from timers,
   connect to REDIS, load redis command from file and run it

   caution:
   1. For set up system limits, daemon user must have appropriate rights
   2. REDIS functions can use *alloc* functions internally,
    which leads to leakage of the memory, if they called from threads, used shared library modules.
    This is evident, for example, on a timers routines.

   note:
   See comments in the end of this file

   help:
   http://citforum.ru/programming/unix/threads/
   http://citforum.ru/programming/unix/threads_2/
   http://man7.org/linux/man-pages/man7/sem_overview.7.html
   http://linux.die.net/man/7/mq_overview
   http://www.redov.ru/kompyutery_i_internet/unix_vzaimodeistvie_processov/p3.php#metkadoc75
   https://redis.io/clients#c
   https://github.com/redis/hiredis
   https://yular.github.io/2017/01/28/C-Redis-QuickStart/
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // _GNU_SOURCE
#include <sys/syscall.h>	/* syscall */
#include <stdio.h>			/* FILENAME_MAX */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <unistd.h>         /* sleep */
#include <mqueue.h>
#include <sys/types.h>
#include <sys/stat.h>       /* mode constants */
#include <fcntl.h>          /* mq_open, O_* constants */
#include <semaphore.h>
#include <sys/resource.h>	/* setrlimit */
#include <hiredis/hiredis.h>
//#include <json-c/json.h>
#include "glonassd.h"
#include "de.h"
#include "logger.h"

// Definitions
#define MAX_SQL_SIZE 4096

// Locals
/*
   Secondary functions
*/

/*
   load_file:
   read file content
   path - full path to file include file name
   return 1 if success & 0 if error
*/
static int load_file(char *path, char *buf, size_t bufsize)
{
    int fp;
    size_t size, readed;

    // check file exists
    if( (fp = open(path, O_RDONLY)) == BAD_OBJ ) {
        logging("database thread[%ld]: open(%s): error %d: %s\n", syscall(SYS_gettid), path, errno, strerror(errno));
        return 0;
    }

    // check file size
    size = lseek(fp, 0, SEEK_END);
    if( size == -1L || lseek(fp, 0, SEEK_SET) ) {
        logging("database thread[%ld]: lseek(SEEK_END) error %d: %s\n", syscall(SYS_gettid), errno, strerror(errno));
        close(fp);
        return 0;
    } else if( size >= bufsize ) {
        logging("database thread[%ld]: sql file size %d >= buffer size %d\n", syscall(SYS_gettid), size, bufsize);
        close(fp);
        return 0;
    } else if( !size ) {
        logging("database thread[%ld]: sql file size = %d, is file empty?\n", syscall(SYS_gettid), size);
        close(fp);
        return 0;
    }

    // read file content into buffer
    memset(buf, 0, bufsize);
    if( (readed = read(fp, buf, size)) != size ) {
        logging("database thread[%ld]: read(%ld)=%ld error %d: %s\n", syscall(SYS_gettid), size, readed, errno, strerror(errno));
        close(fp);
        return 0;
    }

    close(fp);
    return 1;
}
//------------------------------------------------------------------------------

/*
   db_connect:
   connection to / disconnection from database
   connect - flag of connect (1) or disconnect (0)
   rds_context - pointer to redisContext *
   return 1 if success or 0 if error
*/
static int db_connect(int connect, redisContext **rds_context)
{
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds

    if(connect) {	// connecting to database

        if( *rds_context == NULL )
            *rds_context = redisConnectWithTimeout(stConfigServer.db_host, stConfigServer.db_port, timeout);

        if( *rds_context == NULL )
            logging("database thread[%ld]: REDIS error: can't allocate redis context\n", syscall(SYS_gettid));
        else if ( (*rds_context)->err )
            logging("database thread[%ld]: REDIS error: %s\n", syscall(SYS_gettid), (*rds_context)->errstr);

    }	// if(connect)
    else {	// disconnect from database

        if( *rds_context ) {
            redisFree(*rds_context);
            *rds_context = NULL;
        }

    }

    return(connect ? (*rds_context && !(*rds_context)->err) : 1);
}
//------------------------------------------------------------------------------

/*
   write_data_to_db:
   record encoded gps/glonass terminal message to database
   connection - database connection
   msg - pointer to ST_RECORD structure
   return 1 if success or 0 if error
*/
static int write_data_to_db(char *msg, redisContext *rds_context)
{
    redisReply *rds_reply;
    ST_RECORD *record;
    char json[MAX_SQL_SIZE];
    int result = 0;

    if( !rds_context )
        return result;

    record = (ST_RECORD *)msg;

    /* create JSON string aka:
    { "imei": "1234567890", "data": 0, "time": 50400, "lon": 55.5400, "lat": 65.6500, "speed": 20.0, "curs": 40, "port": 19005 }
    */

    /*
    with json-c library:
        https://linuxprograms.wordpress.com/2010/08/19/json_object_new_object/

    json_object * jobj = json_object_new_object();
    //                              key        value
    json_object_object_add(jobj, "imei", json_object_new_string(record->imei));
    json_object_object_add(jobj, "data", json_object_new_int64(record->data));
    json_object_object_add(jobj, "time", json_object_new_int(record->time));
    json_object_object_add(jobj, "lon", json_object_new_double(record->lon));
    json_object_object_add(jobj, "lat", json_object_new_double(record->lat));
    json_object_object_add(jobj, "speed", json_object_new_double(record->speed));
    json_object_object_add(jobj, "curs", json_object_new_int(record->curs));
    json_object_object_add(jobj, "port", json_object_new_int(record->port));

    rds_reply = redisCommand(rds_context, "SET gd_%s %s", record->imei, json_object_to_json_string(jobj));
    */

    /*
    without json-c library
    */
    sprintf(json, "{ \"imei\": \"%s\", \"datetime\": %lld, \"lon\": %03.07lf, \"lat\": %03.07lf, \"speed\": %03.01lf, \"curs\": %d, \"port\": %d, \"satellites\": %d }",
                record->imei,
                (long long)record->data + record->time,
                record->lon,
                record->lat,
                record->speed,
                record->curs,
                record->port,
                record->satellites);
    //logging("write_data_to_db: %s", json);

    /*
    Set a REDIS key
    https://redis.io/commands/set
    */
    rds_reply = redisCommand(rds_context, "SET gd__%s %s", record->imei, json);
    result = rds_reply ? rds_reply->type != REDIS_REPLY_ERROR : 0;

    if( result ){
        // https://redis.io/commands/sadd
        rds_reply = redisCommand(rds_context, "SADD gd_port__%d gd__%s", record->port, record->imei);
        result = result && rds_reply ? rds_reply->type != REDIS_REPLY_ERROR : 0;
    }

    if( !result ){
        if( rds_reply )
            logging("database thread[%ld]: redisCommand() error: %s\n", syscall(SYS_gettid), rds_reply->str);
        else
            logging("database thread[%ld]: redisCommand() return NULL\n", syscall(SYS_gettid));
    }

    freeReplyObject(rds_reply);

    return result;
}
//------------------------------------------------------------------------------



/*
   Main functions
*/

/*
   db_thread
   works in separate thread
   started from func. database_setup in glonassd.c
   arg - pointer to main configuration structure (stConfigServer)
*/
void *db_thread(void *arg)
{
    static __thread redisContext *rds_context = NULL;
    static __thread char msg_buf[SOCKET_BUF_SIZE];
    static __thread mqd_t queue_workers = -1;	// Posix IPC queue of messages from workers
    static __thread struct mq_attr queue_attr;
    static __thread struct rlimit rlim;
    static __thread ssize_t msg_size;
    static __thread size_t buf_size;

    // error handler:
    void exit_db(void * arg) {

        // destroy queue
        if( queue_workers != -1 ) {
            // save messages from queue
            if( mq_getattr(queue_workers, &queue_attr) == 0 && queue_attr.mq_curmsgs > 0 ) {
                logging("database thread writing %ld messages\n", queue_attr.mq_curmsgs);

                while( (msg_size = mq_receive(queue_workers, msg_buf, buf_size, NULL)) > 0 ) {
                    if( rds_context )
                        write_data_to_db(msg_buf, rds_context);
                    else
                        break;
                }   // while
            }	// if( mq_getattr

            mq_close(queue_workers);
            /*
               hmmm, if not destroy, can i retrieve messages from queue after restart?
               to be queue stored messages when daemon crash?
               answer: YES, until destroy queue with mq_unlink all messages strored in queue
               and can be retrieve after reopen.
            */

            mq_unlink(QUEUE_WORKER);
        }   // if( queue_workers != -1 )

        // disconnect from database
        db_connect(0, &rds_context);

        logging("database thread[%ld] destroyed\n", syscall(SYS_gettid));
    }   // exit_db

    // install error handler:
    pthread_cleanup_push(exit_db, arg);

    // create messages queue
    memset(&queue_attr, 0, sizeof(struct mq_attr));

    /* test system limit of the length of messages queue RLIMIT_MSGQUEUE
       by default 819200 bytes
       setup RLIMIT_MSGQUEUE size in /etc/security/limits.conf as:
        hard	msgqueue	1342177280
       and reboot;
       see limits as:
       ulimit -a
       "POSIX message queues"
    */

    /* Max. message size (bytes) */
    queue_attr.mq_msgsize = sizeof(ST_RECORD);

    // get RLIMIT_MSGQUEUE and calculate actual size of queue
    if( getrlimit(RLIMIT_MSGQUEUE, &rlim) == 0 ) {
        if( rlim.rlim_cur != rlim.rlim_max ) {	// increase RLIMIT_MSGQUEUE error
            rlim.rlim_cur = rlim.rlim_max;
            // calculate actual size of queue
            if( setrlimit(RLIMIT_MSGQUEUE, &rlim) == 0 )
                queue_attr.mq_maxmsg = (long)(rlim.rlim_max / queue_attr.mq_msgsize / 10);
            else
                queue_attr.mq_maxmsg = (long)(rlim.rlim_cur / queue_attr.mq_msgsize / 10);
        } else
            queue_attr.mq_maxmsg = (long)(rlim.rlim_cur / queue_attr.mq_msgsize / 10);
    } else {
        logging("database thread[%ld]: getrlimit() error %d: %s\n", syscall(SYS_gettid), errno, strerror(errno));
        queue_attr.mq_maxmsg = (long)(819200 / queue_attr.mq_msgsize / 10);     /* Max. # of messages on queue */
    }

    // calculate buffer size for messages
    buf_size = queue_attr.mq_msgsize + 1;

    queue_workers = mq_open(QUEUE_WORKER, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR, &queue_attr);
    if( queue_workers < 0 ) {
        logging("database thread[%ld]: mq_open() error %d: %s\n", syscall(SYS_gettid), errno, strerror(errno));
        logging("Try this:\n");
        logging("Setup 'POSIX message queues' size in /etc/security/limits.conf as:\n");
        logging("*\thard\tmsgqueue\t%ld", (long)(65536 * queue_attr.mq_msgsize * 10));
        logging("See 'POSIX message queues' size as: ulimit -a");
        exit_db(arg);
        return NULL;
    }

    logging("database thread[%ld] started, queue size %ld msgs\n", syscall(SYS_gettid), (long)queue_attr.mq_maxmsg);

    // try to connect to database
    if( !db_connect(1, &rds_context) )
        logging("database thread[%ld]: Can't connect to REDIS on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_host, stConfigServer.db_port);
    else
        logging("database thread[%ld]: Connected to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);

    /* test
    ST_RECORD rec;
    sprintf(rec.imei, "%s", "1234567890");
    rec.time = 50000;
    rec.lon = 55.55;
    rec.lat = 66.66;
    rec.speed = 10;
    rec.curs = 100;
    rec.port = 19009;

    while( 1 ) {
        pthread_testcancel();

        if( rds_context && !rds_context->err ) {
            write_data_to_db((char *)&rec, rds_context);	// write message to database
        }

        sleep(5);	// wait
    }	// while( 1 )
    */

    // wait messages
    while( 1 ) {
        pthread_testcancel();

        if( rds_context && !rds_context->err ) {
            msg_size = mq_receive(queue_workers, msg_buf, buf_size, NULL);
            if( msg_size > 0 )
                write_data_to_db(msg_buf, rds_context);	// write message to database
        } else {
            sleep(3);	// wait
            if( db_connect(1, &rds_context) )	// try again
                logging("database thread[%ld]: Connected to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
        }

    }	// while( 1 )

    // clear error handler with run it (0 - not run, 1 - run)
    pthread_cleanup_pop(1);
    return NULL;
}
//------------------------------------------------------------------------------


/*
   timer_function:
   call from timers, work in separate thread (thread created by timer)
   connect to database, load sql script and run it
   ptr - pointer to struct ST_TIMER, see glonassd.h
*/
void *timer_function(void *ptr)
{
    static __thread ST_TIMER *st_timer = 0;
    static __thread const char *name = 0;
    static __thread sem_t *semaphore = SEM_FAILED;
    static __thread char sql[MAX_SQL_SIZE];
    static __thread redisContext *rds_context = NULL;
    //static __thread redisReply *rds_reply = NULL;

    // eror handler:
    void exit_timerfunc(void * arg) {
        if( rds_context )
            db_connect(0, &rds_context);

        if( semaphore != SEM_FAILED ) {
            sem_close(semaphore);
            sem_unlink(name);
        }

        pthread_detach(pthread_self());
    }	// exit_timerfunc

    // install error handler:
    pthread_cleanup_push(exit_timerfunc, ptr);

    // initialise
    st_timer = (ST_TIMER *)ptr;
    name = strrchr(st_timer->script_path, '/');
    semaphore = sem_open(name, O_CREAT | O_EXCL, O_RDWR, 0);	// create named semaphore

    if( semaphore != SEM_FAILED ) {	// if semaphore not exists, continue

        if( !load_file(st_timer->script_path, sql, MAX_SQL_SIZE) || !strlen(sql) ) {
            exit_timerfunc(ptr);
            return NULL;
        }

        if( !db_connect(1, &rds_context) ) {
            exit_timerfunc(ptr);
            return NULL;
        }

        /* TODO:
        exec REDIS command?
        */
    }	// if( semaphore != SEM_FAILED )
    else {
        if( errno == EEXIST )
            logging("timer[%ld]: %s already running, increase period, please\n", syscall(SYS_gettid), name);
        else
            logging("timer[%ld]: %s: sem_open() error %d: %s\n", syscall(SYS_gettid), name, errno, strerror(errno));
    }

    // clear error handler with run it (0 - not run, 1 - run)
    pthread_cleanup_pop(1);
    return NULL;
}
//------------------------------------------------------------------------------

/*
Install hiredis library:

git clone https://github.com/redis/hiredis.git
cd hiredis
make
sudo make install
sudo mkdir /usr/include/hiredis
sudo cp libhiredis.so /usr/lib/
sudo cp hiredis.h /usr/include/hiredis/
sudo cp read.h /usr/include/hiredis/
sudo cp sds.h /usr/include/hiredis/
sudo ldconfig

In your.c file:
#include <hiredis/hiredis.h>

help:
https://redis.io/clients#c
https://github.com/redis/hiredis
https://yular.github.io/2017/01/28/C-Redis-QuickStart/
-------------------------------------------------------

Install json-c library:

sudo apt-get install autoconf
sudo apt-get install automake
sudo apt-get install libtool

git clone https://github.com/json-c/json-c.git
cd json-c
sh autogen.sh
./configure #--prefix=/usr/lib
make
sudo make check
sudo make install
sudo ldconfig

In your.c file:
#include <json-c/json.h>

help:
https://github.com/json-c/json-c/wiki
https://linuxprograms.wordpress.com/category/json-c/page/3/
https://linuxprograms.wordpress.com/2010/08/19/json_object_new_object/
*/
