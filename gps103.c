/*
   gps103.c
   shared library for decode/encode gps/glonass terminal GPS-101 - GPS-103 messages

   help:
   D:\Work\Документы\Трекеры\Авто\GPS101C\GPRS data protocol.xls
   http://www.javased.com/?source_dir=traccar/src/org/traccar/protocol/Gps103ProtocolDecoder.java

   compile:
   make -B gps103
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
	char *cRec, cMode[16], cDateTime[11], cTime[11], cLon, cLat, cSignal, cValid;
	double dLon, dLat, dSpeed, diftime;
	int iTemp, iAnswerSize;
	struct tm tm_data;
	time_t ulliTmp;

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	switch(parcel[0]) {
	case '#':	// Log on request: ##,imei:359586015829802,A;
		// answer: LOAD

		memset(&answer->lastpoint, 0, sizeof(ST_RECORD));

		iTemp = sscanf(parcel, "##,imei:%[^,],%*s", answer->lastpoint.imei);

		if( iTemp == 1 && strlen(answer->lastpoint.imei) ) {
			iAnswerSize = 5;	// 4 + завершающий 0
			answer->size = snprintf(answer->answer, iAnswerSize, "LOAD");
		}	// if( iTemp == 1 && strlen(answer->lastpoint.imei) )

		break;
	case '0':	// Heartbeat package: 359586015829802
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		// answer: ON

		iAnswerSize = 3;
		answer->size = snprintf(answer->answer, iAnswerSize, "ON");

		break;
	case 'i':	// data
		//             1           2         3          4      5      6     7    8      9     10    11  12
		// imei:359586015829802,tracker,0809231929,13554900601,F,112909.397,A,2234.4669,N,11354.3287,E,0.11,;
		// imei:359586015829802,move,000000000,13554900601,L,;
		// imei:359586015829802,help me,0809231429,13554900601,F,062947.294,A,2234.4026,N,11354.3277,E,0.00,;
		// imei:359586015829802,help me,000000000,13554900601,L,;

		cRec = strtok(parcel, ";");
		while( cRec ) {
			memset(cMode, 0, 16);

			//                           1     2     3           4   5    6  7   8  9  10  11
			iTemp = sscanf(cRec, "imei:%[^,],%[^,],%[^,],%*[^,],%c,%[^,],%c,%lf,%c,%lf,%c,%lf,",
								answer->lastpoint.imei,		// 1
								cMode, 			// 2 tracker, move, help me, etc...
								cDateTime, 	// 3	datetime:	YYMMDDhhmm
								&cSignal, 	// 4 F || L - full || low signal
								cTime, 			// 5 Time (HHMMSS.SSS)
								&cValid, 		// 6
								&dLat, 			// 7 latitude
								&cLat, 			// 8 N/S
								&dLon, 			// 9
								&cLon,			// 10 E/W
								&dSpeed			// 11 speed in knots
							  );
			switch(iTemp) {
			case 4:	// нет GPS сигнала (cSignal = L)
				break;
			case 11:	// есть GPS сигнал (cSignal = F)

				if( answer->count < MAX_RECORDS - 1 )
					answer->count++;
				record = &answer->records[answer->count - 1];

				snprintf(record->tracker, SIZE_TRACKER_FIELD, "GPS103");
				snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
				snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 1);
				snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", answer->lastpoint.imei);

				// переводим время GMT и текстовом формате в местное
				memset(&tm_data, 0, sizeof(struct tm));
				sscanf(cDateTime, "%2d%2d%2d%2d%2d", &tm_data.tm_year, &tm_data.tm_mon, &tm_data.tm_mday, &tm_data.tm_hour, &tm_data.tm_min);
				tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
				if( strlen(cTime) )
					sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);

				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
				gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
				// получаем время как число секунд от начала суток
				record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
				// в tm_data обнуляем время
				tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
				// получаем дату
            	record->data = timegm(&tm_data);

				iTemp = 0.01 * dLat;
				record->lat = (dLat - iTemp * 100.0) / 60.0 + iTemp;
				record->clat = cLat;

				iTemp = 0.01 * dLon;
				record->lon = (dLon - iTemp * 100.0) / 60.0 + iTemp;
				record->clon = cLon;

				record->valid = (record->lon > 0.0 && record->lat > 0.0 && cValid == 'A');

				if( dSpeed > 10.0 )
					record->speed = Round(dSpeed * MILE, 0);
				else
					record->speed = Round(dSpeed * MILE, 1);

				record->alarm = (strcmp(cMode, "help me") == 0);

				// расчитываем направление движения и пробег, если есть предыдущая отметка
				if(record->data == answer->lastpoint.data && record->time >= answer->lastpoint.time)
					diftime = record->time - answer->lastpoint.time;
				else if(record->data > answer->lastpoint.data && record->time < answer->lastpoint.time)
					diftime = record->time + 86400 - answer->lastpoint.time;
				else
					diftime = 0;

				if( answer->lastpoint.lon && answer->lastpoint.lat && record->lon && record->lat && diftime > 0 ) {
					geoDistance(answer->lastpoint.lon, answer->lastpoint.lat, record->lon, record->lat, &record->probeg, &record->curs);
				}

				if( diftime > 0 && record->lon && record->lat ) {
					memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
				}

			}	// switch(iTemp)

			cRec = strtok(NULL, ";");
		}	// while( cRec )

	}	// switch(parcel[0])

}	// terminal_decode
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
