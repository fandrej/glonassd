/*
   rds.c
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

   compile:
   make -B rds

   note:
   See comments in the end of this file

   help:
   http://citforum.ru/programming/unix/threads/
   http://citforum.ru/programming/unix/threads_2/
   http://man7.org/linux/man-pages/man7/sem_overview.7.html
   http://linux.die.net/man/7/mq_overview
   http://www.redov.ru/kompyutery_i_internet/unix_vzaimodeistvie_processov/p3.php#metkadoc75
   https://redis.io/docs/latest/develop/clients/hiredis/
   https://github.com/redis/hiredis
   https://github.com/redis/hiredis#pipelining
   https://yular.github.io/2017/01/28/C-Redis-QuickStart/
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>	/* syscall */
#include <stdio.h>			/* FILENAME_MAX */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <unistd.h>         /* sleep */
#include <time.h>           /* time */
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

extern ST_CONFIG_SERVER stConfigServer;	// main config

// Definitions
#define MAX_SQL_SIZE 4096
#define MAX_JSON_SIZE (512) // max size of json string one record (511 bytes)
#define MAX_ARRAY_SIZE (MAX_JSON_SIZE * 50 + 99 + 2 + 1) // 100 jsons + 99 ',' + '['']' + 0
#define DB_BATCH_SIZE (200)  // max records written by one pair of pipelines

// Locals
/* dropped stale records counter, touched only from db_thread */
static long long records_dropped = 0;
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

        logging("database thread[%ld]: try to connect to database %s on host %s:%d", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);

        if( *rds_context == NULL )
            *rds_context = redisConnectWithTimeout(stConfigServer.db_host, stConfigServer.db_port, timeout);

        if( *rds_context == NULL )
            logging("database thread[%ld]: db_connect: REDIS error: can't allocate redis context\n", syscall(SYS_gettid));
        else if ( (*rds_context)->err )
            logging("database thread[%ld]: db_connect: REDIS error: %s\n", syscall(SYS_gettid), (*rds_context)->errstr);
        else
            logging("database thread[%ld]: Connected to database %s on host %s:%d", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);

    }	// if(connect)
    else {	// disconnect from database

        if( *rds_context ) {
            redisFree(*rds_context);
            *rds_context = NULL;
            logging("database thread[%ld]: disconnect from database %s", syscall(SYS_gettid), stConfigServer.db_name);
        }

    }

    return(connect ? (*rds_context && !(*rds_context)->err) : 1);
}
//------------------------------------------------------------------------------

