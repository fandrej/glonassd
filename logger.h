#ifndef __LOGGER__
#define __LOGGER__

#define QUEUE_LOGGER "/que_logger"
#define LOG_MSG_SIZE 512

extern void logging(char *template, ...);
void *log_thread_func(void *logmsg);

#endif
