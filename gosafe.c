/*
gosafe.c
shared library for decode/encode gps/glonass terminal with GOSAFE Product Protocol V2.13

sudo service glonassd stop
sudo ./glonassd start -c /opt/glonassd/glonassd.conf
sudo service glonassd start

"Наш"
https://proma-sat.ru/wp-content/uploads/2018/07/%D0%98%D0%BD%D1%81%D1%82%D1%80%D1%83%D0%BA%D1%86%D0%B8%D1%8F-%D0%BF%D0%BE-%D1%8D%D0%BA%D1%81%D0%BF%D0%BB%D1%83%D0%B0%D1%82%D0%B0%D1%86%D0%B8%D0%B8.pdf
Это китайский:
https://gosafesystem.com/wp-content/uploads/2017/05/G797-datasheet-ver1.0_compressed.pdf

Модели, которые работают по этому же протоколу:
Proma Sat G78
Proma Sat G606
Proma Sat G717
Proma Sat G787
Proma Sat G797
Proma Sat 1000
Gosafe G606
Gosafe G626
Gosafe G6B6
Gosafe G6C6
Gosafe G714
Gosafe G717
Gosafe G737
Gosafe G777
Gosafe G79
Gosafe G797

Help:
https://github.com/traccar/traccar/blob/master/src/main/java/org/traccar/protocol/GoSafeProtocolDecoder.java

http://cpp.sh/

compile:
make -B gosafe
*/

#include "glonassd.h"
#include "worker.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"

static void terminal_decode_txt(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker);
static void terminal_decode_bin(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker);
static long long int hex2dec(char *c, size_t size);
static int decodeSYS(ST_RECORD *record, char *chank);
static int decodeGPS(ST_RECORD *record, char *chank);


/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	//log2file("/var/www/locman.org/tmp/gcc/satlite_", parcel, parcel_size);

	if( parcel[0] == '*' && parcel[1] == 'G' && parcel[2] == 'S' ) {
		terminal_decode_txt(parcel, parcel_size, answer, worker);
    }
	else {
		terminal_decode_bin(parcel, parcel_size, answer, worker);
    }
}   // terminal_decode
//------------------------------------------------------------------------------


