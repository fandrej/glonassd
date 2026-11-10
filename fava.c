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
   Строка, разрезанная между чтениями сокета (11.11.26). Воркер читает посылку «по паузам» (worker.c: recv + 10 мс),
   и посылка, пришедшая двумя кусками, раньше теряла разрезанную строку, а OK отправлялся на каждый кусок — «Мультитул»
   принимал лишний OK за подтверждение следующей пачки. Теперь хвост без \n хранится до следующего чтения этого же
   соединения (один поток на соединение — __thread) и склеивается с продолжением; OK — только когда хвоста нет, т.е.
   пачка пришла целиком (один OK на пачку). Точки из уже пришедших целых строк пишутся сразу; если соединение
   оборвётся, OK не будет и приложение пошлёт пачку снова — повторы отсекает pg.c (FAVA: cimei + ttime).
   Другие клиенты fava (строка без \n в конце) — их нет (Андрей, 11.11.26).
*/
#define FAVA_TAIL_MAX 1024
static __thread char fava_tail[FAVA_TAIL_MAX];
static __thread int fava_tail_len = 0;
static __thread char fava_buf[FAVA_TAIL_MAX + SOCKET_BUF_SIZE + 1];


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

    // хвост прошлого чтения + новые данные
    int len = 0;
    if( fava_tail_len ) {
        memcpy(fava_buf, fava_tail, fava_tail_len);
        len = fava_tail_len;
        fava_tail_len = 0;
    }
    if( parcel_size > SOCKET_BUF_SIZE )
        parcel_size = SOCKET_BUF_SIZE;
    memcpy(fava_buf + len, parcel, parcel_size);
    len += parcel_size;
    fava_buf[len] = 0;

    // всё после последнего \n — незаконченная строка: ждёт продолжения
    int end = len;
    while( end > 0 && fava_buf[end - 1] != '\n' )
        end--;
    if( end < len ) {
        if( len - end < FAVA_TAIL_MAX ) {
            memcpy(fava_tail, fava_buf + end, len - end);
            fava_tail_len = len - end;
        }
        else if( worker->listener->log_err )
            logging("terminal_decode[%s:%d]: line without \\n longer than %d, dropped\n", worker->listener->name, worker->listener->port, FAVA_TAIL_MAX);
        fava_buf[end] = 0;
    }
    parcel = fava_buf;

    //          ID        DataTime     Lat    NS   Lon    EW Bear Alt Speed Sat Flags 3field-reserv (0,0.0,0.0)
    //  0        1          2           3      4     5      6  7  8   9 10 11 12 13 14
    // "^353958060415983;1440351467;55.4604883;N;65.3381217;E;50;141;1.0;0;0;0;0;0.0\n"
    // "^353958060415983;1473500096;55.4608500;N;65.3397000;E;116;80;0.0;5;0;15;5;0"
    cPart = strtok(parcel, "\n");

    rec_ok = 1; // первую запись всегда создаем
    while( cPart ) {

        if( rec_ok ) {
            if( answer->count >= MAX_RECORDS - 1 ) {
                // массив полон: остальные строки отбрасываем (раньше они молча затирали последнюю запись)
                if( worker->listener->log_err )
                    logging("terminal_decode[%s:%d]: more than %d records in parcel, rest dropped\n", worker->listener->name, worker->listener->port, MAX_RECORDS - 1);
                break;
            }
            answer->count++;
            record = initST_RECORD(&answer->records[answer->count - 1]);
            rec_ok = 0;
        }    // if( rec_ok )

        //                        1     2   3  4   5  6  7  8   9 10 11  12 13 14
        iTemp = sscanf(cPart, "^%15[^;];%lu;%lf;%c;%lf;%c;%d;%d;%lf;%u;%u;%lf;%u;%u",
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

        if( iTemp == 1 ) {
            // пришел ответ на команду
            // (не обнуляем всю посылку: такой же вид имеет и обрезок «^ID» в конце разрезанной посылки);
            // посылка из одних ответов останется без записей, и OK не отправится, как и раньше
            iTemp = sscanf(cPart, "^%15[^;];%s", record->imei, cTemp);
            initST_RECORD(record);
        }
        else if( iTemp >= 10 ) {
            // отметка

            if( iTemp < 14 ) {
                if( worker->listener->log_err )
                    logging("terminal_decode[%s:%d]: %d parameters recognized, but 14 expected in record\n", worker->listener->name, worker->listener->port, iTemp);
                record->status = 0;
                record->probeg = 0;
                record->ainputs[6] = 0;
                record->ainputs[7] = 0;
            }

            snprintf(record->tracker, SIZE_TRACKER_FIELD, "FAVA");
            snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
            snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", 1);

            // достоверна при >2 спутниках; «Мультитул» не всегда знает число спутников (0) и шлёт точность, м,
            // в поле 13 (ainputs[6]) — тогда достоверна при точности не хуже 50 м
            record->valid = (record->lat > 0.0 && record->lon > 0.0 &&
                             (record->satellites > 2 ||
                              (record->satellites == 0 && record->ainputs[6] > 0 && record->ainputs[6] <= 50)));

            record->ainputs[0] = (record->status & 1);    //  кнопка SOS
            record->alarm = record->ainputs[0];
            record->ainputs[2] = 0;    //  кнопка запрос связи

            record->ttime = ulliTmp;

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
            // испорченная строка (например, посылка разрезана посреди строки): пропускаем только её,
            // запись не заполнена (rec_ok == 0) — её место займёт следующая строка
            if( worker->listener->log_err )
                logging("terminal_decode[%s:%d]: bad record skipped (%d fields)\n", worker->listener->name, worker->listener->port, iTemp);
            initST_RECORD(record);
        }

        cPart = strtok(NULL, "\n");
    }    // while( cPart )

    // последняя запись так и не заполнилась (испорченная строка в конце)
    if( answer->count && !rec_ok )
        answer->count--;

    if( answer->count ) {
        // OK — только на пачку целиком: пока ждём конец разрезанной строки, точки пишутся, но без ответа
        if( fava_tail_len )
            answer->size = 0;
        else
            answer->size = snprintf(answer->answer, 4, "OK\n");
    }
    else
        memset(answer, 0, sizeof(ST_ANSWER));
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
