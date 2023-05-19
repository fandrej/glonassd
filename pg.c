/*
   pg.c
   PostgreSQL library:
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
   http://citforum.ru/programming/unix/threads/
   http://citforum.ru/programming/unix/threads_2/
   http://man7.org/linux/man-pages/man7/sem_overview.7.html
	 http://zetcode.com/db/postgresqlc/
   http://www.postgresql.org/docs/9.4/static/libpq-build.html
	(http://www.linux.org.ru/forum/general/11736854)
   http://www.postgresql.org/docs/9.4/static/libpq-connect.html
   http://www.postgresql.org/docs/9.4/static/libpq-exec.html
   https://www.postgresql.org/docs/9.4/static/libpq-example.html
   https://www.postgresql.org/docs/current/static/libpq-example.html
   http://docs.huihoo.com/redhat/database/rhdb-1.3/prog/libpq-exec.html
   http://zetcode.com/db/postgresqlc/
   http://linux.die.net/man/7/mq_overview
   http://www.redov.ru/kompyutery_i_internet/unix_vzaimodeistvie_processov/p3.php#metkadoc75
   http://rjaan.narod.ru/docs/using_sql-types_with_c-apps.html
   http://www.postgresonline.com/journal/archives/3-Converting-from-Unix-Timestamp-to-PostgreSQL-Timestamp-or-Date.html
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
#include <libpq-fe.h>
#include "glonassd.h"
#include "de.h"
#include "logger.h"

// Definitions
#define MAX_SQL_SIZE 4096
#define INSERT_PARAMS_COUNT 34

// Locals
// params for inserting sql
static __thread char *paramValues[INSERT_PARAMS_COUNT]= {
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 5
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 10
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 15
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 20
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 25
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1,   // 30
	(char*)1,
	(char*)1,
	(char*)1,
	(char*)1    // 34
};

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

/*
   db_connect:
   connection to / disconnection from database
   connect - flag of connect (1) or disconnect (0)
   connection - pointer to PGconn *
   return 1 if success or 0 if error
*/
static int db_connect(int connect, PGconn **connection)
{
	char conninfo[FILENAME_MAX];
	PGresult *db_result;
	ExecStatusType resultStatus;

	if(connect) {	// connecting to database

		if( *connection == NULL ) {
			memset(conninfo, 0, FILENAME_MAX);
			snprintf(conninfo, FILENAME_MAX,
						"host=%s port=%d dbname=%s user=%s password=%s connect_timeout=%d application_name=glonassd sslmode=disable",
						stConfigServer.db_host,
						stConfigServer.db_port,
						stConfigServer.db_name,
						stConfigServer.db_user,
						stConfigServer.db_pass,
						5);	// connect_timeout

			*connection = PQconnectdb(conninfo);
			// PQconnectdb use "malloc" internally, leak memory when called from thread
		}
        else {
			PQreset(*connection);
		}

		if( PQstatus(*connection) == CONNECTION_OK ) {

			if(stConfigServer.db_schema && strlen(stConfigServer.db_schema) ) {

				snprintf(conninfo, FILENAME_MAX, "set search_path to %s;", stConfigServer.db_schema);

				db_result = PQexec(*connection, conninfo);
				resultStatus = PQresultStatus(db_result);
				if( resultStatus != PGRES_COMMAND_OK )
					logging("database thread[%ld]: PQexec(%s): %s\n", syscall(SYS_gettid), conninfo, PQerrorMessage(*connection));
				PQclear(db_result);
			}

		}	// if( PQstatus(*connection) == CONNECTION_OK )
		else
			logging("database thread[%ld]: PQconnectdb(%s): %s\n", syscall(SYS_gettid), conninfo, PQerrorMessage(*connection));

	}	// if(connect)
	else {	// disconnect from database

		if( *connection ) {
			PQfinish(*connection);
			*connection = NULL;
		}

	}

	return(connect ? (PQstatus(*connection) == CONNECTION_OK) : 1);
}
//------------------------------------------------------------------------------

