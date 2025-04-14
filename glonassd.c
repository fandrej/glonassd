/*
    glonassd.c
    main file for project

    help:
    http://pyviy.blogspot.ru/2010/12/gcc.html
    http://cpp.com.ru/shildt_spr_po_c/
    https://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Thread_002dLocal.html#Thread_002dLocal
    https://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Option-Summary.html#Option-Summary
    https://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/C-Extensions.html#C-Extensions
    http://www.ibm.com/developerworks/ru/library/os_lang_c_details_01/index.html
    http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html
    http://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux
    http://www.catb.org/esr/cookbook/helloserver.c
    http://ru.vingrad.com/Ogranichennoye-kolichestvo-podklyucheny-po-soketu-id51fb9c726ccc196813000002/relations
    http://www.ibm.com/developerworks/library/l-memory-leaks/index.html
    http://digitalchip.ru/osobennosti-ispolzovaniya-extern-i-static-v-c-c

    compile:
    cd /home/work/gcc/glonassd
    make -B all

        Note: if error "/usr/bin/ld: cannot find -lpq" occured, run: apt-get install libpq-dev

    start (one of variants):
    ./glonassd start
    /etc/init.d/glonassd.sh start
    service glonassd start

    stop (one of variants):
    ./glonassd stop
    /etc/init.d/glonassd.sh stop
    service glonassd stop

        Autostart configure
        Edit DAEMON variable in glonassd.sh file for correct path to daemon folder.
        Copy glonassd.sh file in /etc/init.d folder.
        Use chmod 0755 /etc/init.d/glonassd.sh for make it executable.
        Use systemctl daemon-reload and update-rc.d glonassd.sh defaults for enable autostart daemon.
        Use update-rc.d -f glonassd.sh remove for diasble autostart without delete glonassd.sh file.
        Delete /etc/init.d/glonassd.sh file and use systemctl daemon-reload for fully cleanup daemon info.

    see logs:
    cat /var/log/glonassd.log
    grep glonassd /var/log/syslog

    see processes:
    ps -xj | grep glonassd

    see open ports:
    netstat -lptun | grep 401

    see memory usage:
    pmap <pid>
    cat /proc/<pid>/smaps > smaps.txt

    debugging memory leaks:

    1. using valgrind:
    /opt/valgrind/bin/valgrind --leak-check=yes --track-origins=yes --show-leak-kinds=all ./glonassd start
    (Compile your program with -g to include debugging information so that Memcheck's error messages include exact line numbers)
    (see: http://valgrind.org/docs/manual/quick-start.html#quick-start.intro
    http://developerweb.net/viewtopic.php?id=5895)

    2. native:
    a) calculate threads stacks count:
    pmap PID | grep 256 | wc -l

    b) calculate running threads count:
    ls /proc/PID/task | wc -l

    c) if threads stacks count greater running threads count and continue increase then we has memory leak

    remove pid-file after crash:
    rm /var/run/glonassd.pid
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h> /* getcwd */
#include <dlfcn.h>	/* dlopen */
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "glonassd.h"
#include "todaemon.h"
#include "worker.h"
#include "forwarder.h"
#include "logger.h"
#include "lib.h"

// globals
#define THREAD_STACK_SIZE_KB	(512)

const char *const gPidFilePath = "/var/run/glonassd.pid";
int graceful_stop, reconfigure;     // flags
ST_PARAMS stParams;	                // startup params
ST_CONFIG_SERVER stConfigServer;	// main config
ST_LISTENERS stListeners;		    // listeners
ST_FORWARDERS stForwarders;	        // forwarders
void (*timer_function_pointer)(union sigval) = NULL;  // timer routine address
long GMT_diff = 0;	// difference between local time & GMT time
pthread_attr_t worker_thread_attr;	// thread attributes
int attr_init = 0;                  // flag: 0 - thread attributes initialized, != 0 - not initialized

// locals
static void *db_library_handle = NULL;
static pthread_t db_thread = 0;
static pthread_t log_thread = 0;
static struct pollfd *pollset = NULL;	// pull of the listener's sockets
static int pollcnt = 0;	// number of the polled sockets

