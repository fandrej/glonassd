/*
    todaemon.c
    switch process to daemon helper

    help:
    http://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux
    http://www.catb.org/esr/cookbook/helloserver.c
    http://www.ibm.com/developerworks/ru/library/l-signals_1/index.html
*/

#define _GNU_SOURCE 1  /* To pick up REG_RIP */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <execinfo.h>
#include <string.h>
#include "glonassd.h"
#include "todaemon.h"

extern void logging(char *template, ...);

void toDaemon(const char *PidFilePath)
{
	pid_t pid;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
        syslog(LOG_NOTICE, "fork() 1 error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "fork() 1 error %d: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
    }

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* On success: The child process becomes session leader */
	if (setsid() < 0) {
        syslog(LOG_NOTICE, "setsid() error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "setsid() error %d: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
    }

	/* Fork off for the second time */
	pid = fork();

	/* An error occurred */
	if (pid < 0) {
        syslog(LOG_NOTICE, "fork() 2 error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "fork() 2 error %d: %s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
    }

    /* close parent */
    if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Catch, ignore and handle signals */
	ConfigureSignalHandlers();

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	if( chdir("/") ){    // error
        syslog(LOG_NOTICE, "chdir('/'), error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "chdir('/'), error %d: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

	/* Close all open file descriptors */
	int x;
	for (x = sysconf(_SC_OPEN_MAX); x>0; x--) {
		close (x);
	}

	/*
	    stdin, stdout, stderr should be opened because standard library functions used it
	    but it should not use real standard descriptors
	*/
	int tmp = open("/dev/null", O_RDWR); /* fd 0 = stdin */
    /* fd 1 = stdout */
	if( dup(tmp) == -1 ){
        syslog(LOG_NOTICE, "dup(1), error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "dup(1), error %d: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
	}
    /* fd 2 = stderr */
	if( dup(tmp) == -1 ){
        syslog(LOG_NOTICE, "dup(2), error %d: %s\n", errno, strerror(errno));
        fprintf(stderr, "dup(2), error %d: %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
	}

	/*
	    put server into its own process group. If this process now spawns
		child processes, a signal sent to the parent will be propagated
		to the children
	*/
	setpgrp();

	/* Open the log file */
	openlog ("glonassd", LOG_PID, LOG_DAEMON);
}
//------------------------------------------------------------------------------

int ConfigureSignalHandlers(void)
{
	struct sigaction sighupSA, sigusr1SA, sigtermSA, sigERR;//, saUSR2;

	/*
	    ignore several signals because they do not concern us. In a
		production server, SIGPIPE would have to be handled as this
		is raised when attempting to write to a socket that has
		been closed or has gone away (for example if the client has
		crashed). SIGURG is used to handle out-of-band data. SIGIO
		is used to handle asynchronous I/O. SIGCHLD is very important
		if the server has forked any child processes.
	*/

	signal(SIGUSR2, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGURG, SIG_IGN);
	signal(SIGXCPU, SIG_IGN);
	signal(SIGXFSZ, SIG_IGN);
	signal(SIGVTALRM, SIG_IGN);
	signal(SIGPROF, SIG_IGN);
	signal(SIGIO, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	/*
	    these signals mainly indicate fault conditions and should be logged.
		Note we catch SIGCONT, which is used for a type of job control that
		is usually inapplicable to a daemon process. We don't do anyting to
		SIGSTOP since this signal can't be caught or ignored. SIGEMT is not
		supported under Linux as of kernel v2.4
	*/

	signal(SIGQUIT, FatalSigHandler);
	signal(SIGTRAP, FatalSigHandler);
	signal(SIGABRT, FatalSigHandler);
	signal(SIGIOT, FatalSigHandler);
#ifdef SIGEMT /* this is not defined under Linux */
	signal(SIGEMT, FatalSigHandler);
#endif
	signal(SIGSTKFLT, FatalSigHandler);
	signal(SIGCONT, FatalSigHandler);
	signal(SIGPWR, FatalSigHandler);
	signal(SIGSYS, FatalSigHandler);

	/* these handlers are important for control of the daemon process */

	/* TERM  - shut down immediately */
	sigtermSA.sa_handler=TermHandler;
	sigemptyset(&sigtermSA.sa_mask);
	sigtermSA.sa_flags=0;
	sigaction(SIGTERM, &sigtermSA, NULL);

	/*
	    USR1 - finish serving the current connections and then close down
		(graceful shutdown)
	*/
	sigusr1SA.sa_handler=Usr1Handler;
	sigemptyset(&sigusr1SA.sa_mask);
	sigusr1SA.sa_flags=0;
	sigaction(SIGUSR1, &sigusr1SA, NULL);

	/*
	    HUP - finish serving the current connections and then restart
		connections handling. This could be used to force a re-read of
		a configuration file for example
	*/
	sighupSA.sa_handler=HupHandler;
	sigemptyset(&sighupSA.sa_mask);
	sighupSA.sa_flags=0;
	sigaction(SIGHUP, &sighupSA, NULL);

	// fatal error signals
	// http://man7.org/linux/man-pages/man2/sigaction.2.html
	sigERR.sa_sigaction = ErrorHandler;	// If SA_SIGINFO is specified in sa_flags, then sa_sigaction (instead of sa_handler) specifies the signal-handling function for signum
	sigemptyset(&sigERR.sa_mask);
	sigERR.sa_flags = SA_SIGINFO;
	// install own handler
	sigaction(SIGFPE, &sigERR, 0);	// FPU error
	sigaction(SIGILL, &sigERR, 0);	// code (instruction) error
	sigaction(SIGSEGV, &sigERR, 0);	// memory error
	sigaction(SIGBUS, &sigERR, 0);	// bus memory error

	// SIGUSR2 used for info of "worker" terminated
	// http://man7.org/linux/man-pages/man2/sigaction.2.html
	/*
	    saUSR2.sa_sigaction = worker_destroy;
	    sigemptyset(&saUSR2.sa_mask);
	    saUSR2.sa_flags = SA_SIGINFO;
	    sigaction(SIGUSR2, &saUSR2, 0);
	*/

	return 0;
}
//------------------------------------------------------------------------------

void FatalSigHandler(int sig)
{
#ifdef _GNU_SOURCE
	syslog(LOG_INFO, "Caught signal: %s - exiting", strsignal(sig));
	logging("glonassd[%d]: Caught signal: %s - exiting\n", (int)getpid(), strsignal(sig));
#else
	syslog(LOG_INFO, "Caught signal: %d - exiting", sig);
	logging("glonassd[%d]: Caught signal: %d - exiting\n", (int)getpid(), sig);
#endif

	graceful_stop = 1;
}
//------------------------------------------------------------------------------

// Handler for the SIGTERM signal
void TermHandler(int sig)
{
	syslog(LOG_INFO, "Caught SIGTERM - exiting");
	logging("glonassd[%d]: Caught SIGTERM - exiting\n", (int)getpid());
	graceful_stop = 1;
}
//------------------------------------------------------------------------------

// Handler for the SIGUSR1 signal
void Usr1Handler(int sig)
{
	syslog(LOG_INFO, "Caught SIGUSR1 - stop");
	logging("glonassd[%d]: Caught SIGUSR1 - stop\n", (int)getpid());
	graceful_stop = 1;
}
//------------------------------------------------------------------------------

/*
    Handler for the SIGHUP signal
    finish serving the current connections, re-read a configuration file and then restart	connections handling
*/
void HupHandler(int sig)
{
	syslog(LOG_INFO, "Caught SIGHUP - restart");
	logging("glonassd[%d]: Caught SIGHUP - restart\n", (int)getpid());

	reconfigure = 1;    // re-read a configuration file

	return;
}
//------------------------------------------------------------------------------

/*
    error handler
    This function receives:
    the signal number as its first argument,
    a pointer to a siginfo_t as its second argument
    and a pointer to a ucontext_t (cast to void *) as its third argument
*/
void ErrorHandler(int sig, siginfo_t *si, void *ptr)
{
	void* ErrorAddr;
	void* Trace[16];
	int    x;
	int    TraceSize;
	char** Messages;
	int pid = (int)getpid();

	// log signal
#ifdef _GNU_SOURCE
	syslog(LOG_ALERT, "Process: %d Signal: %s, Addr: %p\n", si->si_pid, strsignal(sig), si->si_addr);
	logging("glonassd[%d]: Process: %d Signal: %s, Addr: %p\n", pid, si->si_pid, strsignal(sig), si->si_addr);
#else
	syslog(LOG_ALERT, "Signal: %d, Addr: %p\n", sig, si->si_addr);
	logging("glonassd[%d]: Signal: %d, Addr: %p\n", pid, sig, si->si_addr);
#endif

	// get error instruction address
#if __WORDSIZE == 64 // 64-bits OS
	ErrorAddr = (void*)((ucontext_t*)ptr)->uc_mcontext.gregs[REG_RIP];
#else
	ErrorAddr = (void*)((ucontext_t*)ptr)->uc_mcontext.gregs[REG_EIP];
#endif

	// get stack trace
	TraceSize = backtrace(Trace, 16);
	Trace[1] = ErrorAddr;

	// log stack trace
    // https://habr.com/ru/company/ispsystem/blog/144198/
	Messages = backtrace_symbols(Trace, TraceSize);
	if (Messages) {
		syslog(LOG_ALERT, "== Backtrace ==\n");
		logging("glonassd[%d]: == Backtrace ==\n", pid);

		for (x = 1; x < TraceSize; x++) {
			syslog(LOG_ALERT, "%s\n", Messages[x]);
			logging("glonassd[%d]: %s\n", pid, Messages[x]);
		}

		syslog(LOG_ALERT, "== End Backtrace ==\n");
		logging("glonassd[%d]: == End Backtrace ==\n", pid);
		free(Messages);
	}	// if (Messages)

	// stop daemon & close all
	cleanup();

	// log
	syslog(LOG_ALERT, "glonassd terminated\n");
	logging("glonassd[%d]: terminated\n", pid);

	// close logging
	closelog();

	if( attr_init )
		pthread_attr_destroy(&worker_thread_attr);

	// exit process
	exit(EXIT_FAILURE);
}
//------------------------------------------------------------------------------
