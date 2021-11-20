/*
    oracle.c
    Oracle DB library:
    a) writer (db_thread):
    1. connect to database
    3. create messages queue
    5. wait message from workers, where message contain decoded gps/glonass terminal data (coordinates, etc)
    7. write message data to database (into special table)
    13. goto 5.
    todo:
    9. get command from database to gps terminal
    11. put commant into worker

    b) timer_function:
    run from timers,
    connect to database, load sql script and run it

    caution:
    1. For set up system limits, daemon user must have appropriate rights
    2. PostgreSQL functions, such as PQconnectdb, used *alloc* functions internally,
    which leads to leakage of the memory, if they called from threads, used shared library modules.
    This is evident, for example, on a timers routines.

    note:
    see table structure at the end of this file
    see SQL-script for insert record to table at the end of this file

    help:
    Oracle Database Programming Interface for C (ODPI-C):
    https://oracle.github.io/odpi/
    https://oracle.github.io/odpi/doc/index.html
    https://oracle.github.io/odpi/doc/installation.html#oracle-instant-client-zip-files

    https://github.com/oracle/odpi/tree/main/samples
    https://github.com/oracle/odpi/blob/main/samples/DemoInsert.c

    https://blogs.oracle.com/opal/post/odpi-c-a-light-weight-driver-for-oracle-database
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
#include <dpi.h>            /* https://github.com/oracle/odpi/blob/main/include/dpi.h */
#include <ctype.h>
#include "glonassd.h"
#include "de.h"
#include "logger.h"

#ifdef _MSC_VER
#if _MSC_VER < 1900
#define PRId64                  "I64d"
#define PRIu64                  "I64u"
#endif
#endif

#ifndef PRIu64
#include <inttypes.h>
#endif

#define MAX_SQL_SIZE 4096

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
		logging("thread[%ld]: open(%s): error %d: %s\n", syscall(SYS_gettid), path, errno, strerror(errno));
		return 0;
	}

	// check file size
	size = lseek(fp, 0, SEEK_END);
	if( size == -1L || lseek(fp, 0, SEEK_SET) ) {
		logging("thread[%ld]: lseek(SEEK_END) error %d: %s\n", syscall(SYS_gettid), errno, strerror(errno));
		close(fp);
		return 0;
	} else if( size >= bufsize ) {
		logging("thread[%ld]: sql file size %d >= buffer size %d\n", syscall(SYS_gettid), size, bufsize);
		close(fp);
		return 0;
	} else if( !size ) {
		logging("thread[%ld]: sql file size = %d, is file empty?\n", syscall(SYS_gettid), size);
		close(fp);
		return 0;
	}

	// read file content into buffer
	memset(buf, 0, bufsize);
	if( (readed = read(fp, buf, size)) != size ) {
		logging("thread[%ld]: read(%ld)=%ld error %d: %s\n", syscall(SYS_gettid), size, readed, errno, strerror(errno));
		close(fp);
		return 0;
	}

	close(fp);
	return 1;
}
//------------------------------------------------------------------------------


void db_log_error(dpiContext *gContext, const char *message)
{
    dpiErrorInfo info;
    if( gContext ){
        dpiContext_getError(gContext, &info);
        if( message ) {
            logging("database thread[%ld]: %s: %s\n", syscall(SYS_gettid), message, info.message);
        }
        else {
            logging("database thread[%ld]: %s\n", syscall(SYS_gettid), info.message);
        }
    }
    else {
        if( message ) {
            logging("database thread[%ld]: %s\n", syscall(SYS_gettid), message);
        }
        else {
            logging("database thread[%ld]: db_log_error: gContext is NULL\n", syscall(SYS_gettid));
        }
    }
}   // db_log_error
//------------------------------------------------------------------------------


