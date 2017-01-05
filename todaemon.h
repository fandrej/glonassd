#ifndef __TODAEMON__
#define __TODAEMON__
#include <mqueue.h>

void toDaemon(const char *PidFilePath);
int ConfigureSignalHandlers(void);
void FatalSigHandler(int sig);
void TermHandler(int sig);
void HupHandler(int sig);
void Usr1Handler(int sig);
void ErrorHandler(int sig, siginfo_t *si, void *ptr);

#endif
