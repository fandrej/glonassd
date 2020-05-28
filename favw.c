/*
   favw.c
   shared library for decode/encode gps/glonass terminal "Tracker for WinCE" messages

   help:
   see Tracker for WinCE project

   compile:
   make -B favw
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include "glonassd.h"
#include "worker.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"


/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{
	ST_RECORD *record = NULL;
	char *cPart, cTime[10], cDate[10], cValid;
	int iTemp, rec_ok;
	struct tm tm_data;
	time_t ulliTmp;

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	//  0        1          2   3     4     5      6     7  8   9    10  11 12 13
	// "*123456789012345,090524,A,5527.6548,N,06520.3713,E,0.3,351,160115,7,2,104,0,1.2,0.3,0040,0.5*123456789012345,090525,A,5527.6547,N,06520.3712,E,0.3,351,160115,6,2,104*123456789012345,090526,A,5527.6547,N,06520.3712,E,0.0,351,160115,6,2,104*123456789012345,090528,A,5527.6547,N,06520.3712,E,0.0,351,160115,6,2,104*123456789012345,090529,A,5527.6547,N,06520.3714,E,0.1,351,160115,6,2,104*123456789012345,090530,A,5527.6545,N,06520.3713,E,0.0,351,160115,6,2,104*123456789012345,090531,A,5527.6544,N,06520.3713,E,0.3,351,160115,6,2,104*123456789012345,090532,A,5527.6544,N,06520.3713,E,0.0,351,160115,7,2,104*123456789012345,090534,A,5527.6544,N,06520.3713,E,0.0,351,160115,5,4,104"
	cPart = strtok(&parcel[1], "*");

	rec_ok = 1; // первую запись всегда создаем
	while( cPart ) {

		if( rec_ok ) {
			if( answer->count < MAX_RECORDS - 1 )
				answer->count++;
			record = &answer->records[answer->count - 1];
			rec_ok = 0;
		}	// if( rec_ok )

		//          1    2  3   4  5   6  7  8   9  10 11 12 13 14 15 16 17 18
		iTemp = sscanf(cPart, "%15s,%6s,%c,%lf,%c,%lf,%c,%lf,%d,%6s,%d,%d,%d,%d,%d,%d,%d,%d",
							record->imei, // 1
							cTime, // 2
							&cValid, // 3
							&record->lat, // 4
							&record->clat, // 5
							&record->lon, // 6
							&record->clon, // 7
							&record->speed, // 8
							&record->curs, // 9
							cDate, // 10
							&record->satellites, // 11
							&record->hdop, // 12
							&record->height, // 13
							&record->ainputs[3], // 14
							&record->ainputs[4], // 15
							&record->ainputs[5], // 16
							&record->ainputs[6], // 17
							&record->ainputs[7]	// 18
						  );

		if(iTemp >= 13 && iTemp <= 18) {

			snprintf(record->tracker, SIZE_TRACKER_FIELD, "FAVW");
			snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
			snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 1);

			// переводим время GMT и текстовом формате в местное
			memset(&tm_data, 0, sizeof(struct tm));
			sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
			tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
			sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);

			ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
			gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
			// получаем время как число секунд от начала суток
			record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
			// в tm_data обнуляем время
			tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
			// получаем дату
			record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

			iTemp = 0.01 * record->lon;
			record->lon = (record->lon - iTemp * 100) / 60.0 + iTemp;

			iTemp = 0.01 * record->lat;
			record->lat = (record->lat - iTemp * 100) / 60.0 + iTemp;

			// Скорость в километрах
			if( record->speed > 5 )	// мили
				record->speed = Round(MILE * record->speed, 0);	// > 10 км/ч
			else
				record->speed = Round(MILE * record->speed, 1);

			record->valid = (cValid == 'A') && record->lon > 0.0 && record->lat > 0.0;

			rec_ok++;
		}	// if(iTemp == 13 || iTemp == 18)

		cPart = strtok(NULL, "*");
	}	// while( cPart )

}
//------------------------------------------------------------------------------


/* encode function
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
