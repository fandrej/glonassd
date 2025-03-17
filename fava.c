/*
   fava.c
   shared library for decode/encode gps/glonass terminal "CarTracking for Android" messages

   help:
   see CarTracking for Android project

   compile:
   make -B fava
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
    int iTemp, rec_ok;
    char *cPart, cTemp[SOCKET_BUF_SIZE];    // используется и для приема ответов на команды
    struct tm tm_data;
    time_t ulliTmp;

    if( !parcel || parcel_size <= 0 || !answer )
        return;

    //          ID        DataTime     Lat    NS   Lon    EW Bear Alt Speed Sat Flags 3field-reserv (0,0.0,0.0)
    //  0        1          2           3      4     5      6  7  8   9 10 11 12 13 14
    // "^353958060415983;1440351467;55.4604883;N;65.3381217;E;50;141;1.0;0;0;0;0;0.0\n"
    // "^353958060415983;1473500096;55.4608500;N;65.3397000;E;116;80;0.0;5;0;15;5;0"
    cPart = strtok(parcel, "\n");

    rec_ok = 1; // первую запись всегда создаем
    while( cPart ) {

        if( rec_ok ) {
            if( answer->count < MAX_RECORDS - 1 )
                answer->count++;
            record = &answer->records[answer->count - 1];
            rec_ok = 0;
        }    // if( rec_ok )

        //                        1     2   3  4   5  6  7  8   9 10 11  12 13 14
        iTemp = sscanf(cPart, "^%[^;];%lu;%lf;%c;%lf;%c;%d;%d;%lf;%u;%u;%lf;%u;%u",
                            record->imei, // 1
                            &ulliTmp, // 2
                            &record->lat, // 3
                            &record->clat, // 4
                            &record->lon, // 5
                            &record->clon, // 6
                            &record->curs, // 7
                            &record->height, // 8
                            &record->speed, // 9
                            &record->satellites, // 10
                            &record->status, // 11
                            &record->probeg, // 12
                            &record->ainputs[6], // 13
                            &record->ainputs[7]    // 14
                          );

        if( iTemp == 1 ) {    // пришел ответ на команду
            iTemp = sscanf(cPart, "^%[^;];%s", record->imei, cTemp);
            answer->count = 0;
        } else if( iTemp >= 10 ) {    // отметка

            if( iTemp < 14 ) {
                if( worker->listener->log_err ) {
                    logging("terminal_decode[%s:%d]: %d parameters recognized, but 14 expected in record\n", worker->listener->name, worker->listener->port, iTemp);
                }
                record->status = 0;
                record->probeg = 0;
                record->ainputs[6] = 0;
                record->ainputs[7] = 0;
            }

            snprintf(record->tracker, SIZE_TRACKER_FIELD, "FAVA");
            snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
            snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 1);

            record->valid = (record->satellites > 2 && record->lat > 0.0 && record->lon > 0.0);

            record->ainputs[0] = (record->status & 1);    //  кнопка SOS
            record->alarm = record->ainputs[0];
            record->ainputs[2] = 0;    //  кнопка запрос связи

            // переводим время GMT в местное
            ulliTmp += GMT_diff;
            gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
            // получаем время как число секунд от начала суток
            record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
            // в tm_data обнуляем время
            tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
            // получаем дату
        	record->data = timegm(&tm_data);

            rec_ok++;

            memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
        }    // if( iTemp == 14 )
        else {
            answer->count = 0;
        }

        cPart = strtok(NULL, "\n");
    }    // while( cPart )

    if( answer->count ) {
        answer->size = snprintf(answer->answer, 4, "OK\n");
    } else {
        memset(answer, 0, sizeof(ST_ANSWER));
    }
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
