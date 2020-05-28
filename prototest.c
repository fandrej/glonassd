/*
   prototest.c
   shared library for unknown terminal
   write raw terminal messages to file without encode

   compile:
   make -B prototest
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include <stdio.h> 	/* fopen */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>	/* write */
#include "de.h"
#include "lib.h"        // MIN, MAX, BETWEEN, CRC, etc...
#include "glonassd.h"   // stParams
#include "worker.h"
#include "logger.h"

/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{
	int fHandle;
	char fName[FILENAME_MAX];
	int iTemp;
	time_t t;
	struct tm local;

	if( parcel && parcel_size ) {

		// путь к файлу - каталог запуска
		iTemp = snprintf(fName, FILENAME_MAX, "%s/logs/", stParams.start_path);
		// имя файла - текущее дата_время + случайное число
		t = time(NULL);
		localtime_r(&t, &local);
		snprintf(&fName[iTemp], FILENAME_MAX-iTemp, "%02d%02d%02d_%02d%02d%02d_%d",
					local.tm_mday, local.tm_mon+1, local.tm_year-100,
					local.tm_hour, local.tm_min, local.tm_sec,
					rand());

		if( (fHandle = open(fName, O_APPEND | O_CREAT | O_WRONLY, S_IWRITE)) == -1 ) {
			logging("terminal_decode[prototest]: open() error %d: %s\n", errno, strerror(errno));
		} else {
			if( !write(fHandle, parcel, parcel_size) )
				logging("terminal_decode[prototest]: write() error %d: %s\n", errno, strerror(errno));

			close(fHandle);
		}

	}	// if( parcel )

}
//------------------------------------------------------------------------------


/*
   encode function
   records - pointer to array of ST_RECORD struct.
   reccount - number of struct in array, and returning
   buffer - buffer for encoded data
   bufsize - size of buffer
   return size of data in the buffer for encoded data
*/
int terminal_encode(ST_RECORD *records, int reccount, char *buffer, int bufsize)
{
	int top = 0;
	return top;
}
//------------------------------------------------------------------------------