/*
   write_batch_to_db:
   record a batch of encoded gps/glonass terminal messages to database.
   One record used to cost GET + SET + SADD, three sequential round-trips. With
   s2 -> f7 RTT ~34 ms that capped the writer at ~10 records/s, and in rush hours
   the backlog grew up to max_record_age. Here all GETs of the batch go in one
   pipeline and all SET/SADD in a second one: two round-trips per batch.
   Points of one imei inside the batch are appended to its array in arrival order.
   msgs - array of pointers to ST_RECORD structures, n - their count
   return number of accepted records (dropped stale ones included) or 0 on error
*/
static int write_batch_to_db(char **msgs, int n, redisContext *rds_context)
{
    static __thread char (*arrays)[MAX_ARRAY_SIZE] = NULL;  // value of every unique key in the batch
    static __thread size_t lens[DB_BATCH_SIZE];             // strlen of arrays[k]
    static __thread char keys[DB_BATCH_SIZE][25];           // unique keys in the batch
    static __thread int rec_key[DB_BATCH_SIZE];             // key index of every record, -1 = dropped
    static __thread int sadd_key[DB_BATCH_SIZE];            // unique (key, port) pairs for SADD
    static __thread unsigned int sadd_port[DB_BATCH_SIZE];
    redisReply *rds_reply;
    ST_RECORD *record;
    char json[MAX_JSON_SIZE];
    char key[25];
    size_t json_size;
    int i, j, k, nkeys = 0, nsadd = 0, written = 0, result = 1;

    if( !rds_context || n <= 0 )
        return 0;
    if( n > DB_BATCH_SIZE )
        n = DB_BATCH_SIZE;

    if( !arrays ) {
        arrays = malloc((size_t)DB_BATCH_SIZE * MAX_ARRAY_SIZE);
        if( !arrays ) {
            logging("database thread[%ld]: write_batch_to_db: malloc(%ld) error\n",
                    syscall(SYS_gettid), (long)DB_BATCH_SIZE * MAX_ARRAY_SIZE);
            return 0;
        }
    }

    // 1. drop stale records, collect unique keys
    for(i = 0; i < n; i++) {
        record = (ST_RECORD *)msgs[i];
        rec_key[i] = -1;

        /* Drop stale records before touching Redis.
           After a receiver outage the units dump their black-box archive, and that
           archive squeezes real-time data out of the queue. The check sits before
           GET/SET so an archived point costs no round-trip at all. */
        if( stConfigServer.max_record_age > 0 ) {
            long long rec_ts = (long long)record->data + record->time;
            long long rec_age = (long long)time(NULL) - rec_ts;
            if( rec_ts > 0 && rec_age > stConfigServer.max_record_age ) {
                if( ++records_dropped % 1000 == 1 )
                    logging("database thread[%ld]: stale records dropped: %lld (last: imei %s, age %lld s)\n",
                            syscall(SYS_gettid), records_dropped, record->imei, rec_age);
                written++;  /* accepted and deliberately dropped, not a write error */
                continue;
            }
        }

        sprintf(key, "gd__%s", record->imei);
        for(k = 0; k < nkeys && strcmp(keys[k], key); k++)
            ;
        if( k == nkeys ) {
            strcpy(keys[nkeys], key);
            nkeys++;
        }
        rec_key[i] = k;
    }

    if( !nkeys )
        return written;

    // 2. get exists data: one pipeline for all keys
    for(k = 0; k < nkeys; k++)
        redisAppendCommand(rds_context, "GET %s", keys[k]);

    for(k = 0; k < nkeys; k++) {
        /* NULL reply on connection error: see the segfault history in 8ce413a */
        if( redisGetReply(rds_context, (void **)&rds_reply) != REDIS_OK || !rds_reply ) {
            logging("database thread[%ld]: write_batch_to_db: GET %s returned NULL: %s\n",
                    syscall(SYS_gettid), keys[k],
                    rds_context->err ? rds_context->errstr : "no error text");
            return 0;   /* db_thread sees rds_context->err and reconnects */
        }

        if( rds_reply->type == REDIS_REPLY_STRING && rds_reply->str && rds_reply->str[0] == '['
            && rds_reply->len < MAX_ARRAY_SIZE ) {
            memcpy(arrays[k], rds_reply->str, rds_reply->len);
            arrays[k][rds_reply->len] = 0;
            lens[k] = rds_reply->len;
        }
        else {
            arrays[k][0] = 0;
            lens[k] = 0;
        }
        freeReplyObject(rds_reply);
    }

    // 3. add records to arrays
    for(i = 0; i < n; i++) {
        if( (k = rec_key[i]) < 0 )
            continue;
        record = (ST_RECORD *)msgs[i];

        /* create JSON string aka:
        { "imei": "1234567890", "datetime": 1700000000, "lon": 55.5400, "lat": 65.6500, ... }
        */
        snprintf(json, MAX_JSON_SIZE, "{\"imei\": \"%s\", \"datetime\": %lld, \"lon\": %03.07lf, \"lat\": %03.07lf, "
                        "\"speed\": %03.01lf, \"curs\": %d, \"port\": %d, \"satellites\": %d, "
                        "\"height\": %d, \"valid\": %d, \"vbort\": %02.01lf, \"vbatt\": %02.01lf, "
                        "\"temperature\": %d, \"hdop\": %d, \"outputs\": %d, \"inputs\": %d, "
                        "\"fuel0\": %d, \"fuel1\": %d, \"probeg\": %04.03lf, \"zaj\": %d, \"alarm\": %d, "
                        "\"recnum\": %d, \"status\": %d}",
                    record->imei,
                    (long long)record->data + record->time,
                    record->lon,
                    record->lat,
                    record->speed,
                    record->curs,
                    record->port,
                    record->satellites,
                    record->height,
                    record->valid,
                    record->vbort,
                    record->vbatt,
                    record->temperature,
                    record->hdop,
                    record->outputs,
                    record->inputs,
                    record->fuel[0],
                    record->fuel[1],
                    record->probeg,
                    record->zaj,
                    record->alarm,
                    record->recnum,
                    record->status);
        json_size = strlen(json);
        if( !strcmp(stConfigServer.log_imei, record->imei) ) {
            logging("write_data_to_db: %s", json);
        }

        if( lens[k] > 0 && MAX_ARRAY_SIZE - lens[k] > json_size + 3 ) {
            // add to existing array
            snprintf(&arrays[k][lens[k] - 1], json_size + 3, ",%s]", json);
            lens[k] += json_size + 1;
        }
        else {
            // create new array
            snprintf(arrays[k], json_size + 3, "[%s]", json);
            lens[k] = json_size + 2;
        }

        for(j = 0; j < nsadd && !(sadd_key[j] == k && sadd_port[j] == record->port); j++)
            ;
        if( j == nsadd ) {
            sadd_key[nsadd] = k;
            sadd_port[nsadd] = record->port;
            nsadd++;
        }
        written++;
    }

    // 4. Set REDIS keys and add them to port sets: one pipeline
    // https://redis.io/commands/set
    // https://redis.io/commands/sadd
    for(k = 0; k < nkeys; k++)
        redisAppendCommand(rds_context, "SET %s %b", keys[k], arrays[k], lens[k]);
    for(j = 0; j < nsadd; j++)
        redisAppendCommand(rds_context, "SADD gd_port__%d %s", sadd_port[j], keys[sadd_key[j]]);

    for(i = 0; i < nkeys + nsadd; i++) {
        if( redisGetReply(rds_context, (void **)&rds_reply) != REDIS_OK || !rds_reply ) {
            logging("database thread[%ld]: write_batch_to_db: redisCommand() return NULL\n", syscall(SYS_gettid));
            return 0;   /* db_thread sees rds_context->err and reconnects */
        }
        if( rds_reply->type == REDIS_REPLY_ERROR ) {
            logging("database thread[%ld]: write_batch_to_db: redisCommand() error: %s\n", syscall(SYS_gettid), rds_reply->str);
            result = 0;
        }
        freeReplyObject(rds_reply);
    }

    return result ? written : 0;
}
//------------------------------------------------------------------------------

