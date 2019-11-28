/*
   wialonips.c
   shared library for decode/encode gps/glonass terminal Wialon IPS messages

   help:
   https://docs.google.com/spreadsheets/d/15s-2ZbqOQ1bZvAtFFm9sIEuKy3jbJzxdeynp72sjoYU/edit?usp=sharing

   compile:
   make -B wialonips
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include "glonassd.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"


/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
    ST_RECORD *record = NULL;
    char cTime[10], cDate[10], cLon, cLat, *cRec, *cRec1;
    struct tm tm_data, tm_today;
    time_t ulliTmp;
    double dLon, dLat, dAltitude, dHDOP;
    int iAnswerSize, iTemp, iCurs, iSatellits, iSpeed, iInputs = 0, iOutputs = 0, iReadedRecords = 0;

    if( !parcel || parcel_size <= 0 || !answer )
        return;

    answer->size = 0;	// :)

    cRec = strtok(parcel, "\r\n");
    while( cRec ) {
        ulliTmp = time(NULL);   // get simple current UTC time
        gmtime_r(&ulliTmp, &tm_today);  // simple->struct tm
        tm_today.tm_year += 100;


        switch( cRec[1] ) {
        case 'L':	// пакет логина: #L#imei;password\r\n
            // #L#353451048036030;NA
            // answer: #AL#1\r\n

            memset(answer->lastpoint.imei, 0, SIZE_TRACKER_FIELD);
            iTemp = sscanf(cRec, "#L#%[^;];%*s", answer->lastpoint.imei);

            if( iTemp == 1 && strlen(answer->lastpoint.imei) ) {
                iAnswerSize = 8;	// 7 + завершающий 0
                answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AL#1\r\n");
            }	// if( iTemp == 1 )

            break;
        case 'P':	// пинговый пакет: #P#\r\n
            // answer: #AP#\r\n

            iAnswerSize = 7;
            answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AP#\r\n");

            break;                                      //    1    2    3   4     5   6     7     8      9     10
        case 'S':	// SD, Сокращённый пакет с данными: #SD#date;time;lat1;lat2;lon1;lon2;speed;course;height;sats\r\n
            // answer: #ASD#1\r\n

            if( !answer->count ) {	// только 1 ответ на все принятые записи
                iAnswerSize = 9;
                answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#ASD#1\r\n");
            }	// if( !answer->count )

            iTemp = sscanf(cRec, "#SD#%[^;];%[^;];%lf;%c;%lf;%c;%d;%d;%lf;%d",
                                cDate, // 1
                                cTime, // 2
                                &dLat, // 3
                                &cLat, // 4
                                &dLon, // 5
                                &cLon, // 6
                                &iSpeed, // 7
                                &iCurs, // 8
                                &dAltitude, // 9
                                &iSatellits	// 10
                              );
            if( iTemp == 10 ) {	// успешно считаны все поля

                if( answer->count < MAX_RECORDS - 1 )
                    answer->count++;
                record = &answer->records[answer->count - 1];

                snprintf(record->tracker, SIZE_TRACKER_FIELD, "WIPS");
                snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
                snprintf(record->soft, SIZE_TRACKER_FIELD, "%f", 1.1);
                snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", answer->lastpoint.imei);

                // переводим присланное время GMT и текстовом формате в местное
                memset(&tm_data, 0, sizeof(struct tm));
                sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
                tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
                tm_data.tm_year += 100;

                /*
                В ночь на 07.04.19 обнулились счетчики дат в системе GPS.
                Старое оборудование свихнулось. Дата стала рандомной, а время правильное.
                */
                if( tm_data.tm_year != tm_today.tm_year || tm_data.tm_mon != tm_today.tm_mon){
                    ulliTmp = time(NULL) + GMT_diff;
                }
                else {
    				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
                }
                gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct

                // получаем время как число секунд от начала суток
                sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);
                record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;

                // в tm_data обнуляем время
                tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
                // получаем дату
                record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

                iTemp = 0.01 * dLat;
                record->lat = (dLat - iTemp * 100.0) / 60.0 + iTemp;
                record->clat = cLat;

                iTemp = 0.01 * dLon;
                record->lon = (dLon - iTemp * 100.0) / 60.0 + iTemp;
                record->clon = cLon;

                record->curs = iCurs;
                record->speed = iSpeed;
                record->height = (int)dAltitude;
                record->satellites = iSatellits;

                if( record->satellites > 2 && record->lat > 0.0 && record->lon > 0.0 )
                    record->valid = 1;
                else
                    record->valid = 0;

                memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
            }	// if( iTemp == 10 )

            break;                      //   1    2   3    4    5    6     7     8      9     10   11    12      13    14    15     16
        case 'D':	// пакет с данными: #D#date;time;lat1;lat2;lon1;lon2;speed;course;height;sats;hdop;inputs;outputs;adc;ibutton;params\r\n
            //      1       2       3     4     5      6  7  8      9    10    11   12    13        14                                        15 16
            // #D#181215;083214;5525.4081;N;06517.1674;E;13; 65;0.000000;15;0.500000;0;973668352;0.000000,NA,NA,NA,NA,NA,NA,NA,NA,NA,0.000000;NA;gsm_status:1:3,acc_trigger:1:1,can_b0:2:0.000000,valid:1:0,soft:1:229
            // #D#011116;033902;5526.6558;N;06520.9955;E;19;229;      72; 8;     0.9;0;        0;                                            ;NA;tmp:1:30,pwrext:2:13.38,freq1:1:0,freq2:1:0
            // #D#011116;033802;5526.6604;N;06521.0047;E; 0;  0;      72; 9;     0.9;0;        0;                                            ;NA;lat1:3:N 55 26.6604,lon1:3:E 65 21.0047,course:1:0,sys:3:GPS,gsm:3:home,hw:3:2.0,fw:3:1.7,cnt:1:30559,tmp:1:30,currtmp:1:30,pwrext:2:13.42,freq1:1:0,freq2:1:0,rst:3:unknown,systime:3:0d00h13m36s
            // answer: #AD#1\r\n

            if( !answer->count ) {	// только 1 ответ на все принятые записи
                iAnswerSize = 8;
                answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AD#1\r\n");
            }	// if( !answer->count )

            //                         1     2    3   4  5   6  7  8  9  10  11 12 13  14
            iTemp = sscanf(cRec, "#D#%[^;];%[^;];%lf;%c;%lf;%c;%d;%d;%lf;%d;%lf;%d;%d;%*s",
                                cDate, // 1
                                cTime, // 2
                                &dLat, // 3
                                &cLat, // 4
                                &dLon, // 5
                                &cLon, // 6
                                &iSpeed, // 7
                                &iCurs, // 8
                                &dAltitude, // 9
                                &iSatellits, // 10
                                &dHDOP, // 11
                                &iInputs, // 12
                                &iOutputs // 13
                              );
            if( iTemp >= 10 ) {

                if( answer->count < MAX_RECORDS - 1 )
                    answer->count++;
                record = &answer->records[answer->count - 1];

                snprintf(record->tracker, SIZE_TRACKER_FIELD, "WIPS");
                snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
                snprintf(record->soft, SIZE_TRACKER_FIELD, "%f", 1.1);
                snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", answer->lastpoint.imei);

                // переводим время GMT и текстовом формате в местное
                memset(&tm_data, 0, sizeof(struct tm));
                sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
                tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
                tm_data.tm_year += 100;

                /*
                В ночь на 07.04.19 обнулились счетчики дат в системе GPS.
                Старое оборудование свихнулось. Дата стала рандомной, а время правильное.
                */
                if( tm_data.tm_year != tm_today.tm_year || tm_data.tm_mon != tm_today.tm_mon){
                    ulliTmp = time(NULL) + GMT_diff;
                }
                else {
    				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
                }
                gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct

                // получаем время как число секунд от начала суток
                sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);
                record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;

                // в tm_data обнуляем время
                tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
                // получаем дату
                record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

                iTemp = 0.01 * dLat;
                record->lat = (dLat - iTemp * 100.0) / 60.0 + iTemp;
                record->clat = cLat;

                iTemp = 0.01 * dLon;
                record->lon = (dLon - iTemp * 100.0) / 60.0 + iTemp;
                record->clon = cLon;

                record->curs = iCurs;
                record->speed = iSpeed;
                record->height = (int)dAltitude;
                record->satellites = iSatellits;

                if( record->satellites > 2 && record->lat > 0.0 && record->lon > 0.0 )
                    record->valid = 1;
                else
                    record->valid = 0;

            }	// if( iTemp >= 10 )

            if( iTemp >= 11 ) {
                record->hdop = (int)dHDOP;
            }	// if( iTemp >= 11 )

            if( iTemp >= 12 ) {
                record->inputs = iInputs;

                record->ainputs[0] = (iInputs & 1); // кнопка SOS
                record->ainputs[1] = (iInputs & 2); // зажигание
                record->ainputs[2] = (iInputs & 4); // кнопка запрос связи
                record->ainputs[3] = (iInputs & 8); // двери

                record->zaj = record->ainputs[1];
                record->alarm = record->ainputs[0];
            }	// if( iTemp >= 12 )

            if( iTemp >= 13 ) {
                record->outputs = iOutputs;
            }	// if( iTemp >= 13 )

            if( record )
                memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));

            break;
        case 'B':	// Пакет с чёрным ящиком
            /* представляет собой несколько тел сокращённых или полных пакетов (без
                указания типа), разделённых между собой символом |
                Пример:
                //   1    2   3     4   5    6     7     8      9     10
                #B#date;time;lat1;lat2;lon1;lon2;speed;course;height;sats|date;time;lat1;lat2;lon1;lon2;speed;course;height;sats|date;time;lat1;lat2;lon1;lon2;speed;course;height;sats\r\n
            */
            // answer: #AB#x\r\n, где x - количество зафиксированных сообщений

            cRec1 = strtok(&cRec[3], "|");

            while(cRec1) {
                iReadedRecords++;   // кол-во считанных сообщений
                //                       1      2   3   4  5   6  7  8  9  10   11
                iTemp = sscanf(cRec1, "%[^;];%[^;];%lf;%c;%lf;%c;%d;%d;%lf;%d;%*[^|]",
                                    cDate, // 1
                                    cTime, // 2
                                    &dLat, // 3
                                    &cLat, // 4
                                    &dLon, // 5
                                    &cLon, // 6
                                    &iSpeed, // 7
                                    &iCurs, // 8
                                    &dAltitude, // 9
                                    &iSatellits // 10
                                  );

                if( iTemp >= 10 ) {	// успешно считаны все поля

                    if( answer->count < MAX_RECORDS - 1 )
                        answer->count++;
                    record = &answer->records[answer->count - 1];

                    snprintf(record->tracker, SIZE_TRACKER_FIELD, "WIPS");
                    snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
                    snprintf(record->soft, SIZE_TRACKER_FIELD, "%f", 1.1);
                    snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", answer->lastpoint.imei);

                    // переводим время GMT и текстовом формате в местное
                    memset(&tm_data, 0, sizeof(struct tm));
                    sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
                    tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
                    tm_data.tm_year += 100;

                    /*
                    В ночь на 07.04.19 обнулились счетчики дат в системе GPS.
                    Старое оборудование свихнулось. Дата стала рандомной, а время правильное.
                    */
                    if( tm_data.tm_year != tm_today.tm_year || tm_data.tm_mon != tm_today.tm_mon){
                        ulliTmp = time(NULL) + GMT_diff;
                    }
                    else {
        				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
                    }
                    gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct

                    // получаем время как число секунд от начала суток
                    sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);
                    record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
                    // в tm_data обнуляем время
                    tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
                    // получаем дату
                    record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

                    iTemp = 0.01 * dLat;
                    record->lat = (dLat - iTemp * 100.0) / 60.0 + iTemp;
                    record->clat = cLat;

                    iTemp = 0.01 * dLon;
                    record->lon = (dLon - iTemp * 100.0) / 60.0 + iTemp;
                    record->clon = cLon;

                    record->curs = iCurs;
                    record->speed = iSpeed;
                    record->height = (int)dAltitude;
                    record->satellites = iSatellits;

                    if( record->satellites > 2 && record->lat > 0.0 && record->lon > 0.0 )
                        record->valid = 1;
                    else
                        record->valid = 0;

                }	// if( iTemp >= 10 )

                cRec1 = strtok(NULL, "|");
            }	// while(cRec1)

            iAnswerSize = 7;
            if( iReadedRecords > 99 )
                iAnswerSize += 3;
            else if( iReadedRecords > 9 )
                iAnswerSize += 2;
            else
                iAnswerSize += 1;

            answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AB#%d\r\n", iReadedRecords);

            break;
        case 'M':	// Сообщение для водителя
            // answer: #AM#1\r\n

            iAnswerSize = 8;
            answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AM#1\r\n");

            break;                               //  1  2    3     4    5   6       7
        case 'I':	// пакет с фотоизображением: #I#sz;ind;count;date;time;name\r\nBIN
            // answer: #AI#ind;1\r\n - пакет с блоком изображения принят
            // answer: #AI#1\r\n – изображение полностью принято и сохранено в Wialon

            // лень сохранять
            //                         1  2  3   4     5    6
            iTemp = sscanf(cRec, "#AI#%d;%d;%d;%[^;];%[^;];%*s",
                                &iSpeed, // 1
                                &iCurs, // 2
                                &iSatellits, // 3
                                cDate, // 4
                                cTime // 5
                              );

            iAnswerSize = 15;

            if( iTemp >= 3 ) {
                if( iCurs == iSatellits )
                    answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AI#1\r\n");
                else
                    answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AI#%d;1\r\n", iCurs);
            }	// if( iTemp >= 3 )
            else
                answer->size += snprintf(&answer->answer[answer->size], iAnswerSize, "#AI#1\r\n");

        }	// switch( cRec[1] )

        cRec = strtok(NULL, "\r\n");
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
