/*
   nts.c
   shared library for decode/encode gps/glonass terminal NTS messages

   compile:
   make -B nts
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include "glonassd.h"
#include "de.h"     // ST_ANSWER
#include "logger.h"


/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	ST_RECORD *record;
	char cTime[10], cDate[10];
	int iTemp;
	struct tm tm_data;
	time_t ulliTmp;

	if( !parcel || parcel_size <= 4 )
		return;

	answer->size = snprintf(answer->answer, 4, "OK\r\n");

	if( answer->count < MAX_RECORDS - 1 )
		answer->count++;
	record = &answer->records[answer->count - 1];

	// &3001022564,0,1,230516,085138,06521.8057,E,5526.4879,N,81,229,06,10,0,0,0,6
	iTemp = sscanf(&parcel[1], "%10s,%d,%d,%6s,%6s,%lf,%c,%lf,%c,%d,%d,%d,%lf,%d,%d,%d,%d",
						record->imei,
						&record->status,
						&record->valid,
						cDate, cTime,
						&record->lon,
						&record->clon,
						&record->lat,
						&record->clat,
						&record->height,
						&record->curs,
						&record->satellites,
						&record->speed,
						&record->ainputs[0],   // SOS
						&record->ainputs[2],   // вызов на связь
						&record->ainputs[1],   // зажигание
						&record->ainputs[3]);  // двери

	if( iTemp != 17 ) {
		return;
	}

	snprintf(record->tracker, SIZE_TRACKER_FIELD, "NTS");
	snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
	snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 2);

	// переводим время GMT и текстовом формате в местное
	memset(&tm_data, 0, sizeof(struct tm));
	sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
	tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
	tm_data.tm_year = 2000 + tm_data.tm_year - 1900;
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
	record->lon = fmod(record->lon, 100.0) / 60.0 + iTemp;

	iTemp = 0.01 * record->lat;
	record->lat = fmod(record->lat, 100.0) / 60.0 + iTemp;

	record->zaj = record->ainputs[1];
	record->alarm = record->ainputs[0];

	memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));

}   // terminal_decode
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
