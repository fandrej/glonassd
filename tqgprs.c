/*
   tqgprs.c
   shared library for decode/encode gps/glonass terminal TQ GPRS transport protocol messages

   help:
   https://docs.google.com/spreadsheets/d/15s-2ZbqOQ1bZvAtFFm9sIEuKy3jbJzxdeynp72sjoYU/edit?usp=sharing

   compile:
   make -B tqgprs
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include "glonassd.h"
#include "worker.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"


void logg(ST_WORKER *worker, int it_error, char *msg)
{
    if( worker && msg ) {
        if( worker->listener->log_all )
            logging("terminal_decode[%s:%d]: %s\n", worker->listener->name, worker->listener->port, msg);
        else if( it_error && worker->listener->log_err )
            logging("terminal_decode[%s:%d]: %s\n", worker->listener->name, worker->listener->port, msg);
    }
}


/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{
	ST_RECORD *record = NULL;
	char cImei[16], cTime[10], cDate[10], cCmd[10], cStatus[10], cLon, cLat, cValid;
    char *cRec, *saveptr = NULL;
	struct tm tm_data;
	time_t ulliTmp;
	double dLon, dLat, dSpeed;
	int iTemp, iCurs, iReadedRecords = 0;
    int net_mcc, net_mnc, net_lac, net_cellid;

    logg(worker, 0, parcel);

    if( !parcel || parcel_size <= 0 || !answer ){
        logg(worker, 1, "!parcel || parcel_size <= 0 || !answer => return");
		return;
    }

	answer->size = 0;	// :)

    //         1       2    3   4     5     6      7     8    9    10   11      12    13  14   15   16
    // *HQ,8170851119,V1,175222,A,5547.5627,N,03832.8971,E,029.21,074,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175232,A,5547.5532,N,03833.0421,E,034.00,121,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175242,A,5547.4716,N,03833.1692,E,041.78,141,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175252,A,5547.3812,N,03833.2951,E,041.58,142,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175302,A,5547.2884,N,03833.4181,E,041.15,143,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175312,A,5547.1971,N,03833.5211,E,038.07,151,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175322,A,5547.1080,N,03833.6285,E,038.41,140,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175332,A,5547.0304,N,03833.7492,E,036.64,132,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175342,A,5546.9771,N,03833.8944,E,032.93,120,300421,FFFF9FFF,250,02,09039,7762#*HQ,8170851119,V1,175352,A,5546.9263,N,03834.0460,E,040.15,120,300421,FFFF9FFF,250,02,09039,7762#
    cRec = strtok_r(strdup(parcel), "#", &saveptr);
	while( cRec ) {

        if( strlen(cRec) > 70) {
            iTemp = sscanf(&cRec[4],
                            //  1     2      3  4   5  6   7  8   9  10  11 12  13 14 15 16
                            "%15[^,],%9[^,],%6s,%c,%lf,%c,%lf,%c,%lf,%d,%6s,%8s,%d,%d,%d,%d",
                            cImei,      // 1
                            cCmd,       // 2
                            cTime,      // 3
                            &cValid,    // 4   V is invalid, A/S - valid
                            &dLat,      // 5   format : DDFF.FFFF, DD : Degree (00 ~ 90), FF.FFFF : minute
                            &cLat,      // 6   N/S
                            &dLon,      // 7   format : DDDFF.FFFF, DDD : Degree(00 ~ 180), FF.FFFF : minute
                            &cLon,      // 8   E/W
                            &dSpeed,    // 9   speed in knots
                            &iCurs,     // 10  Azimuth
                            cDate,      // 11  DDMMYY:day/month/year
                            cStatus,	// 12  status: 4 bytes
                            //  13         14      15         16
                            &net_mcc, &net_mnc, &net_lac, &net_cellid
            );

            if( iTemp >= 12 ) {	// all OK

                logg(worker, 0, cImei);

				if( answer->count < MAX_RECORDS - 1 )
					answer->count++;
				record = &answer->records[answer->count - 1];

				snprintf(record->tracker, SIZE_TRACKER_FIELD, "TQ");
				snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
				snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 1);
				snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", cImei);

				// переводим время GMT и текстовом формате в местное
				memset(&tm_data, 0, sizeof(struct tm));
				sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
				tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
				tm_data.tm_year += 100;
				sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);

				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
				gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
				// получаем время как число секунд от начала суток
				record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
				// в tm_data обнуляем время
				tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
				// получаем дату
            	record->data = timegm(&tm_data);

                iTemp = dLat / 100.0;
                record->lat = (dLat - iTemp * 100) / 60.0 + iTemp;
				record->clat = cLat;

				iTemp = dLon / 100.0;
				record->lon = (dLon - iTemp * 100) / 60.0 + iTemp;
				record->clon = cLon;

				record->curs = iCurs;
				record->speed = Round(dSpeed * MILE, 1);
				record->height = 0;
				record->satellites = 0;

				record->valid = cValid == 'V' ? 1 : 0;

				memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
                ++iReadedRecords;

                // not used?
                //answer->size += sprintf(&answer->answer[answer->size], "%s", "RCPTOK\r\n");
			}	// if( iTemp >= 12 )

        }   // if( strlen(cRec) > 70)

		cRec = strtok_r(NULL, "#", &saveptr);
	}	// while( cRec )

}   // terminal_decode
//------------------------------------------------------------------------------


/*
   encode function
   records - pointer to array of ST_RECORD struct.
   reccount - number of struct in array, and returning (negative if authentificate required)
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