static void terminal_decode_txt(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{
    const char *delim_packet = "#";
    const char *delim_part = ",";
    const char *delim_param = ":";
    ST_RECORD *record = NULL;
    char *cPaket, *cPart, *cParams, *saveptr1, *saveptr2, *saveptr3;
    char parameter[4], valid;
    int packet_num, part_num, param_num, iFields;
    int gps_dimension, record_ok = 1;
    float hdop;
    struct tm tm_data;
    time_t ulliTmp;

    if( !parcel || parcel_size <= 0 || !answer )
        return;

    logging("terminal_decode[%s:%d]: terminal_decode_txt", worker->listener->name, worker->listener->port);

    // TCP hartbeat data
    // *GS02,357852034572894#
    // Device Upload Information
    // *GS02,357852034572894,CLL:064940150411;460;0;10033;17262,STT:2;0,MGR:1000,ADC:0;12.1;1;36.2;2;4.3,GFS:0FFFFFFF;0FFFFFFF,OBD:410C0C3C410D01,FUL:47226696#
    //    1         2                   3      4  5   6     7       8 9       10    11  12 13  14 15  16        17       18             19               20

    answer->count = 0;
    saveptr1 = NULL;
    for(packet_num = 1, cPaket = strtok_r(parcel, delim_packet, &saveptr1); cPaket; packet_num++, cPaket = strtok_r(NULL, delim_packet, &saveptr1)) {
        logging("terminal_decode[%s:%d]: cPaket=%s", worker->listener->name, worker->listener->port, cPaket);

        if( strlen(cPaket) > 21 ) {
            // it's data
            if( record_ok > 0 && answer->count < MAX_RECORDS - 1 )
            	answer->count++;
            record = &answer->records[answer->count - 1];

            saveptr2 = NULL;
            for(part_num = 0, cPart = strtok_r(cPaket, delim_part, &saveptr2); cPart; part_num++, cPart = strtok_r(NULL, delim_part, &saveptr2)) {
                logging("terminal_decode[%s:%d]: cPart(%d)=%s", worker->listener->name, worker->listener->port, part_num, cPart);

                if( part_num == 0 ){
                    // *GS02
                    iFields = sscanf(cPart, "*GS%s", record->soft);
                }
                else if( part_num == 1 ){
                    // 357852034572894
    				snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", cPart);
                }
                else {

                    saveptr3 = NULL;
                    for(param_num = 0, cParams = strtok_r(cPart, delim_param, &saveptr3); cParams; param_num++, cParams = strtok_r(NULL, delim_param, &saveptr3)) {
                        if( param_num == 0 ) {
            				snprintf(parameter, 4, "%s", cParams);
                        }
                        else if( param_num == 1 ) {
                            logging("terminal_decode[%s:%d]: parameter=%s values=%s", worker->listener->name, worker->listener->port, parameter, cParams);

                            if( strcmp(parameter, "GPS") == 0 ) {
                				memset(&tm_data, 0, sizeof(struct tm));

                                // 1 2 3  4 5  6       7   8       9 10 11  121314
                                // 025804;2;N23.164396;E113.428541;0;0;1.10;161112
                                //                          1  2  3   4  5  6  7  8   9 10 11  12 13 14
                    			iFields = sscanf(cParams, "%2d%2d%2d;%d;%c%lf;%c%lf;%lf;%u;%f;%2d%2d%2d",
                    								&tm_data.tm_hour,   // 1
                                                    &tm_data.tm_min,    // 2
                                                    &tm_data.tm_sec,    // 3
                    								&gps_dimension,     // 4
                    								&record->clat,      // 5
                    								&record->lat,       // 6
                    								&record->clon,      // 7
                    								&record->lon,       // 8
                    								&record->speed,     // 9
                    								&record->curs,      // 10
                    								&hdop,              // 11
                    								&tm_data.tm_mday,   // 12
                                                    &tm_data.tm_mon,    // 13
                                                    &tm_data.tm_year    // 14
                    							  );
                                if( iFields < 14 ){
                                    valid = ' ';
                                    // 1 2 3  4 5  6       7   8       9 10111213
                                    // 065633;A;N23.164865;E113.428970;0;0;150411
                                    //                          1  2  3   4  5  6  7  8   9 10  11 12 13
                        			iFields = sscanf(cParams, "%2d%2d%2d;%c;%c%lf;%c%lf;%lf;%u;%2d%2d%2d",
                        								&tm_data.tm_hour,   // 1
                                                        &tm_data.tm_min,    // 2
                                                        &tm_data.tm_sec,    // 3
                        								&valid,             // 4
                        								&record->clat,      // 5
                        								&record->lat,       // 6
                        								&record->clon,      // 7
                        								&record->lon,       // 8
                        								&record->speed,     // 9
                        								&record->curs,      // 10
                        								&tm_data.tm_mday,   // 11
                                                        &tm_data.tm_mon,    // 12
                                                        &tm_data.tm_year    // 13
                        							  );
                                    record->valid = valid == 'A' && record->lat > 0.0;
                                }   // if( iFields < 14 )
                                else {
                                    record->hdop = (int)hdop;
                                    record->valid = gps_dimension > 2 && record->lat > 0.0;
                                }
                                /*
                                logging("terminal_decode[%s:%d]: iFields=%d", worker->listener->name, worker->listener->port, iFields);
                                logging("terminal_decode[%s:%d]: tm_data.tm_hour=%d, tm_data.tm_min=%d, tm_data.tm_sec=%d", worker->listener->name, worker->listener->port, tm_data.tm_hour, tm_data.tm_min, tm_data.tm_sec);
                                logging("terminal_decode[%s:%d]: record->clat=%c", worker->listener->name, worker->listener->port, record->clat);
                                logging("terminal_decode[%s:%d]: record->lat=%lf", worker->listener->name, worker->listener->port, record->lat);
                                logging("terminal_decode[%s:%d]: record->clon=%c", worker->listener->name, worker->listener->port, record->clon);
                                logging("terminal_decode[%s:%d]: record->lon=%lf", worker->listener->name, worker->listener->port, record->lon);
                                logging("terminal_decode[%s:%d]: record->speed=%lf", worker->listener->name, worker->listener->port, record->speed);
                                logging("terminal_decode[%s:%d]: record->curs=%lf", worker->listener->name, worker->listener->port, record->curs);
                                logging("terminal_decode[%s:%d]: tm_data.tm_mday=%d, tm_data.tm_mon=%d, tm_data.tm_year=%d", worker->listener->name, worker->listener->port, tm_data.tm_mday, tm_data.tm_mon, tm_data.tm_year);
                                */

                                if( iFields >= 13 ){
                    				// переводим время GMT и текстовом формате в местное
                    				tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/
                    				tm_data.tm_year += 100;

                    				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
                    				gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
                    				// получаем время как число секунд от начала суток
                    				record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
                    				// в tm_data обнуляем время
                    				tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
                    				// получаем дату
                    				record->data = timegm(&tm_data);	// local struct->local simple & mktime epoch

                                    record_ok = 1;
                                }   // if( iFields >= 13 )
                                else {
                                    record_ok = 0;
                                }
                            }   // if( strcmp(parameter, "GPS") == 0 )
                            else if( strcmp(parameter, "STT") == 0 ) {
                                // 1 2
                                // 2;0
                    			iFields = sscanf(cParams, "%d;%d",
                    								&record->status,  // 1
                                                    &record->alarm    // 2
                       							  );
                                record->zaj = record->status & 256;     // bit 9
                                record->alarm = record->alarm & 512;    // bit 10
                                /*
                                logging("terminal_decode[%s:%d]: record->status=%d", worker->listener->name, worker->listener->port, record->status);
                                logging("terminal_decode[%s:%d]: record->zaj=%d", worker->listener->name, worker->listener->port, record->zaj);
                                logging("terminal_decode[%s:%d]: record->alarm=%d", worker->listener->name, worker->listener->port, record->alarm);
                                */
                            }   // else if( strcmp(parameter, "STT") == 0 )
                            else if( strcmp(parameter, "MGR") == 0 ) {
                                // 1000
                    			iFields = sscanf(cParams, "%lf",
                    								&record->probeg   // 1 (meters)
                       							  );
                                //logging("terminal_decode[%s:%d]: record->probeg=%lf", worker->listener->name, worker->listener->port, record->probeg);
                            }   // else if( strcmp(parameter, "MGR") == 0 )
                            else if( strcmp(parameter, "ADC") == 0 ) {
                                // 1   2  3   4  5  6
                                // 0;12.1;1;36.2;2;4.3

                                hdop = 0.0;
                                //                          1   2   3   4   5   6
                    			iFields = sscanf(cParams, "%*d;%lf;%*d;%f;%*d;%lf",
                    								&record->vbort,   // 2
                                                    &hdop,            // 4
                                                    &record->vbatt    // 6
                       							  );
                                record->temperature = (int)hdop;
                                /*
                                logging("terminal_decode[%s:%d]: record->vbort=%lf", worker->listener->name, worker->listener->port, record->vbort);
                                logging("terminal_decode[%s:%d]: record->vbatt=%lf", worker->listener->name, worker->listener->port, record->vbatt);
                                logging("terminal_decode[%s:%d]: record->temperature=%d", worker->listener->name, worker->listener->port, record->temperature);
                                */
                            }   // else if( strcmp(parameter, "ADC") == 0 )

                        }   // else if( param_num == 1 )
                    }   // for(cParams = strtok_r(cPart

                }   // else
            }   // for(cPaket = strtok_r(parcel

            if( record_ok > 0 ){
                memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
            }
            else if( answer->count == 1 ){
                answer->count = 0;
            }
        }   // if( strlen(cPaket) > 21 )
    }   // for(cPaket = strtok_r(parcel

    logging("terminal_decode[%s:%d]: %d packets readed, %d records created", worker->listener->name, worker->listener->port, packet_num, answer->count);
}
//------------------------------------------------------------------------------


// G79W Protocol V1.5.pdf, page 15     946674000
static void terminal_decode_bin(char *parcel, int parcel_size, ST_ANSWER *answer, ST_WORKER *worker)
{
    ST_RECORD *record = NULL;
    int p_start, p_stop;    // part of parcel (ont record)
    int record_ok = 1;
    #pragma pack( push, 1 )
    typedef struct {
        uint8_t packet_head;
        uint8_t proto_version;
        uint8_t packet_type;
        char device_id[7];
        char data_mask[2];
        uint8_t event_id;
        char date_time[4];
        uint16_t dummy; // I dont know what is it
    } ST_HEADER;
    #pragma pack( pop )
    ST_HEADER *st_header;
    int data_mask;

    struct tm tm_data;
    time_t ulliTmp;

    if( !parcel || parcel_size <= 0 || !answer )
        return;

    logging("terminal_decode[%s:%d]: terminal_decode_bin", worker->listener->name, worker->listener->port);
    logging("terminal_decode[%s:%d]: parcel_size=%d", worker->listener->name, worker->listener->port, parcel_size);

    answer->count = 0;
    p_stop = -1;
    while(p_stop < parcel_size) {
        // seek packet's start
        for(p_start = p_stop + 1; p_start < parcel_size; p_start++){
            if( (uint8_t)parcel[p_start] == 0xf8 && (uint8_t)parcel[p_start + 1] != 0xf8 ){
                break;
            }
        }
        // seek packet's end
        for(p_stop = p_start + 1; p_stop < parcel_size; p_stop++){
            if( (uint8_t)parcel[p_stop] == 0xf8 ){
                break;
            }
        }
        // test for correct packet
        if( p_start < p_stop && p_stop < parcel_size ){
            if( record_ok > 0 && answer->count < MAX_RECORDS - 1 ){
            	answer->count++;
                record_ok = 0;
            }
            record = &answer->records[answer->count - 1];
        }
        else {
            break;
        }

        logging("terminal_decode[%s:%d]: packet %d, %d-%d", worker->listener->name, worker->listener->port, answer->count, p_start, p_stop);

        st_header = (ST_HEADER *)&parcel[p_start];
        logging("terminal_decode[%s:%d]: packet type=%d", worker->listener->name, worker->listener->port, st_header->packet_type);

		snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", st_header->proto_version);
		snprintf(record->imei, SIZE_TRACKER_FIELD, "%lld", hex2dec(st_header->device_id, 7));
        logging("terminal_decode[%s:%d]: record->soft=%s", worker->listener->name, worker->listener->port, record->soft);
        logging("terminal_decode[%s:%d]: record->imei=%s", worker->listener->name, worker->listener->port, record->imei);

        if( st_header->packet_type == 0 ){
            // heaqrtbeat (IMEI only)
            continue;
        }

        ulliTmp = hex2dec(st_header->date_time, 4) + GMT_diff;	// UTC ->local simple (timestamp, seconds);
    	gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
    	tm_data.tm_year = (tm_data.tm_year + 2000 - 1970);
        logging("terminal_decode[%s:%d]: time=%02d.%02d.%02d %02d:%02d:%02d", worker->listener->name, worker->listener->port,
                                            tm_data.tm_mday, tm_data.tm_mon + 1, tm_data.tm_year + 1900, tm_data.tm_hour, tm_data.tm_min, tm_data.tm_sec);

    	// получаем время как число секунд от начала суток
    	record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
    	// в tm_data обнуляем время
    	tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
    	// получаем дату
    	record->data = timegm(&tm_data);	// local struct->local simple & mktime epoch

        data_mask = hex2dec(st_header->data_mask, 2);
        logging("terminal_decode[%s:%d]: data_mask=%d", worker->listener->name, worker->listener->port, data_mask);

        // ставим указатель на Data field
        p_start += sizeof(ST_HEADER);
        // data_mask, биты определяют наличие полей в Data field:
        //   1    2    3    8    16  32   64   128  256  512
        //   0    1    2    3    4    5    6    7    8    9
        // <SYS><GPS><GSM><COT><ADC><DTT><IWD><ETD><OBD><FUL>
        for(int i = 1; i < 1024; i=(i<<1)){
            logging("terminal_decode[%s:%d]: i=%d p_start=%d (0x%.2X), parcel[p_start]=0x%.2X", worker->listener->name, worker->listener->port, i, p_start, p_start, parcel[p_start]);
            switch( i & data_mask ){
                case 1: // SYS
                    p_start += decodeSYS(record, &parcel[p_start]);

                    if( worker && worker->listener->log_all ) {
                        logging("terminal_decode[%s:%d]: record->tracker=%s", worker->listener->name, worker->listener->port, record->tracker);
                        logging("terminal_decode[%s:%d]: record->soft=%s", worker->listener->name, worker->listener->port, record->soft);
                        logging("terminal_decode[%s:%d]: record->hard=%s", worker->listener->name, worker->listener->port, record->hard);
                    }

                    break;
                case 2: // GPS
                    p_start += decodeGPS(record, &parcel[p_start]);

                    if( worker && worker->listener->log_all ) {
                        logging("terminal_decode[%s:%d]: record->satellites=%d", worker->listener->name, worker->listener->port, record->satellites);
                        logging("terminal_decode[%s:%d]: record->valid=%d", worker->listener->name, worker->listener->port, record->valid);
                        logging("terminal_decode[%s:%d]: record->lat=%lf %c", worker->listener->name, worker->listener->port, record->lat, record->clat);
                        logging("terminal_decode[%s:%d]: record->lon=%lf %c", worker->listener->name, worker->listener->port, record->lon, record->clon);
                        logging("terminal_decode[%s:%d]: record->speed=%lf", worker->listener->name, worker->listener->port, record->speed);
                        logging("terminal_decode[%s:%d]: record->curs=%d", worker->listener->name, worker->listener->port, record->curs);
                        logging("terminal_decode[%s:%d]: record->height=%d", worker->listener->name, worker->listener->port, record->height);
                        logging("terminal_decode[%s:%d]: record->hdop=%d\n", worker->listener->name, worker->listener->port, record->hdop);
                    }

                    break;
                case 4: // GSM
                    break;
                case 8: // COT
                    break;
                case 16: // ADC
                    break;
                case 32: // DTT
                    break;
                case 64: // IWD
                    break;
                case 128: // ETD
                    break;
                case 256: // OBD
                    break;
                case 512: // FUL
                    break;
            }   // switch( i & data_mask )
        }
    }   // while(p_stop < parcel_size)

    if( record_ok == 0 && answer->count == 1 ){
        answer->count = 0;  // debug only
    }

    logging("terminal_decode[%s:%d]: %d records created", worker->listener->name, worker->listener->port, answer->count);
}   // terminal_decode_bin


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


static int decodeGPS(ST_RECORD *record, char *chank){
    int index = 0;
    int chank_length = (int)chank[index++]; // not include first byte (size of chank)
    //logging("decodeGPS, chank[0]=0x%.2X, chank[1]=0x%.2X, chank_length=%d", chank[0], chank[1], chank_length);

    #pragma pack( push, 1 )
    typedef struct {
        uint16_t subdata_mask;
        uint8_t flag;   // Bit0-bit4: Valid satellite number; Bit5-Bit6: GPS fix flag
        char lat[4];    // =/ 1000000
        char lon[4];    // =/ 1000000
        char speed[2];  // km/h
        char azimuth[2];
        char altitude[2];   // range is “-9999 to +9999”, unit is “meter”
        char hdop[2];  // =/ 100
        char vdop[2];  // =/ 100
    } ST_GPS;
    #pragma pack( pop )

    ST_GPS *pGPS = (ST_GPS *)&chank[index];

    record->satellites = pGPS->flag & 31;  // 00011111
    record->valid = (pGPS->flag & 96       /* 01100000 */) > 0;
    record->lat = (double)hex2dec(pGPS->lat, 4) / 1000000.0;
    record->clat = record->lat > 0.0 ? 'N' : 'S';
    record->lon = (double)hex2dec(pGPS->lon, 4) / 1000000.0;
    record->clon = record->lon > 0.0 ? 'E' : 'W';
    record->speed = hex2dec(pGPS->speed, 2);
    record->curs = hex2dec(pGPS->azimuth, 2);
    record->height = hex2dec(pGPS->altitude, 2);
    record->hdop = Round((double)hex2dec(pGPS->hdop, 2) / 100.0, 0);

    return chank_length + 1;    // include first byte (size of chank)
}   // decodeGPS


static int decodeSYS(ST_RECORD *record, char *chank){
    int index = 0;
    int chank_length = (int)chank[index++]; // not include first byte (size of chank)
    uint8_t type_length, type, length;
    //logging("decodeSYS, chank[0]=0x%.2X, chank[1]=0x%.2X, chank_length=%d", chank[0], chank[1], chank_length);

    while( index < chank_length ){
        type_length = (uint8_t)chank[index++];
        type = (type_length & 240 /* 11110000 */) >> 4;
        length = type_length & 15;  // 00001111

        switch( type ){
            case 0: // Device name
        		snprintf(record->tracker, min(length + 1, SIZE_TRACKER_FIELD), "%s", &chank[index]);
                break;
            case 1: // Firmware version
        		snprintf(record->soft, min(length + 1, SIZE_TRACKER_FIELD), "%s", &chank[index]);
                break;
            case 2: // Hardware version
        		snprintf(record->hard, min(length + 1, SIZE_TRACKER_FIELD), "%s", &chank[index]);
        }   // swith( type )

        index += length;
    }   // while( index < chank_length )

    return chank_length + 1;    // include first byte (size of chank)
}   // decodeSYS


/*
Convert hexadecimal to decimal
*/
static long long int hex2dec(char *c, size_t size)
{
    long long int retval = 0LL;
    size_t i, j;
    char temp[STRLEN];

    if( size < (STRLEN >> 1) ){
        memset(temp, 0, STRLEN);
        for(i = 0, j = 0; i < size; i++, j += 2){
            sprintf(&temp[j], "%.2X", (uint8_t)c[i]);
        }
        retval = strtoll(temp, NULL, 16);
    }

    return retval;
}   // hex2dec