/*
   write_data_to_db:
   record encoded gps/glonass terminal message to database
   connection - database connection
   msg - pointer to ST_RECORD structure
   return 1 if success or 0 if error
*/
static int write_data_to_db(PGconn *connection, char *msg, char *sql_insert_point)
{
	PGresult *res;
	ExecStatusType pqstatus;
	ST_RECORD *record;

	if( !connection || !msg || !sql_insert_point )
		return 0;

	record = (ST_RECORD *)msg;
	snprintf(paramValues[0], SIZE_TRACKER_FIELD, "%lld", (long long)record->data); // $1
	snprintf(paramValues[1], SIZE_TRACKER_FIELD, "%d", record->time);
	snprintf(paramValues[2], SIZE_TRACKER_FIELD, "%s", record->imei);			   // $3
	snprintf(paramValues[3], SIZE_TRACKER_FIELD, "%d", record->status);
	snprintf(paramValues[4], SIZE_TRACKER_FIELD, "%03.07lf", record->lon);         // $5
	snprintf(paramValues[5], SIZE_TRACKER_FIELD, "%c", record->clon);
	snprintf(paramValues[6], SIZE_TRACKER_FIELD, "%03.07lf", record->lat);         // $7
	snprintf(paramValues[7], SIZE_TRACKER_FIELD, "%c", record->clat);
	snprintf(paramValues[8], SIZE_TRACKER_FIELD, "%d", record->height);            // $9
	snprintf(paramValues[9], SIZE_TRACKER_FIELD, "%03.01lf", record->speed);
	snprintf(paramValues[10], SIZE_TRACKER_FIELD, "%d", record->curs);             // $11
	snprintf(paramValues[11], SIZE_TRACKER_FIELD, "%d", record->satellites);
	snprintf(paramValues[12], SIZE_TRACKER_FIELD, "%d", record->valid);            // $13
	snprintf(paramValues[13], SIZE_TRACKER_FIELD, "%d", record->recnum);
	snprintf(paramValues[14], SIZE_TRACKER_FIELD, "%02.01lf", record->vbort);      // $15
	snprintf(paramValues[15], SIZE_TRACKER_FIELD, "%02.01lf", record->vbatt);
	snprintf(paramValues[16], SIZE_TRACKER_FIELD, "%d", record->temperature);      // $17
	snprintf(paramValues[17], SIZE_TRACKER_FIELD, "%d", record->hdop);
	snprintf(paramValues[18], SIZE_TRACKER_FIELD, "%u", record->outputs);          // $19
	snprintf(paramValues[19], SIZE_TRACKER_FIELD, "%u", record->inputs);
	snprintf(paramValues[20], SIZE_TRACKER_FIELD, "%u", record->ainputs[0]);       // $21
	snprintf(paramValues[21], SIZE_TRACKER_FIELD, "%u", record->ainputs[1]);
	snprintf(paramValues[22], SIZE_TRACKER_FIELD, "%u", record->ainputs[2]);       // $23
	snprintf(paramValues[23], SIZE_TRACKER_FIELD, "%u", record->ainputs[3]);
	snprintf(paramValues[24], SIZE_TRACKER_FIELD, "%u", record->ainputs[4]);       // $25
	snprintf(paramValues[25], SIZE_TRACKER_FIELD, "%u", record->ainputs[5]);
	snprintf(paramValues[26], SIZE_TRACKER_FIELD, "%u", record->ainputs[6]);       // $27
	snprintf(paramValues[27], SIZE_TRACKER_FIELD, "%u", record->ainputs[7]);
	snprintf(paramValues[28], SIZE_TRACKER_FIELD, "%d", record->fuel[0]);          // $29
	snprintf(paramValues[29], SIZE_TRACKER_FIELD, "%d", record->fuel[1]);
	snprintf(paramValues[30], SIZE_TRACKER_FIELD, "%04.03lf", record->probeg);     // $31
	snprintf(paramValues[31], SIZE_TRACKER_FIELD, "%d", record->zaj);
	snprintf(paramValues[32], SIZE_TRACKER_FIELD, "%d", record->alarm);            // $33
	snprintf(paramValues[33], SIZE_MESSAGE_FIELD, "%s", record->message);		   // $34

	res = PQexecParams(connection,          // PGconn *conn,
                        sql_insert_point,      // const char *command,
                        INSERT_PARAMS_COUNT,   // int nParams,
                        NULL,                  // const Oid *paramTypes
                        (const char* const*)paramValues,
                        NULL,                  // const int *paramLengths,
                        NULL,                  // const int *paramFormats,
                        1);                    // int resultFormat: 1-ask for binary results

	pqstatus = PQresultStatus(res);
	PQclear(res);

	if( pqstatus == PGRES_COMMAND_OK )
		return 1;
	else {
		logging("database thread[%ld]: PQexecParams() error: %s\n", syscall(SYS_gettid), PQerrorMessage(connection));
		return 0;
	}
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
	static __thread PGconn *db_connection = NULL;
	static __thread char sql_insert_point[MAX_SQL_SIZE];	// text of inserting sql
	static __thread char values[INSERT_PARAMS_COUNT * SIZE_TRACKER_FIELD];	// buffer for parameters values for sql
	static __thread char msg_buf[SOCKET_BUF_SIZE];
	static __thread mqd_t queue_workers = -1;	// Posix IPC queue of messages from workers
	static __thread struct mq_attr queue_attr;
	static __thread struct rlimit rlim;
	static __thread ssize_t msg_size;
	static __thread size_t buf_size;
	static __thread int i;

	// error handler:
	void exit_db(void * arg) {

		// destroy queue
		if( queue_workers != -1 ) {
			// save messages from queue
			if( mq_getattr(queue_workers, &queue_attr) == 0 && queue_attr.mq_curmsgs > 0 ) {
				logging("database thread writing %ld messages\n", queue_attr.mq_curmsgs);

				while( (msg_size = mq_receive(queue_workers, msg_buf, buf_size, NULL)) > 0 ) {
					if( PQstatus(db_connection) == CONNECTION_OK )
						write_data_to_db(db_connection, msg_buf, sql_insert_point);
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
		db_connect(0, &db_connection);

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

	// initialise sql-parameters pointers
	for(i = 0; i < INSERT_PARAMS_COUNT; i++)
		paramValues[i] = values + (i * SIZE_TRACKER_FIELD);

	logging("database thread[%ld] started, queue size %ld msgs\n", syscall(SYS_gettid), (long)queue_attr.mq_maxmsg);

	// try to connect to database
	if( !db_connect(2, &db_connection) ) {
		logging("database thread[%ld]: Can't connect to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
	} else {
		logging("database thread[%ld]: Connected to database %s on host %s:%d.", syscall(SYS_gettid), stConfigServer.db_name, stConfigServer.db_host, stConfigServer.db_port);
	}

	// wait messages
	while( 1 ) {
		pthread_testcancel();

		if( PQstatus(db_connection) == CONNECTION_OK ) {
			msg_size = mq_receive(queue_workers, msg_buf, buf_size, NULL);
			if( msg_size > 0 )
				write_data_to_db(db_connection, msg_buf, sql_insert_point);	// write message to database
		} else {
			sleep(3);	// wait
			if( db_connect(2, &db_connection) )	// try again
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
	static __thread PGconn *db_connection = 0;
	static __thread PGresult *sql_result = 0;
	static __thread ExecStatusType resultStatus = 0;

	// eror handler:
	void exit_timerfunc(void * arg) {
		if( sql_result )
			PQclear(sql_result);

		if( db_connection )
			db_connect(0, &db_connection);

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

		if( !db_connect(2, &db_connection) ) {
        	logging("timer[%ld]: %s: failed database connection, exit\n", syscall(SYS_gettid), name);
			exit_timerfunc(ptr);
			return NULL;
		}

		sql_result = PQexec(db_connection, sql);
		resultStatus = PQresultStatus(sql_result);

		switch(resultStatus) {
		case PGRES_COMMAND_OK:	// INSERT or UPDATE without a RETURNING clause, etc.
		case PGRES_TUPLES_OK:   // retrieve the rows returned by the query (SELECT)
		case PGRES_SINGLE_TUPLE:// same as above
		case PGRES_COPY_OUT:    // Copy Out (from server) data transfer started
		case PGRES_COPY_IN:     // Copy In (to server) data transfer started
			logging("timer[%ld]: %s: result %s\n", syscall(SYS_gettid), name, PQresStatus(resultStatus));
			break;
		default:
			logging("timer[%ld]: %s: result %s, error: %s\n", syscall(SYS_gettid), name, PQresStatus(resultStatus), PQresultErrorMessage(sql_result));
		}

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

CREATE TABLE tgpsdata (
    dsysdata timestamp without time zone DEFAULT ('now'::text)::timestamp without time zone,
    ddata timestamp without time zone,
    ntime integer,
    cimei character varying(15),
    nstatus integer,
    nlongitude double precision,
    cew character varying(1) DEFAULT 'E'::character varying,
    nlatitude double precision,
    cns character varying(1) DEFAULT 'N'::character varying,
    naltitude real,
    nspeed real,
    nheading integer,
    nsat integer,
    nvalid integer,
    nnum integer,
    nvbort real,
    nvbat real,
    ntmp real,
    nhdop real,
    nout integer,
    ninp integer,
    nin0 real,
    nin1 real,
    nin2 real,
    nin3 real,
    nin4 real,
    nin5 real,
    nin6 real,
    nin7 real,
    nfuel1 real,
    nfuel2 real,
    nprobeg real,
    nzaj integer,
    nalarm integer,
    nprobegc real
);

COMMENT ON TABLE tgpsdata IS 'Данные GPS';
COMMENT ON COLUMN tgpsdata.dsysdata IS 'Системная (серверная) дата записи';
COMMENT ON COLUMN tgpsdata.ddata IS 'Дата GPS';
COMMENT ON COLUMN tgpsdata.ntime IS 'Время GPS в секундах от начала суток';
COMMENT ON COLUMN tgpsdata.cimei IS 'IMEI или ID устройства';
COMMENT ON COLUMN tgpsdata.nstatus IS 'Состояние устройства GPS';
COMMENT ON COLUMN tgpsdata.nlongitude IS 'Долгота в долях градусов';
COMMENT ON COLUMN tgpsdata.cew IS 'Флаг долготы E/W';
COMMENT ON COLUMN tgpsdata.nlatitude IS 'Широта в долях градусов';
COMMENT ON COLUMN tgpsdata.cns IS 'Флаг широты N/S';
COMMENT ON COLUMN tgpsdata.naltitude IS 'Высота, метры';
COMMENT ON COLUMN tgpsdata.nspeed IS 'Скорость, км/ч (1миля = 1.852 km)';
COMMENT ON COLUMN tgpsdata.nheading IS 'Направление движения (азимут)';
COMMENT ON COLUMN tgpsdata.nsat IS 'Кол-во спутников';
COMMENT ON COLUMN tgpsdata.nvalid IS '0-подозрительная запись, 1-норма';
COMMENT ON COLUMN tgpsdata.nnum IS 'Порядковый № пакета GPS';
COMMENT ON COLUMN tgpsdata.nvbort IS 'Напряжение бортовое';
COMMENT ON COLUMN tgpsdata.nvbat IS 'Напряжение батареи устройства';
COMMENT ON COLUMN tgpsdata.ntmp IS 'Температура устройства';
COMMENT ON COLUMN tgpsdata.nhdop IS 'HDOP';
COMMENT ON COLUMN tgpsdata.nout IS 'Битовое поле состояния управляющих контактов (выходов)';
COMMENT ON COLUMN tgpsdata.ninp IS 'Битовое поле состояния датчиков (входов)';
COMMENT ON COLUMN tgpsdata.nin0 IS 'Состояние датчика (входа) № 1';
COMMENT ON COLUMN tgpsdata.nfuel1 IS 'Показания датчика уровня топлива № 1';
COMMENT ON COLUMN tgpsdata.nprobeg IS 'Пробег, расчитанный терминалом';
COMMENT ON COLUMN tgpsdata.nzaj IS 'Состояние зажигания';
COMMENT ON COLUMN tgpsdata.nalarm IS 'Состояние кнопки тревоги';
COMMENT ON COLUMN tgpsdata.nprobegc IS 'Пробег, расчитанный сервером, метры';

CREATE INDEX igpsdata ON tgpsdata USING btree (ddata, ntime, cimei, nvalid);
*/

/*
SQL-script for insert record to table

INSERT INTO gps.tgpsdata (
	ddata ,          --$1
	ntime ,
	cimei ,          --$3
	nstatus ,
	nlongitude ,     --$5
	cew ,
	nlatitude ,      --$7
	cns ,
	naltitude ,      --$9
	nspeed ,
	nheading ,       --$11
	nsat ,
	nvalid ,         --$13
	nnum ,
	nvbort ,         --$15
	nvbat ,
	ntmp ,           --$17
	nhdop ,
	nout ,           --$19
	ninp ,
	nin0 ,           --$21
	nin1 ,
	nin2 ,           --$23
	nin3 ,
	nin4 ,           --$25
	nin5 ,
	nin6 ,           --$27
	nin7 ,
	nfuel1 ,         --$29
	nfuel2 ,
	nprobeg ,        --$31
	nzaj ,
	nalarm           --$33
) VALUES (
	to_timestamp($1::bigint),
	$2::integer,
	$3::varchar,
	$4::integer,
	$5::double precision,
	$6::varchar,
	$7::double precision,
	$8::varchar,
	$9::real,
	$10::real,
	$11::integer,
	$12::integer,
	$13::integer,
	$14::integer,
	$15::real,
	$16::real,
	$17::real,
	$18::real,
	$19::integer,
	$20::integer,
	$21::real,
	$22::real,
	$23::real,
	$24::real,
	$25::real,
	$26::real,
	$27::real,
	$28::real,
	$29::real,
	$30::real,
	$31::real,
	$32::integer,
	$33::integer
);
*/