/*
   db_connect:
   connection to / disconnection from database
   params:
   connect - flag of connect (1) or disconnect (0)
   connection - pointer to dpiConn
   return 1 if success or 0 if error
*/
static int db_connect(int connect, dpiConn **connection, dpiContext **gContext)
{
    dpiErrorInfo errorInfo;

	if(connect) {	// connecting to database

		if( !*connection ) {
            // perform initialization
            if ( !*gContext ) {
                // https://oracle.github.io/odpi/doc/functions/dpiContext.html#c.dpiContext_createWithParams
                if (dpiContext_createWithParams(DPI_MAJOR_VERSION, DPI_MINOR_VERSION, NULL, gContext, &errorInfo) < 0) {
                    logging("database thread[%ld]: %s: %s\n", syscall(SYS_gettid), "Cannot create DPI context.", errorInfo.message);
                    dpiContext_destroy(*gContext);
                    *gContext = NULL;
                }
            }

            // create a standalone connection
            if ( *gContext ) {
                // https://oracle.github.io/odpi/doc/functions/dpiConn.html#c.dpiConn_create
                logging("database thread[%ld]: Attempt connect to %s\n", syscall(SYS_gettid), stConfigServer.db_name);
                if (dpiConn_create(*gContext,
                                    stConfigServer.db_user, strlen(stConfigServer.db_user),
                                    stConfigServer.db_pass, strlen(stConfigServer.db_pass),
                                    stConfigServer.db_name, strlen(stConfigServer.db_name),
                                    NULL, NULL, connection) < 0)
                {
                    db_log_error(*gContext, "Unable to create connection");
                    dpiContext_destroy(*gContext);
                    *gContext = NULL;
                    *connection = NULL;
                }
            }   // if ( !gContext )
		}   // if( *connection == NULL )

	}	// if(connect)
	else {	// disconnect from database

		if( *connection ) {
            dpiConn_release(*connection);
			*connection = NULL;
            if( *gContext ){
                dpiContext_destroy(*gContext);
                *gContext = NULL;
            }
		}

	}

	return(connect ? (*connection != NULL) : 1);
}   // db_connect
//------------------------------------------------------------------------------