/*
   write_data_to_db:
   record one encoded gps/glonass terminal message to database
   msg - pointer to ST_RECORD structure
   return 1 if success or 0 if error
*/
static int write_data_to_db(char *msg, redisContext *rds_context)
{
    return write_batch_to_db(&msg, 1, rds_context) > 0;
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
    /* union keeps every slot aligned as ST_RECORD: a plain char[233] stride would not */
    static __thread union { ST_RECORD record; char raw[sizeof(ST_RECORD) + 1]; } batch_buf[DB_BATCH_SIZE];
    static __thread char *batch_msgs[DB_BATCH_SIZE];
    static __thread int batch_n;
    static __thread struct timespec no_wait;

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
    //logging("database thread[%ld]: sizeof(ST_RECORD)= %ld\n", syscall(SYS_gettid), (long)sizeof(ST_RECORD));

    // get RLIMIT_MSGQUEUE and calculate actual size of queue
    if( getrlimit(RLIMIT_MSGQUEUE, &rlim) == 0 ) {
        logging("database thread[%ld]: rlim.rlim_cur= %lld, rlim.rlim_max= %lld\n", syscall(SYS_gettid), rlim.rlim_cur, rlim.rlim_max);

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
    for(batch_n = 0; batch_n < DB_BATCH_SIZE; batch_n++)
        batch_msgs[batch_n] = batch_buf[batch_n].raw;

    // queue files located in: /dev/mqueue
    queue_workers = mq_open(QUEUE_WORKER, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR, &queue_attr);
    if( queue_workers < 0 ) {
        logging("database thread[%ld]: mq_open(%s) error %d: %s\nTry this:\nSetup 'POSIX message queues' size in /etc/security/limits.conf as:\n*\thard\tmsgqueue\t%ld\nSee 'POSIX message queues' size as: ulimit -a", syscall(SYS_gettid), QUEUE_WORKER, errno, strerror(errno), (long)(65536 * queue_attr.mq_msgsize * 10));
        exit_db(arg);
        return NULL;
    }

    logging("database thread[%ld] started, queue (%s) size %ld msgs\n", syscall(SYS_gettid), QUEUE_WORKER, (long)queue_attr.mq_maxmsg);
    logging("database thread[%ld]: max_record_age=%d sec\n", syscall(SYS_gettid), stConfigServer.max_record_age);

    // try to connect to database
    db_connect(1, &rds_context);

    // wait messages
    while( 1 ) {
        pthread_testcancel();

        if( rds_context && !rds_context->err ) {
            msg_size = mq_receive(queue_workers, batch_msgs[0], buf_size, NULL);
            if( msg_size > 0 ) {
                /* take what is already waiting in the queue, without blocking,
                   and write it all with one batch */
                batch_n = 1;
                clock_gettime(CLOCK_REALTIME, &no_wait);
                while( batch_n < DB_BATCH_SIZE &&
                       mq_timedreceive(queue_workers, batch_msgs[batch_n], buf_size, NULL, &no_wait) > 0 )
                    batch_n++;
                write_batch_to_db(batch_msgs, batch_n, rds_context);	// write messages to database
            }
            else if ( msg_size < 0 && errno == EAGAIN )
                sleep(0.01);	// wait
        }
        else {
            if( rds_context )   // connected, but error
                db_connect(0, &rds_context);

            sleep(3);	// wait

            db_connect(1, &rds_context);	// try again
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