// functions
extern int loadConfig(char *cPathToFile);	// loadconfig.c
static int parceParams(int argc, char* argv[]);
static int setup(char *config_path);
static void usage(void);
static int listeners_start();
static int listeners_stop();
static int forwarders_start();
static int forwarders_stop();
static int database_setup(unsigned int start);
static int timers_start();
static int timers_stop();

// parcing command-line parameters
static int parceParams(int argc, char* argv[])
{
    unsigned int i;
    char szTmp[32], *p;

    memset(&stParams, 0, sizeof(ST_PARAMS));

    // full start-path:
    sprintf(szTmp, "/proc/%d/exe", getpid());
    if( readlink(szTmp, stParams.start_path, FILENAME_MAX) > 0 ) {
        p = strrchr(stParams.start_path, '/');
        if(p)
            *p = 0;
    }
    else if( !getcwd(stParams.start_path, FILENAME_MAX) ){
    	strncpy(stParams.start_path, argv[0],  FILENAME_MAX);
        p = strrchr(stParams.start_path, '/');
        if(p)
            *p = 0;
    }

    for(i=1; i<argc; i++) {
        if( strcmp("-c", argv[i]) == 0 ) {
            sprintf(stParams.config_path, "%s", argv[++i]);
        }
        if( strcmp("-d", argv[i]) == 0 ) {
            stParams.daemon = 1;
        }
        else if( strcmp("start", argv[i]) == 0 ||
                   strcmp("stop", argv[i]) == 0 ||
                   strcmp("restart", argv[i]) == 0 ) {
            stParams.cmd = argv[i];
        }
    }	// for(i=0; i<argc; i++)

    if( !strlen(stParams.config_path) )
        sprintf(stParams.config_path, "%s/%s", stParams.start_path, CONFIG_DEFAULT);

    if( stParams.cmd )
        i = 1;
    else
        i = 0;

    return(i);
}
//------------------------------------------------------------------------------

/*
    database threads start/stop rutine & database timer function pointer initialize
    start - 1-start, 0-stop database thread
*/
static int database_setup(unsigned int start)
{
    void *(*db_thread_func)(void *); // pointer to database thread function
    char *cerror, lib_path[FILENAME_MAX];
    int thread_ok;

    if( start ) {

        memset(lib_path, 0, FILENAME_MAX);
        snprintf(lib_path, FILENAME_MAX, "%.4060s/%.30s.so", stParams.start_path, stConfigServer.db_type);

        db_library_handle = dlopen(lib_path, RTLD_LAZY);
        if( !db_library_handle ) {
            logging("database_setup: dlopen(%s) error: %s\n", lib_path, dlerror());
            return 0;
        }

        // get pointer to database thread function
        dlerror();	// Clear any existing error
        db_thread_func = dlsym(db_library_handle, "db_thread");
        cerror = dlerror();
        if( cerror != NULL ) {
            logging("database_setup: dlsym(\"db_thread\") error: %s\n", cerror);
            database_setup(0);
            return 0;
        }

        // get pointer to timer thread function
        timer_function_pointer = dlsym(db_library_handle, "timer_function");
        cerror = dlerror();
        if( cerror != NULL ) {
            timer_function_pointer = NULL;
            logging("database_setup: dlsym(\"timer_function\") error: %s\n", cerror);
        }

        // start database thread
        if( attr_init )
            thread_ok = pthread_create(&db_thread, &worker_thread_attr, db_thread_func, &stConfigServer);
        else
            thread_ok = pthread_create(&db_thread, NULL, db_thread_func, &stConfigServer);

        if( thread_ok ) {	// error
            logging("database_setup: pthread_create error %d: %s\n", errno, strerror(errno));
            database_setup(0);
            return 0;
        }

        // return database thread working status
        return ( pthread_tryjoin_np(db_thread, NULL) == EBUSY );

    }	// if( start )
    else {

        // stop database thread
        if( db_thread ) {
            pthread_cancel(db_thread);
            pthread_join(db_thread, NULL);
            db_thread = 0;
        }

        // unload library
        if( db_library_handle ) {
            dlclose(db_library_handle);
            db_library_handle = NULL;
        }

    } // else if( start )

    return 1;
}
//------------------------------------------------------------------------------