/*
   write_data_to_db:
   record encoded gps/glonass terminal message to database
   params:
   connection - database connection
   msg - pointer to ST_RECORD structure
   return 1 if success or 0 if error
*/
static int write_data_to_db(dpiConn *connection, dpiContext *gContext, char *msg, char *sql_insert_point)
{
    int retval = 0;
    struct tm tm_data;
    char tmp[SIZE_TRACKER_FIELD];

    dpiData intDDATA,           // :1    DDATA
            intNTIME,           // :2    NTIME
            strCID,             // :3    CID
            intNNUM,            // :4    NNUM
            strCLATITUDE,       // :5    CLATITUDE
            strCNS,             // :6    CNS
            strCLONGTITUDE,     // :7    CLONGTITUDE
            strCEW,             // :8    CEW
            strCCURSE,          // :9    CCURSE
            strCSPEED,          // :10   CSPEED
            strCFUEL,           // :11   CFUEL
            strCDATAVALID,      // :12   CDATAVALID
            strCNAPR,           // :13   CNAPR
            strCBAT,            // :14   CBAT
            strCTEMPER,         // :15   CTEMPER
            strCZAJ,            // :16   CZAJ
            strCSATEL,          // :17   CSATEL
            strCPROBEG,         // :18   CPROBEG
            strCIN0,            // :19   CIN0
            strCIN1,            // :20   CIN1
            strCIN2,            // :21   CIN2
            strCIN3,            // :22   CIN3
            strCIN4,            // :23   CIN4
            strCIN5,            // :24   CIN5
            strCIN6,            // :25   CIN6
            strCIN7;            // :26   CIN7

	dpiStmt *stmt = NULL;
	ST_RECORD *record;

	if( !connection || !msg || !sql_insert_point )
		return 0;

    // prepare insert statement for execution
    if ( dpiConn_prepareStmt(connection, 0, sql_insert_point, strlen(sql_insert_point), NULL, 0, &stmt) < 0 ){
        db_log_error(gContext, "dpiConn_prepareStmt");
		return 0;
    }

	record = (ST_RECORD *)msg;

    // :1    DDATA
    // time_t -> struct tm, see also: localtime_r
    gmtime_r(&record->data, &tm_data);
    // https://oracle.github.io/odpi/doc/functions/dpiData.html
    // https://github.com/oracle/odpi/blob/main/src/dpiOracleType.c
    dpiData_setTimestamp(&intDDATA,
                            tm_data.tm_year + 1900, tm_data.tm_mon + 1, tm_data.tm_mday,
                            tm_data.tm_hour, tm_data.tm_min, tm_data.tm_sec,
                            0, 0, 0);
    if (dpiStmt_bindValueByPos(stmt, 1, DPI_NATIVE_TYPE_TIMESTAMP, &intDDATA) < 0){
        db_log_error(gContext, "DDATA");
  		goto the_end;
    }

    // :2    NTIME
    dpiData_setInt64(&intNTIME, record->time);
    if (dpiStmt_bindValueByPos(stmt, 2, DPI_NATIVE_TYPE_INT64, &intNTIME) < 0){
        db_log_error(gContext, "NTIME");
  		goto the_end;
    }

    // :3   CID
    dpiData_setBytes(&strCID, record->imei, strlen(record->imei));
    if (dpiStmt_bindValueByPos(stmt, 3, DPI_NATIVE_TYPE_BYTES, &strCID) < 0){
        db_log_error(gContext, "CID");
  		goto the_end;
    }

    // :4    NNUM
    dpiData_setInt64(&intNNUM, record->recnum);
    if (dpiStmt_bindValueByPos(stmt, 4, DPI_NATIVE_TYPE_INT64, &intNNUM) < 0){
        db_log_error(gContext, "NNUM");
  		goto the_end;
    }

    // :5 10    CLATITUDE
    snprintf(tmp, 11, "%03.07lf", record->lat);
    dpiData_setBytes(&strCLATITUDE, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 5, DPI_NATIVE_TYPE_BYTES, &strCLATITUDE) < 0){
        db_log_error(gContext, "CLATITUDE");
  		goto the_end;
    }

    // :6 1+1    CNS
    snprintf(tmp, 2, "%c", record->clat);
    dpiData_setBytes(&strCNS, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 6, DPI_NATIVE_TYPE_BYTES, &strCNS) < 0){
        db_log_error(gContext, "CNS");
  		goto the_end;
    }

    snprintf(tmp, 11, "%03.07lf", record->lon);     // :7 10+1    CLONGTITUDE
    dpiData_setBytes(&strCLONGTITUDE, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 7, DPI_NATIVE_TYPE_BYTES, &strCLONGTITUDE) < 0){
        db_log_error(gContext, "CLONGTITUDE");
  		goto the_end;
    }

    snprintf(tmp, 2, "%c", record->clon);                      // :8 1+1    CEW
    dpiData_setBytes(&strCEW, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 8, DPI_NATIVE_TYPE_BYTES, &strCEW) < 0){
        db_log_error(gContext, "CEW");
  		goto the_end;
    }

    snprintf(tmp, 4, "%d", record->curs);                 // :9 3+1    CCURSE
    dpiData_setBytes(&strCCURSE, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 9, DPI_NATIVE_TYPE_BYTES, &strCCURSE) < 0){
        db_log_error(gContext, "CCURSE");
  		goto the_end;
    }

    snprintf(tmp, 4, "%03.0lf", record->speed);                 // :10 3+1   CSPEED
    dpiData_setBytes(&strCSPEED, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 10, DPI_NATIVE_TYPE_BYTES, &strCSPEED) < 0){
        db_log_error(gContext, "CSPEED");
  		goto the_end;
    }

    snprintf(tmp, 4, "%d", record->fuel[0]);                 // :11 3+1   CFUEL
    dpiData_setBytes(&strCFUEL, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 11, DPI_NATIVE_TYPE_BYTES, &strCFUEL) < 0){
        db_log_error(gContext, "CFUEL");
  		goto the_end;
    }

    if( record->valid ) {
        snprintf(tmp, 2, "%c", 'V');               // :12 1+1   CDATAVALID
    }
    else {
        snprintf(tmp, 2, "%c", ' ');               // :12 1+1   CDATAVALID
    }
    dpiData_setBytes(&strCDATAVALID, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 12, DPI_NATIVE_TYPE_BYTES, &strCDATAVALID) < 0){
        db_log_error(gContext, "CDATAVALID");
  		goto the_end;
    }

    snprintf(tmp, 4, "%02.0lf", record->vbort);                  // :13 3+1   CNAPR
    dpiData_setBytes(&strCNAPR, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 13, DPI_NATIVE_TYPE_BYTES, &strCNAPR) < 0){
        db_log_error(gContext, "CNAPR");
  		goto the_end;
    }

    snprintf(tmp, 4, "%02.0lf", record->vbatt);                   // :14 3+1   CBAT
    dpiData_setBytes(&strCBAT, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 14, DPI_NATIVE_TYPE_BYTES, &strCBAT) < 0){
        db_log_error(gContext, "CBAT");
  		goto the_end;
    }

    snprintf(tmp, 4, "%d", record->temperature);                // :15 3+1   CTEMPER
    dpiData_setBytes(&strCTEMPER, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 15, DPI_NATIVE_TYPE_BYTES, &strCTEMPER) < 0){
        db_log_error(gContext, "CTEMPER");
  		goto the_end;
    }

    snprintf(tmp, 2, "%d", record->zaj);                     // :16 1+1   CZAJ
    dpiData_setBytes(&strCZAJ, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 16, DPI_NATIVE_TYPE_BYTES, &strCZAJ) < 0){
        db_log_error(gContext, "CZAJ");
  		goto the_end;
    }

    snprintf(tmp, 3, "%d", record->satellites);                  // :17 2+1   CSATEL
    dpiData_setBytes(&strCSATEL, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 17, DPI_NATIVE_TYPE_BYTES, &strCSATEL) < 0){
        db_log_error(gContext, "CSATEL");
  		goto the_end;
    }

    snprintf(tmp, 11, "%04.0lf", record->probeg);         // :18 10+1   CPROBEG
    dpiData_setBytes(&strCPROBEG, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 18, DPI_NATIVE_TYPE_BYTES, &strCPROBEG) < 0){
        db_log_error(gContext, "CPROBEG");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :19 10+1   CIN0
    dpiData_setBytes(&strCIN0, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 19, DPI_NATIVE_TYPE_BYTES, &strCIN0) < 0){
        db_log_error(gContext, "CIN0");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :20 10+1   CIN1
    dpiData_setBytes(&strCIN1, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 20, DPI_NATIVE_TYPE_BYTES, &strCIN1) < 0){
        db_log_error(gContext, "CIN1");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :21 10+1   CIN2
    dpiData_setBytes(&strCIN2, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 21, DPI_NATIVE_TYPE_BYTES, &strCIN2) < 0){
        db_log_error(gContext, "CIN2");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :22 10+1   CIN3
    dpiData_setBytes(&strCIN3, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 22, DPI_NATIVE_TYPE_BYTES, &strCIN3) < 0){
        db_log_error(gContext, "CIN3");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :23 10+1   CIN4
    dpiData_setBytes(&strCIN4, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 23, DPI_NATIVE_TYPE_BYTES, &strCIN4) < 0){
        db_log_error(gContext, "CIN4");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :24 10+1   CIN5
    dpiData_setBytes(&strCIN5, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 24, DPI_NATIVE_TYPE_BYTES, &strCIN5) < 0){
        db_log_error(gContext, "CIN5");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :25 10+1   CIN6
    dpiData_setBytes(&strCIN6, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 25, DPI_NATIVE_TYPE_BYTES, &strCIN6) < 0){
        db_log_error(gContext, "CIN6");
  		goto the_end;
    }

    snprintf(tmp, 11, "%d", record->ainputs[0]);            // :26 10+1   CIN7
    dpiData_setBytes(&strCIN7, tmp, strlen(tmp));
    if (dpiStmt_bindValueByPos(stmt, 26, DPI_NATIVE_TYPE_BYTES, &strCIN7) < 0){
        db_log_error(gContext, "CIN7");
  		goto the_end;
    }


    // insert
    // https://oracle.github.io/odpi/doc/functions/dpiStmt.html
    if ( dpiStmt_execute(stmt, DPI_MODE_EXEC_DEFAULT, NULL) == DPI_SUCCESS ){
        // https://oracle.github.io/odpi/doc/functions/dpiConn.html
        if ( dpiConn_commit(connection) == DPI_SUCCESS ){
            retval = 1;
        }
        else {
            db_log_error(gContext, "dpiConn_commit");
        }
    }
    else {
        db_log_error(gContext, "dpiStmt_execute");
    }

    the_end:
    dpiStmt_release(stmt);

    return retval;
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
    static __thread dpiContext *gContext = NULL;
	static __thread dpiConn *db_connection = NULL;
	static __thread char sql_insert_point[MAX_SQL_SIZE];	// text of inserting sql
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
					if( db_connection )
						write_data_to_db(db_connection, gContext, msg_buf, sql_insert_point);
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
		db_connect(0, &db_connection, &gContext);

		logging("database thread[%ld] destroyed\n", syscall(SYS_gettid));
	}   // exit_db

	// install error handler:
	pthread_cleanup_push(exit_db, arg);

	// load insert sql from file, temporary using msg_buf
	memset(msg_buf, 0, SOCKET_BUF_SIZE);
	snprintf(msg_buf, SOCKET_BUF_SIZE, "%.4075s/%.15s.sql", stParams.start_path, stConfigServer.db_type);
	if( !load_file(msg_buf, sql_insert_point, MAX_SQL_SIZE) || !strlen(sql_insert_point) ) {
		exit_db(arg);
		return NULL;
	}

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
	if( !db_connect(2, &db_connection, &gContext) ) {
		logging("database thread[%ld]: Can't connect to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
	} else {
		logging("database thread[%ld]: Connected to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
	}

    // wait messages
	while( 1 ) {
		pthread_testcancel();

        if( db_connection ) {
			msg_size = mq_receive(queue_workers, msg_buf, buf_size, NULL);
			if( msg_size > 0 )
				write_data_to_db(db_connection, gContext, msg_buf, sql_insert_point);	// write message to database
		} else {
			sleep(3);	// wait
			if( db_connect(2, &db_connection, &gContext) )	// try again
				logging("database thread[%ld]: Connect to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
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
    static __thread dpiContext *gContext = NULL;
	static __thread dpiConn *db_connection = NULL;
	static __thread dpiStmt *stmt = NULL;

	// eror handler:
	void exit_timerfunc(void * arg) {
		if( db_connection )
			db_connect(0, &db_connection, &gContext);

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
	name = strrchr(st_timer->script_path, '/') + 1;
	logging("timer[%ld]: %s: start\n", syscall(SYS_gettid), name);

	semaphore = sem_open(name, O_CREAT | O_EXCL, O_RDWR, 0);	// create named semaphore

	if( semaphore != SEM_FAILED ) {	// if semaphore not exists, continue

		if( !load_file(st_timer->script_path, sql, MAX_SQL_SIZE) || !strlen(sql) ) {
        	logging("timer[%ld]: %s: script loading error, exit\n", syscall(SYS_gettid), name);
			exit_timerfunc(ptr);
			return NULL;
		}
      	//logging("timer[%ld]: sql=%s\n", syscall(SYS_gettid), sql);

		if( !db_connect(2, &db_connection, &gContext) ) {
        	logging("timer[%ld]: %s: failed database connection, exit\n", syscall(SYS_gettid), name);
			exit_timerfunc(ptr);
			return NULL;
		}

        // https://github.com/oracle/odpi/blob/main/samples/DemoCallProc.c

        // prepare statement for execution
        if( !stmt ){
            if ( dpiConn_prepareStmt(db_connection, 0, sql, strlen(sql), NULL, 0, &stmt) < 0 ){
                db_log_error(gContext, "timer: dpiConn_prepareStmt");
    			exit_timerfunc(ptr);
    			return NULL;
            }
        }

        // execute statement
        if ( dpiStmt_execute(stmt, DPI_MODE_EXEC_DEFAULT, NULL) < 0 ){
            db_log_error(gContext, "timer: dpiStmt_execute");
        }
        else {
    		logging("timer[%ld]: %s: complete\n", syscall(SYS_gettid), name);
        }

        dpiStmt_release(stmt);

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
Table for store gps/glonass terminals data (tgpsdata):

CREATE TABLE DISPATCHER.TGPSDATA
(
    DSYSDATA    DATE         DEFAULT SYSDATE NOT NULL,
    DDATA       DATE         NOT NULL,
    NTIME       NUMBER(6)    DEFAULT 0 NOT NULL,
    CTIME       VARCHAR2(8)      NULL,
    CID         VARCHAR2(15) DEFAULT '000000000000000' NOT NULL,
    CIP         VARCHAR2(15)     NULL,
    NNUM        NUMBER(5)        NULL,
    CLATITUDE   VARCHAR2(10)     NULL,
    CNS         VARCHAR2(1)      NULL,
    CLONGTITUDE VARCHAR2(10)     NULL,
    CEW         VARCHAR2(1)      NULL,
    CCURSE      VARCHAR2(3)      NULL,
    CSPEED      VARCHAR2(3)      NULL,
    CFUEL       VARCHAR2(3)      NULL,
    CFLAG       VARCHAR2(2)      NULL,
    CFLAGS      VARCHAR2(2)      NULL,
    CFLAGP      VARCHAR2(2)      NULL,
    CDATAVALID  VARCHAR2(1)      NULL,
    CNAPR       VARCHAR2(3)      NULL,
    CBAT        VARCHAR2(3)      NULL,
    CTEMPER     VARCHAR2(3)      NULL,
    CGSM        VARCHAR2(3)      NULL,
    COPER       VARCHAR2(1)      NULL,
    CZAJ        VARCHAR2(1)      NULL,
    CPLOMBA     VARCHAR2(1)      NULL,
    CSATEL      VARCHAR2(2)      NULL,
    CANT        VARCHAR2(1)      NULL,
    CPROBEG     VARCHAR2(10)     NULL,
    CIN0        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN1        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN2        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN3        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN4        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN5        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN6        VARCHAR2(10) DEFAULT '0'     NULL,
    CIN7        VARCHAR2(10) DEFAULT '0'     NULL,
    CRESERV     VARCHAR2(3)      NULL,
    CRESERV1    VARCHAR2(3)      NULL,
    CRESERV2    VARCHAR2(3)      NULL,
    CCAR        VARCHAR2(10) DEFAULT '0' NOT NULL,
    NPROBEG     NUMBER(10,2)     NULL
)
*/

/*
SQL-script for insert record to table

INSERT INTO DISPATCHER.TGPSDATA (
    DDATA,
    NTIME,
    CID,
    NNUM,
    CLATITUDE,
    CNS,
    CLONGTITUDE,
    CEW,
    CCURSE,
    CSPEED,
    CFUEL,
    CDATAVALID,
    CNAPR,
    CBAT,
    CTEMPER,
    CZAJ,
    CSATEL,
    CPROBEG,
    CIN0,
    CIN1,
    CIN2,
    CIN3,
    CIN4,
    CIN5,
    CIN6,
    CIN7
) VALUES (
    :1,  -- DDATA,
    :2,  -- NTIME,
    :3,  -- CID,
    :4,  -- NNUM,
    :5,  -- CLATITUDE,
    :6,  -- CNS,
    :7,  -- CLONGTITUDE,
    :8,  -- CEW,
    :9,  -- CCURSE,
    :10,  -- CSPEED,
    :11,  -- CFUEL,
    :12,  -- CDATAVALID,
    :13,  -- CNAPR,
    :14,  -- CBAT,
    :15,  -- CTEMPER,
    :16,  -- CZAJ,
    :17,  -- CSATEL,
    :18,  -- CPROBEG,
    :19,  -- CIN0,
    :20,  -- CIN1,
    :21,  -- CIN2,
    :22,  -- CIN3,
    :23,  -- CIN4,
    :24,  -- CIN5,
    :25,  -- CIN6,
    :26  -- CIN7,
);
*/