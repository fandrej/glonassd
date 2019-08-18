/*
   satlite.c
   shared library for decode/encode gps/glonass terminal SAT-LITE/SAT-LITE2 messages

   help:
   http://satsol.ru/equipment/navigation-terminals/sat-lite-2/

   compile:
   make -B satlite
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include <stdint.h> /* uint8_t, etc... */
#include <sys/time.h>
#include "glonassd.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"


// functions
static void satlite_decode_txt(char *parcel, int parcel_size, ST_ANSWER *answer);
static void satlite_decode_bin(char *parcel, int parcel_size, ST_ANSWER *answer);
static uint16_t crc16(unsigned char *pcBlock, uint16_t len);

/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	//log2file("/var/www/locman.org/tmp/gcc/satlite_", parcel, parcel_size);

	if( (parcel[1] == 'A' && parcel[2] == 'V') || (parcel[1] == 'G' && parcel[2] == 'S') )
		satlite_decode_txt(parcel, parcel_size, answer);
	else
		satlite_decode_bin(parcel, parcel_size, answer);

}   // terminal_decode
//------------------------------------------------------------------------------

// текстовый протокол satlite/SAT-LITE2
static void satlite_decode_txt(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	ST_RECORD *record = NULL;
	char cVers[10], cImei[16], cArchive[10], cTime[10], cDate[10], cAltitude[10], cGeoidheight[10],
		  cXCOORD[12], cYCOORD[12];
	char *cRec;
	int iTemp, iFields = 0;
	int iLinesOK = 0;	// успешно обработанных строк
	int iHard = 0;
	int iSerial, iVIN, iVBAT, iFSDATA, iISSTOP, iISEGNITION, iD_STATE, iFREQ1, iCOUNT1, iFIXTYPE,
		 iSATCOUNNT, iFREQ2, iCOUNT2, iADC1, iCOUNTER3, iTS_TEMP, iANT_STATE;
	double dTemp, dSPEED = 0.0, dCOURSE = 0.0;
	struct tm tm_data;
	time_t ulliTmp;


	cRec = strtok(parcel, "\r\n");
	while( cRec ) {

		iSerial = iVIN = iVBAT = iFSDATA = iISSTOP = iISEGNITION = iD_STATE = iFREQ1 = iCOUNT1 = iFIXTYPE =
														  iSATCOUNNT = iFREQ2 = iCOUNT2 = iADC1 = iCOUNTER3 = iTS_TEMP = iANT_STATE = 0;

		memset(cTime, 0, 10);
		memset(cDate, 0, 10);
		memset(cAltitude, 0, 10);
		memset(cXCOORD, 0, 12);
		memset(cYCOORD, 0, 12);

		switch( cRec[5] ) {
		case '2':	// $AV,V2 - Основное сообщение GPSLite V2

			iHard = 2;
			if( cRec[6] == ',' ) {
				//                            1     2    3  4  5  6  7  8  9 10 11 12 13  14     15    16   17  18   19    20
				iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%[^,],%[^,],%[^,],%lf,%lf,%[^,],%[^*]",
									  //        1       2       3       4       5        6         7         8            9
									  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
									  //				10				 11	       12          13        14      15      16        17       18
									  &iFREQ1, &iCOUNT1, &iFIXTYPE, &iSATCOUNNT, cTime, cXCOORD, cYCOORD, &dSPEED, &dCOURSE,
									  //        19      20
									  cDate, cArchive);
			} else {	// $AV,V2DI СЛУЖЕБНОЕ сообщение, Нет необходимости производить разбор
				iFields = 0;
				++iLinesOK;	// успешно обработанных строк
			}

			break;
		case '3':	// $AV,V3 - Основное сообщение GPSLite V3

			iHard = 3;
			if( cRec[6] == ',' ) {
				//    1    2    3    4   5  6  7 8  9  10  12  14    16       17         18      19  20    21   22 23  24   25
				//$AV,V3,71186,751,1107,381,-1,0,1,192,0,0,0,0,0,2,142302,5827.9176N,03049.9828E,0.0,0.0,120613,10,0,32767,*74
				//$AV,V3,71098,16926,16,415,-1,0,0,192,0,0,0,0,0,0,,0,0,32767,,SF*60
				//                                                17         21

				//                             1    2    3  4  5  6  7  8  9 10 11 12 13 14 15  16     17    18   19  20   21  22 23 24   25
				iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%[^,],%[^,],%[^,],%lf,%lf,%[^,],%d,%d,%d,%[^*]",
									  //         1     2        3        4      5       6          7           8            9
									  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
									  //         10        11       12       13       14         15         16      17       18
									  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iFIXTYPE, &iSATCOUNNT, cTime, cXCOORD, cYCOORD,
									  //         19      20       21      22       23          24        25
									  &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);

			} else {
				iFields = 0;
				++iLinesOK;	// успешно обработанных строк
			}

			break;
		case '4':	// $AV,V4 - сновное сообщение GPSLite V4

			iHard = 4;
			if( cRec[6] == ',' ) {
				// отличается от V3: после 14 поля вставлены ещё 2 (15,16)
				if( strstr(cRec, ",,,") ) {
					//    1    2     3    4   5   6 7 8  9 10  12  14 15      18      19         20      21  22   23   24    26
					//$AV,V4,71169,8430,1210,416,-1,1,1,192,0,0,0,0,0,0,,,000026,5959.4886N,03014.9790E,0.0,0.0,060180,10,0,32767,*7C
					//$AV,V4,84542,25532,1237,424,-1,1,1,192,0,0,0,0,0,0,,,233958,4333.4757N,03946.4132E,0.0,0.0,230816,10,0,32767,*79
					//                            1     2    3  4  5  6  7  8  9 10 11 12 13 14 15     18    19    20   21  22   23  24 25 26
					iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,,,%[^,],%[^,],%[^,],%lf,%lf,%[^,],%d,%d,%d,%[^*]",
										  //1      2       3         4      5        6         7          8            9
										  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
										  // 10       11       12         13         14         15          16         17
										  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iFIXTYPE, &iSATCOUNNT,
										  //18      19       20       21       22       23     24        25          26        27
										  cTime, cXCOORD, cYCOORD, &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);
				} else {
					//    1    2     3    4   5   6 7 8  9 10  12  14 15      18      19         20      21  22   23   24    26
					//$AV,V4,71169,8430,1210,416,-1,1,1,192,0,0,0,0,0,0,,,000026,5959.4886N,03014.9790E,0.0,0.0,060180,10,0,32767,*7C
					//$AV,V4,84542,25532,1237,424,-1,1,1,192,0,0,0,0,0,0,,,233958,4333.4757N,03946.4132E,0.0,0.0,230816,10,0,32767,*79
					//                            1     2    3  4  5  6  7  8  9 10 11 12 13 14 15   16    17    18    19    20   21  22   23  24 25 26
					iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%[^,],%[^,],%[^,],%[^,],%[^,],%lf,%lf,%[^,],%d,%d,%d,%[^*]",
										  // 1      2       3         4      5        6         7          8            9
										  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
										  //  10       11       12         13         14         15          16         17
										  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iFIXTYPE, &iSATCOUNNT, cAltitude, cGeoidheight,
										  // 18      19       20       21       22       23     24        25          26        27
										  cTime, cXCOORD, cYCOORD, &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);
				}

			}	// if( cRec[6] == ',' )
			else {
				iFields = 0;
				++iLinesOK;	// успешно обработанных строк
			}

			break;
		case '5':	// $AV,V5 - Основное сообщение GPSLite V5, $AV,V5SD - Дополнительное сообщение GPSLite V5

			iHard = 5;
			// отличается от V4: после 13 поля вставлено поле iANT_STATE
			if( cRec[6] == 'S' ) {	// $AV,V5SD - Дополнительное сообщение GPSLite V5
				iFields = 0;
				++iLinesOK;	// успешно обработанных строк
			} else {
				if( strstr(cRec, ",,,") ) {
					//     1   2      3    4   5  6  7 8  9 10  12  14   16     17       18        19        20    21     22   23    25   26
					//$AV,V5,206804,2046,1312,422,-1,0,1,192,0,0,0,0,0,2,18,,,034906,5526.6511N,06526.8890E,6.60,199.59,240816,36,0,32767,*59
					//                            1     2    3  4  5  6  7  8  9 10 11 12 13 14 15 16     17    18    19   20  21   22  23 24 25
					iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,,,%[^,],%[^,],%[^,],%lf,%lf,%[^,],%d,%d,%d,%[^*]",
										  //        1      2        3       4       5        6         7          8            9
										  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
										  //         10       11        12       13          14          15          16
										  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iANT_STATE, &iFIXTYPE, &iSATCOUNNT,
										  //        17      18       19       20        21      22      23       24          25        26
										  cTime, cXCOORD, cYCOORD, &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);

					if( iFields < 20 ) {
						//     1   2      3    4    5  6  7 8  9 10  12  14  16     17       18  19    20   21 22  23   24
						//$AV,V5,206664,15285,1396,418,-1,0,1,192,0,0,0,0,1,0,0,,,075516,,,0.00,0.00,230816,49,0,32767,*6D
						//                            1     2    3  4  5  6  7  8  9 10 11 12 13 14 15 16     17     18  19   20  21 22 23  24
						iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,,,%[^,],,,%lf,%lf,%[^,],%d,%d,%d,%[^*]",
											  //        1      2        3       4       5        6         7          8            9
											  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
											  //         10       11        12       13          14          15          16
											  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iANT_STATE, &iFIXTYPE, &iSATCOUNNT,
											  //        17      18       19       20      21      22           23       24
											  cTime, &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);
					}

				}	// if( strstr(cRec, ",,,") )
				else {
					//$AV,V5,203747,44942,1199,424,-1,0,1,192,0,0,0,0,0,1,20,74.1,-19.2,160110,5527.2197N,06521.0973E,0.00,0.00,120215,25,0,32767,*41
					iFields = sscanf(&cRec[4], "%[^,],%[^,],%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%[^,],%[^,],%[^,],%[^,],%[^,],%lf,%lf,%[^,],%d,%d,%d,%[^*]",
										  cVers, cImei, &iSerial, &iVIN, &iVBAT, &iFSDATA, &iISSTOP, &iISEGNITION, &iD_STATE,
										  &iFREQ1, &iCOUNT1, &iFREQ2, &iCOUNT2, &iANT_STATE, &iFIXTYPE, &iSATCOUNNT, cAltitude, cGeoidheight,
										  cTime, cXCOORD, cYCOORD, &dSPEED, &dCOURSE, cDate, &iADC1, &iCOUNTER3, &iTS_TEMP, cArchive);
				}
			}	// else if( cRec[6] == 'S' )

			break;
		case '6':	// $AV,V6SD - Дополнительное сообщение GPSLite V6
			iFields = 0;
			++iLinesOK;
			iHard = 6;
			break;
		case 'O':	// $GSMCONT,GPRSACK,205553,1,"+RESP:SMSD,78.108.77.230,40101,",,SF*0D - ответ на команду
			iFields = 0;
			++iLinesOK;
			break;
		case 'S':	// $AV,CSPOLL,74711,57601,1385888805,*3F - сообщение для сервера обновлений
			iFields = 0;
			++iLinesOK;	// успешно обработанных строк
			break;
		case 'K':	// RCPTOK - server responce
			return;
		default:
			iFields = 0;
		}	// switch( cRec[5] )

		if( iFields >= 19 && strlen(cDate) == 6 ) {
			++iLinesOK;	// успешно обработанных строк

			if( answer->count < MAX_RECORDS - 1 )
				++answer->count;
			record = &answer->records[answer->count - 1];

			snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", cImei);
			snprintf(record->tracker, SIZE_TRACKER_FIELD, "sat-lite2");
			snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", iHard);
			snprintf(record->soft, SIZE_TRACKER_FIELD, "%s", cVers);

            // переводим время GMT и текстовом формате в местное
            memset(&tm_data, 0, sizeof(struct tm));
            sscanf(cDate, "%2d%2d%2d", &tm_data.tm_mday, &tm_data.tm_mon, &tm_data.tm_year);
            tm_data.tm_year += 100;	// 2 digit year's in tm_data.tm_year since 1900 year
            tm_data.tm_mon--;	// http://www.cplusplus.com/reference/ctime/tm/

            sscanf(cTime, "%2d%2d%2d", &tm_data.tm_hour, &tm_data.tm_min, &tm_data.tm_sec);

            ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
            gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
            // получаем время как число секунд от начала суток
            record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
            // получаем дату
            /*
            В ночь на 07.04.19 обнулились счетчики дат в системе GPS. Старое оборудование свихнулось.
            Некоторое оборудование GPS стало присылать в поле даты значение 220899, что соответствует 22.08.2099.
            Некоторое оборудование GPS стало присылать в поле даты значение 020100, что соответствует 02.01.2000.
            При этом время присылается правильное.
            */
            if( tm_data.tm_year == 199 || tm_data.tm_year == 100 ){
                ulliTmp = time(NULL) + GMT_diff;
                gmtime_r(&ulliTmp, &tm_data);
            }
            // в tm_data обнуляем время
            tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
            record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

			if( cXCOORD[0] ) {
				sscanf(cXCOORD, "%lf%c", &dTemp, &record->clat);
				iTemp = dTemp / 100.0;
				record->lat = (dTemp - iTemp * 100) / 60.0 + iTemp;

				/* :)
					scanf interprete "1.2E" as floating point number where E is precision
					and not store E to variable
				*/
				record->clon = 0;
				iTemp = 11;
				while(iTemp) {
					if(cYCOORD[iTemp] == 'E') {
						record->clon = cYCOORD[iTemp];
						cYCOORD[iTemp] = 0;
						break;
					}
					iTemp--;
				}
				if( record->clon == 'E' )
					sscanf(cYCOORD, "%lf", &dTemp);
				else
					sscanf(cYCOORD, "%lf%c", &dTemp, &record->clon);
				iTemp = dTemp / 100.0;
				record->lon = (dTemp - iTemp * 100) / 60.0 + iTemp;
			}	// if( cXCOORD[0] )
			else {
				record->lat = record->lon = 0;
				record->clat = 'N';
				record->clon = 'E';
			}

			// Курс в градусах
			record->curs = (int)dCOURSE;
			// Скорость в километрах
			record->speed = (int)(dSPEED * MILE);
			// кол-во спутников
			record->satellites = iSATCOUNNT;
			// Высота над уровнем моря, м
			if( strlen(cAltitude) > 0 )
				sscanf(cAltitude, "%i", &record->height);
			// № отметки
			record->recnum = iSerial;
			// батарейка
			record->vbatt = 0.01 * iVBAT;
			// питание
			record->vbort = 0.01 * iVIN;
			// данные с ДУТ Стрела-Е232, целое 0..65535;
			record->fuel[0] = iFSDATA;
			// зажигание
			record->zaj = iISEGNITION;
			record->ainputs[1] = iISEGNITION;
			// валидность данных
			record->valid = (record->lon > 0.0 && record->lat > 0.0 && record->satellites > 2);

			if( iFields >= 24 ) {
				// бит 0: зарезервирован (& 1)
				// бит 1: состояние цифрового входа 3 (1 - активен; 0 - не активен); (& 2)
				// бит 2: состояние цифрового входа 2 (1 - активен; 0 - не активен); (& 4)
				// бит 3: состояние цифрового входа 1 (1 - активен; 0 - не активен); (& 8)
				// бит 4: зарезервирован;
				// бит 5: зарезервирован;
				// бит 6: зарезервирован;
				// бит 7: состояние дискретного выхода 1 (0 - замкнуто, 1 - разомкнуто); (& 128)
				record->ainputs[0] = (iD_STATE & 2);  // SOS  состояние цифрового входа 3
				record->ainputs[2] = (iD_STATE & 4);  // вызов  состояние цифрового входа 2
				record->ainputs[3] = (iD_STATE & 8);  // двери    состояние цифрового входа 1
				record->inputs = iD_STATE;

				// напряжение на аналоговом входе 1. Сотые доли вольта. Максимум - 40в. 1140 = 11,4В
				// фильтруем искуственно
				if( iADC1 > 100) {	// 1 вольт
					record->ainputs[4] = 0.01 * iADC1;
				}

				record->outputs = (iD_STATE & 128);	// состояние дискретного выхода (0 - замкнуто, 1 - разомкнуто)

				switch( iANT_STATE ) {
				case 0:	// антенна подключена
				case 1:	// короткое замыкание в антенне
				case 2: // антенна отсутствует или обрыв антенны
					record->ainputs[5] = iANT_STATE;
					break;
				}

				// температура внешнего датчика температуры, C*2
				record->temperature = iTS_TEMP / 2;
			}	// if( iFields >= 24 )
			else {
				record->ainputs[0] = iD_STATE;  // SOS
			}	// else if( iFields >= 24 )

			record->alarm = record->ainputs[0];
		}	// if( iFields >= 19 && strlen(cDate) == 6 )

		cRec = strtok(NULL, "\r\n");
	}	// while( cRec )
	//----------------------------------------------------------------

	answer->size += sprintf(&answer->answer[answer->size], "%s", "RCPTOK\r\n");
	if( record )
		memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
}   // satlite_decode_txt
//------------------------------------------------------------------------------