// work preparing
static int setup(char *config_path)
{
    struct timespec waittime;
    int thread_error;

    memset(&waittime, 0, sizeof(struct timespec));
    memset(&stListeners, 0, sizeof(ST_LISTENERS));
    memset(&stForwarders, 0, sizeof(ST_FORWARDERS));

    // load settings
    if( !loadConfig(config_path) ) {
        syslog(LOG_NOTICE, "Can't load config file %s\n", stParams.config_path);
        if( !stParams.daemon ) printf("Can't load config file %s\n", stParams.config_path);
        return 0;
    }

    // start logging thread
    if( attr_init )
        thread_error = pthread_create(&log_thread, &worker_thread_attr, log_thread_func, NULL);
    else
        thread_error = pthread_create(&log_thread, NULL, log_thread_func, NULL);

    if( thread_error ) {	// error
        syslog(LOG_NOTICE, "Logger start error %d: %s\n", errno, strerror(errno));
        syslog(LOG_NOTICE, "logging to syslog\n");
        if( !stParams.daemon ) printf("Logger start error %d: %s\n", errno, strerror(errno));
        if( !stParams.daemon ) printf("logging to syslog\n");
    }

    // wait for complete start thread
    waittime.tv_sec = 1;
    pthread_timedjoin_np(log_thread, NULL, &waittime);

    return database_setup(1);
}
//------------------------------------------------------------------------------

// stopping & free resources
int cleanup(void)
{
    timers_stop();
    listeners_stop();
    forwarders_stop();

    if( stListeners.listener )
        free(stListeners.listener);

    if( stForwarders.forwarder )
        free(stForwarders.forwarder);

    database_setup(0);

    // stop logger
    if( log_thread ) {
        pthread_cancel(log_thread);
        pthread_join(log_thread, NULL);
        log_thread = 0;
    }

    return 1;
}
//------------------------------------------------------------------------------

// process start/restart/stop command
void command(const char *pidfile, const char *cmd)
{
    int pid = 0;
    unsigned int started = 0;
    FILE *handle = fopen(pidfile, "r");

    if(handle ) {
        if( fscanf(handle, "%d", &pid) && pid > 0 ) {
            if( kill(pid, 0) == 0 )
                started = 1;
        }
        fclose(handle);

        if( pid > 0 && !started && errno == ESRCH ) {
            printf("Found PID file %s with PID %d without process glonassd(%d)\nPID file will be deleted.\n", pidfile, pid, pid);
            unlink(gPidFilePath);
        }
    }	// if(handle)

    if( strcmp(cmd, "start") == 0 ) {
        if( started ) {
            printf("glonassd already started.\n");
            exit(EXIT_FAILURE);
        }
        if( stParams.daemon )
            printf("Start glonassd as daemon.\nUse 'grep glonassd /var/log/syslog' command to see the result\n");
        else
            printf("Start glonassd.\n");
    }	// if( strcmp(cmd, "start")
    else if( strcmp(cmd, "stop") == 0 ) {
        if( started ) {
            if( kill(pid, SIGUSR1) != 0 ) {
                printf("glonassd: kill(%d, SIGUSR1) error %d: %s\n", (int)pid, errno, strerror(errno));
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        } else {
            printf("glonassd not started.\n");
            exit(EXIT_FAILURE);
        }
    }	// if( strcmp(cmd, "stop")
    else if( strcmp(cmd, "restart") == 0 ) {
        if( started ) {
            if( kill(pid, SIGHUP) != 0 ) {
                printf("glonassd: kill(%d, SIGHUP) error %d: %s\n", (int)pid, errno, strerror(errno));
                exit(EXIT_FAILURE);
            }
            printf("glonassd restarting.\n");
            exit(EXIT_SUCCESS);
        } else {
            printf("glonassd not started.\n");
            exit(EXIT_FAILURE);
        }
    }	// if( strcmp(cmd, "restart")
    else {
        usage();
        exit(EXIT_FAILURE);
    }
}
//------------------------------------------------------------------------------

// usage display
static void usage(void)
{
    printf("\nglonassd v 1.0\n");
    printf("Fedorov Andrey 2016\n");
    printf("Usage: ./glonassd start|stop|restart [-c path_to_config_file] [-d]\n");
    printf("where:\n");
    printf("-c: full path to config file (glonassd.conf in current directory by default)\n");
    printf("-d: start in daemon mode\n\n");
}
//------------------------------------------------------------------------------

/*
    load terminals protocols shared library
    protocol - terminal protocol name (ST_LISTENER.name / ST_FORWARDER.app)
    lib_handle - pointer to tpointer to library handle
    f_decode - pointer to pointer to decode function
    f_encode - pointer to pointer to encode function
*/
static int library_load(char *protocol, void **lib_handle, void **f_decode, void **f_encode)
{
    char lib_path[FILENAME_MAX], *cerror;

    // load external library by name
    memset(lib_path, 0, FILENAME_MAX);
    snprintf(lib_path, FILENAME_MAX, "%.4060s/%.30s.so", stParams.start_path, protocol);

    *lib_handle = dlopen(lib_path, RTLD_LAZY);
    if( *lib_handle == NULL ) {
        cerror = dlerror();
        logging("shared library %s: dlopen(%s) error: %s\n", protocol, lib_path, cerror);
        return 0;
    }

    *f_decode = dlsym(*lib_handle, "terminal_decode");
    cerror = dlerror();
    if( cerror != NULL ) {
        logging("shared library %s: dlsym(\"terminal_decode\") error: %s\n", protocol, cerror);
        dlclose(*lib_handle);
        *lib_handle = NULL;
        return 0;
    }

    *f_encode = dlsym(*lib_handle, "terminal_encode");
    cerror = dlerror();
    if( cerror != NULL ) {
        logging("shared library %s: dlsym(\"terminal_encode\") error: %s\n", protocol, cerror);
    }

    return 1;
}
//------------------------------------------------------------------------------

// startup listeners
static int listeners_start()
{
    unsigned int i;
    struct sockaddr_in in_addr;

    // preventive clear pollfd structure
    pollcnt = 0;
    if( pollset ) {
        free(pollset);
        pollset = NULL;
    }

    if( !stListeners.count ) {
        logging("No configured listeners\n");
        return 0;
    }

    // iterate listeners
    for(i = 0; i < stListeners.count; i++) {

        // start service if enabled
        if( stListeners.listener[i].enabled ) {

            logging("listener[%s] port=%d protocol=%s attempt to start\n", stListeners.listener[i].name, stListeners.listener[i].port, (stListeners.listener[i].protocol == SOCK_STREAM ? "TCP" : "UDP"));

            // load library for listener's worker
            if( library_load(stListeners.listener[i].name, &stListeners.listener[i].library_handle, (void*)&stListeners.listener[i].terminal_decode, (void*)&stListeners.listener[i].terminal_encode) ) {

                // create listener socket
                stListeners.listener[i].socket = socket(AF_INET, stListeners.listener[i].protocol, 0);
                if( stListeners.listener[i].socket < 0 ) {
                    logging("listener[%s]: socket() error %d: %s\n", stListeners.listener[i].name, errno, strerror(errno));
                    continue;	// next listener
                }

                /*
                    After listener stop, if client connected and hold connect,
                    socket switch to TIME_WAIT mode and restart listener with
                    bind() raise error: "Address already in use" (errno = 98)
                    Block error with: SO_REUSEADDR & SO_REUSEPORT
                */
                if (setsockopt(stListeners.listener[i].socket, SOL_SOCKET, SO_REUSEADDR, &(int) {1}, sizeof(int)) < 0)
                    logging("listener[%s]: setsockopt(SO_REUSEADDR) error %d: %s\n", stListeners.listener[i].name, errno, strerror(errno));

#ifdef SO_REUSEPORT
                if (setsockopt(stListeners.listener[i].socket, SOL_SOCKET, SO_REUSEPORT, &(int) {1}, sizeof(int)) < 0)
                    logging("listener %s: setsockopt(SO_REUSEPORT) error %d: %s\n", stListeners.listener[i].name, errno, strerror(errno));
#endif

                // bind socket to address & port
                memset(&in_addr, 0, sizeof(struct sockaddr_in));
                in_addr.sin_family = AF_INET;
                inet_aton(stConfigServer.listen, &in_addr.sin_addr);
                in_addr.sin_port = htons(stListeners.listener[i].port);

                if( bind(stListeners.listener[i].socket, (struct sockaddr *)&in_addr, sizeof(struct sockaddr_in)) < 0 ) {
                    logging("listener[%s]: bind() error %d: %s\n", stListeners.listener[i].name, errno, strerror(errno));
                    close(stListeners.listener[i].socket);
                    stListeners.listener[i].socket = BAD_OBJ;
                    continue;
                }

                // listen terminals, second param. - listener queue size
                if( listen(stListeners.listener[i].socket, stConfigServer.socket_queue) < 0 ) {
                    logging("listener[%s]: listen() error %d: %s\n", stListeners.listener[i].name, errno, strerror(errno));
                    close(stListeners.listener[i].socket);
                    stListeners.listener[i].socket = BAD_OBJ;
                    continue;
                }

                ++pollcnt;	// number of started listeners (and polled sockets)

                // set up pollfd structure
                pollset = (struct pollfd *)realloc(pollset, pollcnt * sizeof(struct pollfd));
                pollset[pollcnt - 1].fd = stListeners.listener[i].socket;
                pollset[pollcnt - 1].events = POLLIN;
                pollset[pollcnt - 1].revents = 0;	// filled by the kernel

                logging("listener[%s] started on port %d\n", stListeners.listener[i].name, stListeners.listener[i].port);
            }	// if( library_load(

        }	// if( stListeners.listener[i].enabled )

    }	// for(i = 0; i < stListeners.count; i++)

    return pollcnt;
}
//------------------------------------------------------------------------------

// stopping listeners
static int listeners_stop()
{
    unsigned int i;

    // iterate listeners
    for(i = 0; i < stListeners.count; i++) {
        if( stListeners.listener[i].socket != BAD_OBJ ) {
            shutdown(stListeners.listener[i].socket, SHUT_RDWR);
            close(stListeners.listener[i].socket);
            logging("listener[%s] on port %d stopped\n", stListeners.listener[i].name, stListeners.listener[i].port);
        }

        if( stListeners.listener[i].library_handle )
            dlclose(stListeners.listener[i].library_handle);
    }	// for(i=0; i < stListeners.count; i++)

    // clear pollfd structure
    pollcnt = 0;
    if( pollset ) {
        free(pollset);
        pollset = NULL;
    }

    return 1;
}
//------------------------------------------------------------------------------

// startup forwarders
static int forwarders_start()
{
    unsigned int i = 0, cnt = 0;
    int thread_ok;
    long name_max, len;

    // iterate forwarders
    for(i = 0; i < stForwarders.count; i++) {

        if( !stForwarders.forwarder[i].app || !strlen(stForwarders.forwarder[i].app) ){
            logging("forwarder[%s] has error in parametes, skipped\n", stForwarders.forwarder[i].name);
            continue;
        }

        logging("forwarder[%s] attempt to start\n", stForwarders.forwarder[i].name);

        // load library for encode/decode functions
        if( library_load(stForwarders.forwarder[i].app, &stForwarders.forwarder[i].library_handle, (void*)&stForwarders.forwarder[i].terminal_decode, (void*)&stForwarders.forwarder[i].terminal_encode) ) {

            // open saved files directory
            stForwarders.forwarder[i].data_dir = opendir(stConfigServer.forward_files);	// use malloc internally
            name_max = pathconf(stConfigServer.forward_files, _PC_NAME_MAX);
            if (name_max == -1)         /* Limit not defined, or error */
                name_max = FILENAME_MAX;         /* Take a guess */
            len = offsetof(struct dirent, d_name) + name_max + 1;

            // start forwarder in separate thread, passing point to his config (last parameter)
            if( attr_init )
                thread_ok = pthread_create(&stForwarders.forwarder[i].thread, &worker_thread_attr, forwarder_thread, &stForwarders.forwarder[i]);
            else
                thread_ok = pthread_create(&stForwarders.forwarder[i].thread, NULL, forwarder_thread, &stForwarders.forwarder[i]);

            if( thread_ok )	// error
                logging("forwarder[%s]: error %d: %s\n", stForwarders.forwarder[i].name, errno, strerror(errno));
            else
                ++cnt;

        }	// if( library_load

    }	// for(i = 0; i < stForwarders.count; i++)

    return( cnt == stForwarders.count );
}
//------------------------------------------------------------------------------

// stopping forwarders
static int forwarders_stop()
{
    unsigned int i;

    // iterate forwarders
    for(i = 0; i < stForwarders.count; i++) {

        // stop forwarder if worked
        if( stForwarders.forwarder[i].thread ) {
            if( pthread_cancel(stForwarders.forwarder[i].thread) )
                logging("cancel forwarder[%s] error %d: %s\n", stForwarders.forwarder[i].name, errno, strerror(errno));

            if( pthread_join(stForwarders.forwarder[i].thread, NULL) )
                logging("stop forwarder[%s] error %d: %s\n", stForwarders.forwarder[i].name, errno, strerror(errno));

            if( stForwarders.forwarder[i].library_handle )
                dlclose(stForwarders.forwarder[i].library_handle);

        }	// if( stForwarders.forwarder[i].thread )

    }	// for(i=0; i<stForwarders.count; i++)
    stForwarders.count = 0;

    // clear list of the forwarding terminals
    if( stForwarders.terminals ) {
        free(stForwarders.terminals);
        stForwarders.terminals = NULL;
    }
    stForwarders.listcount = 0;

    return 1;
}
//------------------------------------------------------------------------------

int timers_stop()
{
    unsigned int i, e = 0;

    for(i = 0; i < TIMERS_MAX; i++) {
        if( stConfigServer.timers[i].id ) {
            timer_delete(stConfigServer.timers[i].id);
            stConfigServer.timers[i].id = 0;
            e++;
        }
    }	// for(i = 0; i < TIMERS_MAX; i++)

    if( e )
        logging("%d timers stopped\n", e);

    return 1;
}
//------------------------------------------------------------------------------

int timers_start()
{
    unsigned int curtime, i, e = 0;
    struct sigevent se;
    struct itimerspec its;
    struct tm local;
    time_t t;

    if( !timer_function_pointer ) { // see function database_setup
        logging("timers_start: timer function not exists\n");
        return 0;
    }

    memset(&se, 0, sizeof(struct sigevent));
    se.sigev_notify = SIGEV_THREAD;	// Upon timer expiration, invoke sigev_notify_function as if it were the start function of a new thread
    se.sigev_notify_function = timer_function_pointer;	// thread function, see pg.c (timer_function)

    // thread attributes
    if( attr_init )
        se.sigev_notify_attributes = &worker_thread_attr;
    else
        se.sigev_notify_attributes = NULL;

    for(i = 0; i < TIMERS_MAX; i++) {
        if( strlen(stConfigServer.timers[i].script_path) ) {

            // pass pointer to timer structure to thread function
            se.sigev_value.sival_ptr = &stConfigServer.timers[i];

            /* Create the timer */
            if( !timer_create(CLOCK_REALTIME, &se, &stConfigServer.timers[i].id) ) {

                memset(&its, 0, sizeof(struct itimerspec));
                // first start time
                if( stConfigServer.timers[i].start != -1 ) {	// time exists
                    t = time(NULL);
                    localtime_r(&t, &local);
                    curtime = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;	// current time in sec. from 00:00:00 of current day

                    if( curtime > stConfigServer.timers[i].start ) {
                        its.it_value.tv_sec = 24 * 3600 - (curtime - stConfigServer.timers[i].start);
                    } else if( curtime < stConfigServer.timers[i].start ) {
                        its.it_value.tv_sec = stConfigServer.timers[i].start - curtime;
                    } else {	// start immediality!
                        its.it_value.tv_sec = 1;
                    }
                } else {	// periodical
                    its.it_value.tv_sec = stConfigServer.timers[i].period;
                }

                // period interval
                its.it_interval.tv_sec = stConfigServer.timers[i].period;

                // Start the timer
                if( timer_settime(stConfigServer.timers[i].id, 0, &its, NULL) ) {
                    logging("timers_start: timer_settime error %d: %s\n", errno, strerror(errno));
                    timer_delete(stConfigServer.timers[i].id);
                    stConfigServer.timers[i].id = 0;
                }   // if( timer_settime(
                else {
                    ++e;
                }
            } else {
                logging("timers_start: timer_create error %d: %s\n", errno, strerror(errno));
            }

        }	// if( strlen(stConfigServer.timers[i].script_path) )
    }	// for(i = 0; i < TIMERS_MAX; i++)

    if( e )
        logging("%d timers started\n", e);

    return 1;
}
//------------------------------------------------------------------------------

/* get difference between local time & gmt/utc time in seconds */
static long gettimediffwithgmt(void)
{
    struct tm tm_local, tm_gmt;
    time_t tl;

    tl = time(NULL);
    localtime_r(&tl, &tm_local);

    gmtime_r(&tl, &tm_gmt);

    return(mktime(&tm_local) - mktime(&tm_gmt));
}
//------------------------------------------------------------------------------

// CTRL+C handler
void  INThandler(int sig)
{
    signal(sig, SIG_IGN);
    printf("\nYou hit Ctrl-C, quit\n");
    graceful_stop = 1;
}

// main function
int main(int argc, char* argv[])
{
    int thread_error, nfds = BAD_OBJ, exit_code = EXIT_SUCCESS;
    unsigned int i, j, k = 0;
    socklen_t sockaddr_in_size = sizeof(struct sockaddr_in);
    ST_WORKER *worker_config;
    FILE *handle;
    struct rlimit rlim;

    // parse command string
    if( !parceParams(argc, argv) ) {
        usage();
        exit(EXIT_FAILURE);
    }

    // calculate difference between local time & gmt time in seconds
    GMT_diff = gettimediffwithgmt();

    // process start/restart/stop command
    command(gPidFilePath, stParams.cmd);

    if( stParams.daemon ) {
        toDaemon(gPidFilePath); // force programm to daemon
        syslog(LOG_NOTICE, "glonassd[%d] started\n", (int)getpid());
    }
    else {
        // install CTRL+C handler
        signal(SIGINT, INThandler);
        printf("glonassd[%d] started\n", (int)getpid());
    }

    // create pid file
    handle = fopen(gPidFilePath, "w");
    if(handle) {
        fprintf(handle, "%d\n", (int)getpid());
        fclose(handle);
    }
    else {
        fprintf(stderr, "Create PID file %s, error %d: %s\n", gPidFilePath, errno, strerror(errno));
        if( stParams.daemon )
            syslog(LOG_NOTICE, "Create PID file %s, error %d: %s\n", gPidFilePath, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    graceful_stop = 0;      // flag "stop programm"
    reconfigure = 1;        // flag "read config"
    /*
        Daemon-specific initialization done
    */

    // initialise thread attributes
    attr_init = (0 == pthread_attr_init(&worker_thread_attr)); // attr_init = 1 if successfull
    if( attr_init ) {
        // set stack size for threads
        if( pthread_attr_setstacksize(&worker_thread_attr, 1024 * THREAD_STACK_SIZE_KB) ) {
            // error, use default stack size
            attr_init = 0;
            pthread_attr_destroy(&worker_thread_attr);
            syslog(LOG_NOTICE, "pthread_attr_setstacksize(%d) error %d: %s\n", 1024 * THREAD_STACK_SIZE_KB, errno, strerror(errno));
        }	// if( pthread_attr_setstacksize
    }	// if( attr_init )

    // increase RLIMIT_MSGQUEUE, root only
    logging("glonassd[%d]: ST_RECORD size = %lld", (int)getpid(), sizeof(ST_RECORD));
    if( getrlimit(RLIMIT_MSGQUEUE, &rlim) == 0 )
        logging("glonassd[%d]: current limits: rlim.rlim_cur= %lld, rlim.rlim_max= %lld", (int)getpid(), rlim.rlim_cur, rlim.rlim_max);

    rlim.rlim_cur = rlim.rlim_max = 10 * 65536 * sizeof(ST_RECORD);
    if( setrlimit(RLIMIT_MSGQUEUE, &rlim) != 0 )
        logging("glonassd[%d]: setrlimit error %d, %s", (int)getpid(), errno, strerror(errno));

    if( getrlimit(RLIMIT_MSGQUEUE, &rlim) == 0 )
        logging("glonassd[%d]: new limits: rlim.rlim_cur= %lld, rlim.rlim_max= %lld", (int)getpid(), rlim.rlim_cur, rlim.rlim_max);

    /*
        The Main Loop
    */
    while( !graceful_stop ) {

        if( reconfigure ) {     // signal SIGHUP (see todaemon.c)
            cleanup();          // do first
            reconfigure = 0;    // do second

            if( setup(stParams.config_path) && listeners_start() ) {
                timers_start();
                forwarders_start();
            } else {
                exit_code = EXIT_FAILURE;
                break;
            }
        }	// if( reconfigure )

        // wait listeners
        nfds = poll(pollset, pollcnt, -1);  // wait infinity

        switch(nfds) {
        case -1:	// poll() error (socket close or SIGNAL)

            if( reconfigure || graceful_stop ) {    // signals (see todaemon.c)
                // do nothing,
                // reconfigure handled before this,
                // stop cycle if graceful_stop > 0
            } else {
                // real error or signal for stop
                graceful_stop = 1;
                exit_code = EXIT_FAILURE;
                logging("glonassd[%d]: poll() error %d: %s\n", (int)getpid(), errno, strerror(errno));
            }

            break;
        case 0:	// timeout

            // this case newer fired becouse we infinity waiting

            break;
        default:	// nfds = number of structures which have nonzero revents fields

            for(i = 0; i < pollcnt; i++) {	// scan fired sockets

                if( pollset[i].revents ) {

                    for(j = 0; j < stListeners.count; j++) {	// scan listeners

                        // search fired socket
                        if( stListeners.listener[j].socket == pollset[i].fd ) {

                            // create worker config structure: MUST be free in worker.c
                            worker_config = (ST_WORKER *)malloc(sizeof(ST_WORKER));
                            memset(worker_config, 0, sizeof(ST_WORKER));

                            // assept connection
                            worker_config->client_socket = accept(stListeners.listener[j].socket, (struct sockaddr *)&worker_config->client_addr, &sockaddr_in_size);
                            if( worker_config->client_socket < 0 ) {
                                free(worker_config);
                                logging("glonassd[%d]: listener[%s] accept() error %d: %s\n", (int)getpid(), stListeners.listener[j].name, errno, strerror(errno));
                            } else {
                                // set settings for worker
                                worker_config->listener = &stListeners.listener[j];

                                // start worker thread
                                if( attr_init )
                                    thread_error = pthread_create(&worker_config->thread, &worker_thread_attr, worker_thread, worker_config);
                                else
                                    thread_error = pthread_create(&worker_config->thread, NULL, worker_thread, worker_config);

                                if( thread_error ) {   // error :(
                                    free(worker_config);
                                    logging("glonassd[%d]: listener[%s] pthread_create() error %d: %s\n", (int)getpid(), stListeners.listener[j].name, errno, strerror(errno));
                                }	// if( pthread_create(
                                else {
                                    if( pthread_detach(worker_config->thread) )
                                        logging("glonassd[%d]: listener[%s] pthread_detach(%lld) error %d: %s\n", (int)getpid(), stListeners.listener[j].name, worker_config->thread, errno, strerror(errno));
                                }
                            }	// else if( worker_config->client_socket < 0 )

                            break;	// fired socket located and treated, break search
                        }	// if( stListeners.listener[j].socket == pollset[i].fd )

                    }	// for(j = 0;

                    if( ++k == nfds )	// if all fired sockets treated
                        break;          // stop scan
                }	// if( pollset[i].revents )

            }	// for(i = 0;

        }	// switch(nfds)

    }	// while( !graceful_stop )

    /*
        graceful cleanup
    */
    logging("glonassd[%d] stopped, exit_code=%d\n", (int)getpid(), exit_code);
    cleanup();
    syslog(LOG_NOTICE, "glonassd[%d] stopped, exit_code=%d\n", (int)getpid(), exit_code);
    printf("glonassd[%d] stopped, exit_code=%d\n", (int)getpid(), exit_code);

    if( stParams.daemon )
        closelog(); // opened in todaemon.c

    if( attr_init )
        pthread_attr_destroy(&worker_thread_attr);

    unlink(gPidFilePath);

    exit(exit_code);
}
//------------------------------------------------------------------------------