// бинарный протокол satlite/SAT-LITE2 (это НЕ EGTS!)
static void satlite_decode_bin(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	ST_RECORD *record = NULL;
	//-----------------------------------------------------

	/*
	   Описание типов данных:
	   GCC/standard C (stdint.h)																				Borland Builder C++
	   uint8_t - 8ми битный целый беззнаковый. 	unsigned char          unsigned __int8
	   int8_t - 8ми битный целый знаковый.       char                   __int8
	   uint16_t - 16ти битный целый беззнаковый. unsigned short int     unsigned __int16
	   int16_t - 16ти битный целый знаковый.     short int              __int16
	   uint32_t - 32х битный целый беззнаковый.  unsigned int           unsigned __int32
	   int32_t - 32х битный целый знаковый.      int                    __int32
	*/

	// Трекер обменивается с сервером пакетами следующей структуры (бинарные контейнеры):
	// выравнивание элементов страктуры на границу 1 байт
#pragma pack( push, 1 )
	typedef struct {
		uint16_t crc;					// контрольная сумма пакета целиком начиная с поля preamble и заканчивая последним байтом содердимого пакета (кроме поля crc);
		uint16_t preamble;		// преамбула. Содердимое всегда 0x8A2C;
		uint32_t tracker_id;	// идентификатор трекера;
		uint16_t data_len; 		// длина поля с данными; Максимальная длина бинарного контейнера - 1400байт, включая заголовок.
	} t_binary_container;
#pragma pack( pop )
	// В ответ сервер должен прислать трекеру пакет такой же структуры и произвольним содержимом.

	// В качестве полезного содержимого бинарного контейнера выступают данные со структурой t_common_data_header за которой следуют полезные данные пакета:
	// sizeof(t_common_data_header) = 21
#pragma pack( push, 1 )
	typedef struct {
		uint16_t crc;          // контрольная сумма пакета, включая блок с данными (кроме поля crc);
		uint16_t serial_id;    // серийный номер пакета
		uint32_t timestamp;    // unixtime пакета (UTC+0)
		uint16_t packet_type;  // тип структуры, которая является полезными данными текущего пакета;
		uint16_t packet_len;   // длина полезных данных;
		uint32_t x_coord;      // широта - в соответсвии с правилами ЕГТС (широта по модулю, градусы/90 * 0xFFFFFFFF и взята целая часть). Если равно 0xffffffff - значит нет фиксации валидных координат;
		uint32_t y_coord;      // долгота - в соответствии с правилапи ЕГТС (долгота по модулю, градусы/180 * 0xFFFFFFFF и взята целая часть). Если равно 0xffffffff - значит нет фиксации валидных координат;
		uint8_t sf;            // Дополнительные флаги пакета - битовая маска:
	} t_common_data_header;  // бит 0 - признак отправки данных из черного ящика (0 - свежесобранные данные, 1 - данные отправлены из черного ящика);
#pragma pack( pop )      // бит 6 - поле LAHS записи EGTS_SR_POS_DATA; 0 - северная широта; 1 - южная широта;
	// бит 7 - поле LOHS записи EGTS_SR_POS_DATA; 0 - восточная долгота; 1 - западная долгота;

	// данные датчиков трекера
	typedef struct {
		uint16_t adc_data; // напряжение на соответсвующем канале АЦП - сотые доли вольта
	} t_adc_data;

	typedef struct {
		uint32_t freq; // частота на соответсвующем канале дискретных датчиков
		uint32_t counter; // состояние (счетчик переключений) соответсвующего дискретного датчика;
	} t_discreete_data;

	typedef struct {
		uint16_t ow_data; // температура соответсвующего канала температурных датчиков, целое со знаком, СОТЫЕ ДОЛИ ГРАДУСА
	} t_ow_data;

	typedef struct {
		uint8_t i_button_id[8]; // ID ключа
		uint32_t i_button_fix_time; // точное время "приложения" ключа. unixtime UTC+0.
	} t_ibutton_data;

	// данные датчиков трекера
#pragma pack( push, 1 )
	typedef struct {
		uint16_t update_reason;        // причина отправки данного сообщения - бинарное поле
		uint16_t d_state;              // текущее состояние дискретных входов
		uint16_t v_bat;                // напряжение на аккумуляторе - сотые доли вольта;
		uint16_t v_in;                 // входное напряжение на трекере - сотые доли вольта
		uint16_t v_5v;                 // напряжение по цепи 5в в трекере - сотые доли вольта
		uint16_t v_1224v;              // напряжение по цепи 12-24в - сотые доли вольта;
		uint16_t pwr_flags;            // текущее состояние трекера - описание в разработке;
		uint16_t acc_x;                // ускорение по оси X - от 0 до 32767. В диапазоне работы акселерометра, который задается переменной ACC_RANGE
		uint16_t acc_y;                // ускорение по оси Y - от 0 до 32767.
		uint16_t acc_z;                // ускорение по оси Z - от 0 до 32767
		uint8_t adc_chan_to_send;      // количество каналов АЦП к отправке - от 0 до 2
		uint8_t discreete_count_to_send;// количество дискретных каналов к отправке - от 0 до 6
		uint8_t ow_count_to_send;      // количество каналов термоментров к отправке - от 0 до 16
		uint8_t ibutton_data_send;     // количество каналов ibutton к отправке - 0 или 1
		t_adc_data (*adc_data)[]; // adc_chan_to_send : данные АЦП. Количество полей зависит от поля adc_chan_to_send. Может отсутсвовать.
		t_discreete_data (*discreete_data)[]; // discreete_count_to_send : данные дискретных датчоков. Количество полей зависит от поля discreete_count_to_send. Может отсутсвовать.
		t_ow_data (*ow_data)[]; // ow_count_to_send : данные температурныз датчиков. Количество полей зависит от поля ow_count_to_send. Может отсутсвовать.
		t_ibutton_data (*ibutton_data)[]; // ibutton_data_send : данные кнопки ibutton. Может отсутствовать
	} t_sensor_data;
#pragma pack( pop )
	t_sensor_data *sensor_data;

	// данные GPS
#pragma pack( push, 1 )
	typedef struct {
		uint16_t v_in; // входное напряжение, сотые доли вольта
		uint16_t v_bat; // напряжение на акумуляторе, сотые доли вольта
		uint16_t fs_data; // данные топливного датчика
		uint8_t mode_acc_stop:4; // состояние остановки по акселерометру. 0 - остановка не зафиксирована; 1 - зафиксирована остановка;
		uint8_t ignition_on:4; // состояние датчика зажигания. 0 - зажигание выключено; 1 - зажигание включено;
		uint8_t d_state; // состояние дискретныз датчиков - см. выше
		uint8_t fix_type; // тип фиксации GPS/GLONASS;
		uint8_t sat_count; // количество отслеживаемых спутников
		uint16_t hdop; // hdop - сотые доли
		uint16_t altitude; // Высота антенны при.мника над/ниже уровня моря, м;
		uint16_t geoid_height; // Геоидальное различие - различие между земным эллипсоидом WGS-84 и уровнем моря(геоидом), "-" = уровень моря ниже эллипсоида.;
		uint16_t speed; // скорость - сотые доли км-ч
		uint16_t azimuth; // вектор перемещения - сотые доли градусов
		int32_t x_coord; // широта - кодирование в соответсвии с правилами ЕГТС; (4294967295 - координата невалидна)
		int32_t y_coord; // долгота - кодирование в соответсвии с правилами ЕГТС; (4294967295 - координата невалидна)
		uint8_t ant_state; // состояние GPS антенны. 0 - OK, 1 - короткое замыкание, 2 - отсутсвует или обрыв.
		uint8_t egts_flags; // флаги ЕГТС для передачи на сервер
		uint8_t egts_src; // Источник данных ЕГТС для передачи на сервер
	} t_gps_data_v4;
#pragma pack( pop )
	t_gps_data_v4 *gps_data_v4;

	// данные топливного датчика
#pragma pack( push, 1 )
	typedef struct {
		uint8_t bind_place; // место подключения датчика (0 - RS232_MAIN; 1 - RS232_AUX; 2 - RS485);
		char t; // температура топлива;
		uint16_t n; // текущий уровень топлива;
		uint16_t f_curr; // текущая частота внутреннего генератора;
		uint32_t id; // ID датчика;
		uint16_t f_max_t; // показания частоты при максимальном уровне топлива;
		uint16_t f_min_t; // показания частоты при минимальном уровне топлива;
		uint16_t k_t; // зарезервировано;
		char k_t0; // зарезервировано;
		uint16_t pwm_max; // зарезервировано;
		uint16_t trl; // зарезервировано;
		char net_addr; // сетевой адрес датчика;
		char net_mode; // режим работы датчика - 0 - одиночный; 1 - сетевой;
		char pwm_mode; // зарезервировано
	} t_dut_e_struct;
#pragma pack( pop )
	t_dut_e_struct *dut_e_struct;

	// данные тревожной кнопки
#pragma pack( push, 1 )
	typedef struct {
		uint16_t gps_speed; // Скорость в момент возникновения тревожного события
		uint16_t gps_angle; // Направление в момент возникновения тревожного события
		uint32_t x_coord; // Широта - милионные доли градуса
		uint32_t y_coord; // Долгота - милионные доли градуса
		uint8_t alarm_state; // 1 - есть тревога; 0 - нет тревоги
	} t_alarm_data_v1;
#pragma pack( pop )
	t_alarm_data_v1 *alarm_data_v1;

	// Тип пакета 0x0040 - информация о GPS-координатах
#pragma pack( push, 1 )
	typedef struct {
		uint16_t v1224; // напряжение на аккумуляторе автомобиля, сотые доли вольта
		uint16_t v_bat;  // напряжение на аккумуляторе трекера - сотые доли вольта;
		uint16_t fs_data;  // данные топливного датчка 1 на канале RS232 или RS485
		uint8_t stop_state;  // 1 если автомобиль стоит на месте; 0 - если едет;
		uint8_t ign_state;  // 1 если зажигание включено; 0 - если зажигание выключено;
		uint8_t d_state;  // состояние дискретных входов - битовая маска;
		uint16_t freq1;  // частота на дискретном входе 1;
		uint16_t c_counter1;  // счетчик импульсов на дискретном входе 1;
		uint16_t freq2;  // частота на дискретом входе 2;
		uint16_t c_counter2;  // счетчик импульсов на дискретном входе 2;
		uint8_t ant_state;  // состояние GPS-антенны;
		uint8_t fix_type;  // тип фиксации спутников;
		uint8_t sat_count;  // количество отслеживаемых спутников;
		uint16_t altitude;  // высота;
		uint16_t geoid_height;  // высота геоида;
		uint32_t x_coord;  // широта в системе шифрования ЕГТС;
		uint32_t y_coord;  // долгота в системе шифрования ЕГТС;
		uint16_t speed;  // сотые км-час;
		uint16_t course;  // азимут, градусы;
		uint16_t adc1;  // данные АЦП1 - слтые доли вольта;
		uint16_t c_counter3;  // счетчик дискретного входа 3;
		int16_t ow_data;  // данные датчика 1-wire N1, сотые доли градуса;
		uint8_t egts_flags;  // флаги ЕГТС;
		uint8_t egts_src;  // источник данных ЕГТС;
	} t_l2b_gps_info;
#pragma pack( pop )
	t_l2b_gps_info *l2b_gps_info;

	// Тип пакета 0x0042 - информация о датчиках
#pragma pack( push, 1 )
	typedef struct {
		uint16_t acc_max_x; // данные акселерометра по оси X
		uint16_t acc_max_y; // данные акселерометра по оси Y
		uint16_t acc_max_z; // данные акселерометра по оси Z
		uint16_t fuel_data[4]; // данные 4х топливных датчиков RS485
		uint32_t uptime_cnt; // время с момента запуска трекера, сек
	} t_l2b_sd_line;
#pragma pack( pop )
	t_l2b_sd_line *l2b_sd_line;

	// Тип пакета 0x0045 - минимальная информация о маршруте
#pragma pack( push, 1 )
	typedef struct {
		uint8_t ant_state; // состояние GPS антенны
		uint8_t fix_type; // тип фиксации спутников
		uint8_t sat_count; // количество спутников в обзоре
		uint16_t speed; // текущая скорость - сотые км-ч
		uint16_t course; // текущий вектор перемещения
		uint8_t egts_flags; // флаги ЕГТС
	} t_l2b_gps_info_min;
#pragma pack( pop )
	t_l2b_gps_info_min *l2b_gps_info_min;

	/*	пока не используется
	   // Тип пакета 0x0008 - текстовая команда устройству из стека команд
	   #pragma pack( push, 1 )
	   typedef struct {
	  	char cmd_id[16]; // ID команды
	    char cmd[160]; // Команда устройству
	   } t_text_cmd_data;
	   #pragma pack( pop )
	   t_text_cmd_data *text_cmd_data;
	*/

	static time_t prevTime = 0;					// prev. packet time
	int i, iBuffPosition;
	struct tm tm_data;
	time_t ulliTmp = 0;
	t_binary_container *binary_container;
	t_common_data_header *common_data_header;

	iBuffPosition = 0;

	// parcel header
	binary_container = (t_binary_container *)&parcel[iBuffPosition];

	if( binary_container->preamble != 0x8A2C	// preamble always 0x8A2C
			|| !binary_container->tracker_id	// IMEI corrupted
			|| !binary_container->data_len		// server responce or data corrupted
	  ) {
		return;	// return for error
	}

	iBuffPosition += sizeof(t_binary_container);

	while( iBuffPosition < binary_container->data_len ) {

		record = &answer->records[answer->count];

		snprintf(record->imei, SIZE_TRACKER_FIELD, "%d", binary_container->tracker_id);
		snprintf(record->tracker, SIZE_TRACKER_FIELD, "sat-lite2");
		//snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", iHard);
		snprintf(record->soft, SIZE_TRACKER_FIELD, "%s", "BIN");

		common_data_header = (t_common_data_header *)&parcel[iBuffPosition];
		iBuffPosition += sizeof(t_common_data_header);

		if( !common_data_header->timestamp ) {
			iBuffPosition += common_data_header->packet_len;
			continue;
		}

		ulliTmp = common_data_header->timestamp + GMT_diff;	// UTC ->local
		gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
		// получаем время как число секунд от начала суток
		record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
		// в tm_data обнуляем время
		tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
		// получаем дату
		record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

		// координаты
		// Если равно 0xffffffff - значит нет фиксации валидных координат
		record->valid = ( common_data_header->x_coord != 0xffffffff && common_data_header->y_coord != 0xffffffff );
		if ( record->valid ) {
			record->lat = 90.0 * common_data_header->x_coord / 0xFFFFFFFF;
			record->lon = 180.0 * common_data_header->y_coord / 0xFFFFFFFF;
		}

		if(common_data_header->sf & 64)
			record->clat = 'S';
		else
			record->clat = 'N';

		if(common_data_header->sf & 128)
			record->clon = 'W';
		else
			record->clon = 'E';

		if( answer->count < MAX_RECORDS - 1 ) {
			if( prevTime != ulliTmp &&
					(record->lat && record->lon) &&
					common_data_header->packet_type != 0x0003 &&
					common_data_header->packet_type != 0x000A &&
					common_data_header->packet_type != 0x0040
			  )
				++answer->count;
		}	// if( answer->count < MAX_RECORDS - 1 )

		switch( common_data_header->packet_type ) {
		case 0xFFFF:	// пустой пакет (для подтверждения приема контйнера). Данных нет, поле packet_len=0
			break;
		case 0x0001:	// содержит в себе данные датчиков трекера:
			sensor_data = (t_sensor_data *)&parcel[iBuffPosition];

			record->inputs = sensor_data->d_state; // текущее состояние дискретных входов
			record->ainputs[3] = (sensor_data->d_state & 1);  // двери // бит 0: состояние цифрового входа 1 (1 - активен; 0 - не активен);
			record->ainputs[2] = (sensor_data->d_state & 2);  // вызов // бит 1: состояние цифрового входа 2 (1 - активен; 0 - не активен);
			record->ainputs[0] = (sensor_data->d_state & 4);  // SOS;	// бит 2: состояние цифрового входа 3 (1 - активен; 0 - не активен);
			record->ainputs[4] = (sensor_data->d_state & 8);	// бит 3: состояние цифрового входа 4 (1 - активен; 0 - не активен);
			record->ainputs[5] = (sensor_data->d_state & 16);	// бит 4: состояние цифрового входа 5 (1 - активен; 0 - не активен);
			record->ainputs[6] = (sensor_data->d_state & 32);	// бит 5: состояние цифрового входа 6 (1 - активен; 0 - не активен);

			record->vbatt = 0.01 * sensor_data->v_bat;	// батарейка - сотые доли вольта
			record->vbort = 0.01 * sensor_data->v_in;		// входное напряжение на трекере - сотые доли вольта;

			record->alarm = record->ainputs[0];

			break;
		case 0x0002:	// содержит в себе данные по GSM каналу
			break;
		case 0x0003:	// данные GPS
			gps_data_v4 = (t_gps_data_v4 *)&parcel[iBuffPosition];

			record->fuel[0] = gps_data_v4->fs_data; // данные топливного датчика
			record->zaj = (gps_data_v4->ignition_on & 16); // состояние датчика зажигания. 0 - зажигание выключено; 1 - зажигание включено;
			record->ainputs[1] = record->zaj;
			record->vbatt = 0.01 * gps_data_v4->v_bat;	// напряжение на аккумуляторе - сотые доли вольта;
			record->vbort = 0.01 * gps_data_v4->v_in;	// входное напряжение на трекере - сотые доли вольта;
			record->inputs = gps_data_v4->d_state; // текущее состояние дискретных входов
			record->satellites = gps_data_v4->sat_count; // количество отслеживаемых спутников
			record->hdop = 0.01 * gps_data_v4->hdop; // hdop - сотые доли
			record->height = gps_data_v4->altitude; // Высота антенны при.мника над/ниже уровня моря, м;
			record->speed = (int)(0.01 * gps_data_v4->speed); // скорость - сотые доли км-ч
			record->curs = (int)(0.01 * gps_data_v4->azimuth); // вектор перемещения - сотые доли градусов
			record->lat = 90.0 * gps_data_v4->x_coord / 0xFFFFFFFF;
			record->lon = 180.0 * gps_data_v4->y_coord / 0xFFFFFFFF;
			record->valid = (record->satellites > 2 && record->lat > 0 && record->lon > 0);

			if( answer->count < MAX_RECORDS - 1 ) {
				if( prevTime != ulliTmp )
					++answer->count;
			}	// if( answer->count < MAX_RECORDS - 1 )

			break;
		case 0x0004:	// данные топливного датчика
			dut_e_struct = (t_dut_e_struct *)&parcel[iBuffPosition];
			record->fuel[0] = dut_e_struct->n; // текущий уровень топлива;

			break;
		case 0x0005:	// данные с CAN шины
			break;
		case 0x000A:	// данные тревожной кнопки
			alarm_data_v1 = (t_alarm_data_v1 *)&parcel[iBuffPosition];

			record->speed = alarm_data_v1->gps_speed; // Скорость в момент возникновения тревожного события
			record->curs = alarm_data_v1->gps_angle; // Направление в момент возникновения тревожного события
			record->lat = 90.0 * alarm_data_v1->x_coord / 0xFFFFFFFF;
			record->lon = 180.0 * alarm_data_v1->y_coord / 0xFFFFFFFF;
			record->alarm = alarm_data_v1->alarm_state;	// 1 - есть тревога; 0 - нет тревоги
			record->ainputs[0] = record->alarm;
			record->valid = (record->lat > 0 && record->lon > 0);

			if( answer->count < MAX_RECORDS - 1 ) {
				if( prevTime != ulliTmp )
					++answer->count;
			}	// if( answer->count < MAX_RECORDS - 1 )

			break;
		case 0x0006:	// запрос блока прошивки
			break;
		case 0x0007:	// блок прошивки
			break;
		case 0x0008:	// текстовая команда устройству из стека команд

			//text_cmd_data = (t_text_cmd_data *)&parcel[iBuffPosition];

			break;
		case 0x0009:	// запрос данных с конфигурационного сервера
			break;
		case 0x000D:	// данные с CANLog (дочерней платы расширения или модуля подключаемого по интерфейсу RS232/485);
			break;
		case 0x0040:	// информация о GPS-координатах
			l2b_gps_info = (t_l2b_gps_info *)&parcel[iBuffPosition];

			record->vbort = 0.01 * l2b_gps_info->v1224;	// напряжение на аккумуляторе автомобиля, сотые доли вольта
			record->vbatt = 0.01 * l2b_gps_info->v_bat;	// напряжение на аккумуляторе - сотые доли вольта;
			record->fuel[0] = l2b_gps_info->fs_data; // данные топливного датчка 1 на канале RS232 или RS485
			record->zaj = l2b_gps_info->ign_state; // 1 если зажигание включено; 0 - если зажигание выключено;
			record->ainputs[1] = record->zaj;
			record->inputs = l2b_gps_info->d_state; // состояние дискретных входов - битовая маска;
			record->satellites = l2b_gps_info->sat_count; // количество отслеживаемых спутников
			record->height = l2b_gps_info->altitude; // Высота антенны при.мника над/ниже уровня моря, м;
			record->lat = 90.0 * l2b_gps_info->x_coord / 0xFFFFFFFF;
			record->lon = 180.0 * l2b_gps_info->y_coord / 0xFFFFFFFF;
			record->speed = (int)(0.01 * l2b_gps_info->speed); // скорость - сотые доли км-ч
			record->curs = (int)l2b_gps_info->course; // азимут, градусы;
			record->valid = (record->satellites > 2 && record->lat > 0 && record->lon > 0);

			if( answer->count < MAX_RECORDS - 1 ) {
				if( prevTime != ulliTmp )
					++answer->count;
			}	// if( answer->count < MAX_RECORDS - 1 )

			break;
		case 0x0041: // информация о GSM-сети
			break;
		case 0x0042: // информация о датчиках
			l2b_sd_line = (t_l2b_sd_line *)&parcel[iBuffPosition];

			for(i = 0; i < 4; i++) {
				if( l2b_sd_line->fuel_data[i] ) {
					if( !record->fuel[0] )
						record->fuel[0] = l2b_sd_line->fuel_data[i]; // данные 4х топливных датчиков RS485
					else if( !record->fuel[1] )
						record->fuel[1] = l2b_sd_line->fuel_data[i];
				}
			}	// for(int i = 0; i < 4; i++)

			break;
		case 0x0043: // отладочная информация
			break;
		case 0x0044: // информация о датчиках 1-wire
			break;
		case 0x0045: // минимальная информация о маршруте
			l2b_gps_info_min = (t_l2b_gps_info_min *)&parcel[iBuffPosition];

			record->satellites = l2b_gps_info_min->sat_count; // количество спутников в обзоре
			record->speed = (int)(0.01 * l2b_gps_info_min->speed);	// текущая скорость - сотые км-ч
			record->curs = (int)l2b_gps_info_min->course; // текущий вектор перемещения

		}	// switch( common_data_header->packet_type )

		// store prev. packet time
		prevTime = ulliTmp;
		// next packet
		iBuffPosition += common_data_header->packet_len;

	}	// while( iBuffPosition < binary_container->data_len )

	//-----------------------------------------------------


	answer->size = sizeof(t_binary_container);
	memcpy(answer->answer, binary_container, sizeof(t_binary_container));
	binary_container = (t_binary_container *)answer->answer;
	binary_container->data_len = 0;
	binary_container->crc = crc16( (unsigned char *)&binary_container->preamble, sizeof(t_binary_container) - sizeof(uint16_t) );

	if( record )
		memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
}   // satlite_decode_bin
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

static uint16_t crc16(unsigned char *pcBlock, uint16_t len)
{
	unsigned short crc = 0xFFFF;
	unsigned char i;

	while(len--) {
		crc ^= *pcBlock++ << 8;
		for(i = 0; i < 8; i++)
			crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
	}	// while(len--)

	return crc;
}
//------------------------------------------------------------------------------
