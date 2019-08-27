/*
   galileo.c
   shared library for decode/encode gps/glonass terminal Galileo messages

   help:
   http://7gis.ru/assets/files/docs/manuals_ru/opisanie-protokola-obmena-s-serverom.pdf

   compile:
   make -B galileo
*/

#include "glonassd.h"
#include "de.h"     // ST_ANSWER, ST_RECORD
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"

// Структура команды:
#pragma pack( push, 1 )		// https://gcc.gnu.org/onlinedocs/gcc-4.4.2/gcc/Structure_002dPacking-Pragmas.html#Structure_002dPacking-Pragmas
typedef struct {
	unsigned char   Type;    // всегда 0x01
	unsigned short  Len; 		// длина полезной нагрузки. CRC - не входит. И первые 3 байта не входят.
	unsigned char   T03;     // Тег3 (число 3)
	unsigned char   IMEI[15];
	unsigned char   T04;     // Тег4 (число 4)
	unsigned short  ID;
	unsigned char   TE0;     // Тег 0xE0
	unsigned int    NumQ;    // Номер запроса (число вставляемое сервером. В ответ устройство повторяет это поле )
	unsigned char   TE1;     // Тег 0xE1
	unsigned char   SLen;    // Длина строки с командой
	// Текст команды в ASCII
	// unsigned short crc;  // После строки стоит CRC в бинарном виде
} ST_COMMAND;
#pragma pack( pop )

// Ответная структура имеет точно такой же формат.
// Все поля при работе с командами обязательны!
// Верификация команды проходит, если совпадает тег IMEI либо тег ID.
// Верификация правильности тегов, данных и CRC осуществляется тоже.

/*
   массив длин тегов (в байтах)
   я идиот, что скопировал эту идею. Переделывать уже лень.
*/
const int tag_len[]= {
	-1,   //                                 															0
	1,		// Версия железа
	1,		// Версия прошивки
	15,		// IMEI                            															3
	2,		// Идентификатор устройства
	-1, -1, -1, -1, -1, -1, -1, -1,	-1,	-1,	-1,
	2,		// Номер записи в архиве           															16
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	4,		// Дата и время                    															32
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	9,		// Координаты в градусах                                        48
	-1,	-1,
	4,		// Скорость в км/ч и направление в градусах                     51
	2,		// Высота, м                                                    52
	1,		// Одно из значений: 1. HDOP, если источник координат ГЛОНАСС/GPS модуль. 2. Погрешность в метрах, если источник базовые станции GSM-сети.
	-1,	-1,
	2,    // Status of outs 2 bytes (old version)                         56
	2,    // Status of inputs 2 bytes (old version)                       57
	-1,	-1,	-1,	-1,	-1,	-1,
	2,		// Статус устройства                                            64
	2,		// Напряжение питания, мВ                                       65
	2,		// Напряжение аккумулятора, мВ                                  66
	1,		// Температура терминала, С
	4,		// Ускорение
	2,		// Статус выходов                                               69
	2,		// Статус входов
	4,		// EcoDrive и определение стиля вождения                        71
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	2,		// Значение на входе IN0                                        80
	2,		// Значение на входе IN1
	2,		// Значение на входе IN2
	2,		// Значение на входе IN3
	2,		// Значение на входе IN4
	2,		// Значение на входе IN5
	2,		// Значение на входе IN6
	2,		// Значение на входе IN7                                        87
	2,		// RS232 0                                                      88
	2,		// RS232 1                                                      89
	4,		// Показания счётчика электроэнергии РЭП-500
	1,		// Данные рефрижераторной установки
	68,		// Система контроля давления в шинах PressurePro
	3,		// Данные дозиметра ДБГ -С11Д
	-1,	-1,
	2,		// RS485[0].ДУТ с адресом 0                                     96
	2,		// RS485[1].ДУТ с адресом 1                                     97
	2,		// RS485[2].ДУТ с адресом 2
	3,		// RS485[3].ДУТ с адресом 3
	3,		// RS485[4].ДУТ с адресом 4
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	3,		// RS485[15].ДУТ с адресом 15
	2,		// Идентификатор термометра 0
	2,		// Идентификатор термометра 1
	2,		// Идентификатор термометра 2
	2,		// Идентификатор термометра 3
	2,		// Идентификатор термометра 4
	2,		// Идентификатор термометра 5
	2,		// Идентификатор термометра 6
	2,		// Идентификатор термометра 7
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	3,		// Идентификатор нулевого датчика DS1923
	3,		// Идентификатор первого датчика DS1923
	3,		// Идентификатор второго датчика DS1923
	3,		// Идентификатор третьего датчика DS1923
	3,		// Идентификатор четвёртого датчика DS1923
	3,		// Идентификатор пятого датчика DS1923
	3,		// Идентификатор шестого датчика DS1923
	3,		// Идентификатор седьмого датчика DS1923
	1,		// Расширенные данные RS232
	-1,
	1,		// Температура ДУТ с адресом 0, подключенного к порту RS485
	1,		// Температура ДУТ с адресом 1, подключенного к порту RS485
	1,		// Температура ДУТ с адресом 2, подключенного к порту RS485
	-1,	-1,	-1,
	4,		// Идентификационный номер первого ключа iButton
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	1,		// CAN8BITR15
	1,		// CAN8BITR16
	1,		// CAN8BITR17
	1,		// CAN8BITR18
	1,		// CAN8BITR19
	1,		// CAN8BITR20
	1,		// CAN8BITR21
	1,		// CAN8BITR22
	1,		// CAN8BITR23
	1,		// CAN8BITR24
	1,		// CAN8BITR25
	1,		// CAN8BITR26
	1,		// CAN8BITR27
	1,		// CAN8BITR28
	1,		// CAN8BITR29
	1,		// CAN8BITR30
	2,		// CAN16BITR5
	2,		// CAN16BITR6
	2,		// CAN16BITR7
	2,		// CAN16BITR8
	2,		// CAN16BITR9
	2,		// CAN16BITR10
	2,		// CAN16BITR11
	2,		// CAN16BITR12
	2,		// CAN16BITR13
	2,		// CAN16BITR14
	-1,	-1,	-1,	-1,	-1,	-1,
	4,		// Данные CAN - шины (CAN_A0)
	4,		// Данные CAN -шины (CAN_A1)
	4,		// Данные CAN - шины (CAN_B0)
	4,		// CAN_B1
	1,		// CAN8BITR0
	1,		// CAN8BITR1
	1,		// CAN8BITR2
	1,		// CAN8BITR3
	1,		// CAN8BITR4        200
	1,		// CAN8BITR5
	1,		// CAN8BITR6
	1,		// CAN8BITR7
	1,		// CAN8BITR8
	1,		// CAN8BITR9
	1,		// CAN8BITR10
	1,		// CAN8BITR11
	1,		// CAN8BITR12
	1,		// CAN8BITR13
	1,		// CAN8BITR14          210
	4,		// Идентификационный номер второго ключа iButton
	4,		// Общий пробег по данным GPS / ГЛОНАСС - модулей, м.     212
	1,		// Состояние ключей iButton
	2,		// В зависимости от настроек                              214
	2,		//
	2,		//                                                        216
	2,		//
	2,		//                                                        218
	4,		//
	4,		// 																												220
	4,		//
	4,		//                                                        222
	4,		//
	5,		// ID команды от сервера                                  224
	0,		// ответ на команду от сервера 225
	4,		// Данные пользователя
	-1,	-1,	-1,	-1,	-1,	-1,
	4,		// Данные пользователя
	0,		// Массив данных пользователя (Младший байт–длина массива)   234
	-1,	-1,	-1,	-1,	-1,
	4,		// CAN32BITR5         240
	4,		// CAN32BITR6
	4,		// CAN32BITR7
	4,		// CAN32BITR8
	4,		// CAN32BITR9
	4,		// CAN32BITR10
	4,		// CAN32BITR11
	4,		// CAN32BITR12
	4,		// CAN32BITR13
	4,		// CAN32BITR14				249
	-1,   // 										250
	-1,
	-1,
	-1,
	-1,
	-1    // 										255
};


// function automatically called when library loaded before dlopen() returns
__attribute__ ((constructor)) static void load(void)
{
}
//------------------------------------------------------------------------------

// function automatically called when library unloaded before dlclose() returns
__attribute__ ((constructor)) static void unload(void)
{
}
//------------------------------------------------------------------------------

/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	static char data[SOCKET_BUF_SIZE]= {0};	// буфер для хранения посылки
	static unsigned int part_size = 0;
	static unsigned int packet_len = 0;
	ST_COMMAND *galileo_cmd = NULL;
	ST_RECORD *record = NULL;
	unsigned int tag, i = 0, rec_ok = 0, cur_tag = 0;
	struct tm tm_data = {0};
	time_t ulliTmp = 0;
	void *tpp = NULL;	// for prevent error: dereferencing type-punned pointer will break strict-aliasing rules

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	//log2file("/var/www/locman.org/tmp/gcc/galileo_in_", parcel, parcel_size);
	//---------------------------------------------------------------------
	// функция подготовки структуры декодированных данных
	ST_RECORD *new_record(void) {
		if( answer->count < MAX_RECORDS - 1 )
			++answer->count;
		snprintf(answer->records[answer->count - 1].tracker, SIZE_TRACKER_FIELD, "galileo");
		return &answer->records[answer->count - 1];
	}
	//---------------------

	if( parcel[i] == 1	// маркер начала посылки или Hard Version of terminal
			// и это ещё не конец данных
			&& (i + 3 < parcel_size - 2)
			// +2 байта длинны записи и следующий тег должен быть одним из:
			&& (parcel[i + 3] == 1 ||
				 parcel[i + 3] == 2 ||
				 parcel[i + 3] == 3 ||
				 parcel[i + 3] == 4 ||
				 parcel[i + 3] == 16 ||
				 parcel[i + 3] == 32 ||
				 parcel[i + 3] == 48
				)
	  ) {
		// начало посылки найдено
		// получаем длинну посылки
		packet_len = (32767 & (*(unsigned short int*)&parcel[i+1]));

		// копируем её в буфер
		memset(&data, 0, SOCKET_BUF_SIZE);
		memcpy(&data, parcel, parcel_size);
		part_size = parcel_size;

	}	// if( parcel[i] == 1
	else if( parcel[i] == 2 && parcel_size == 3 ) {
		// respond from server (e.g forwarder)
		return;	// do nothing
	}	// else if( parcel[i] == 2
	else {	// пришла следующая часть посылки

		if( part_size ) {	// есть первая часть посылки
			if(part_size + parcel_size <= SOCKET_BUF_SIZE) {	// add part of the parcel to buffer
				memcpy(&data[part_size], parcel, parcel_size);
				part_size += parcel_size;
			} else {	// full parcel is too long
				memcpy(&data[part_size], parcel, parcel_size - (SOCKET_BUF_SIZE - part_size));
				part_size += (SOCKET_BUF_SIZE - part_size);
			}
		}	// if( part_size )
		else {	// нет первой части посылки, part_size == 0
			if( answer->lastpoint.imei[0] ) {	// есть IMEI
				// попробуем найти записи в посылке
				for(i = 0; i < parcel_size - 2; i++) {
					if( parcel[i] == answer->lastpoint.imei[0]
							&& (i > 3 && parcel[i - 1] == 3)
							&& parcel[i + 1] == answer->lastpoint.imei[1] ) {
						// с большой вероятностью мы нашли imei
						i -= 4;	// имитируем начало пакета: 3 байта заголовка перед тегом 3 (imei)
						packet_len = parcel_size - i;
						if( packet_len > 0 ) {
							memcpy(&data[part_size], &parcel[i], packet_len);
							part_size = packet_len + 10;	//
							break;
						}
					}
				}	// for(i = 0;
			}	// if( answer->lastpoint->imei[0] )
		}	// else if( part_size )

	}	// else if( parcel[i] == 1

	if( part_size - 5 < packet_len ) {	// это только часть всей посылки
		//logging("terminal_decode[galileo]: wait\n");
		return;	// и выходим, будем ждать остаток
	}

	//logging("terminal_decode[galileo]: work\n");
	//logging("terminal_decode[galileo]: CRC=%u\n", *(unsigned short *)&data[part_size-sizeof(unsigned short)]);

	if( packet_len > 0 ) {
		record = new_record();
		i = 3;	// пропускаем 2 байта длинны записи
	} else {
		// да отвяжись ты уже
		answer->answer[0] = 2;	// response code
		// copy CRC of the packet
		memcpy(&answer->answer[1], &parcel[parcel_size - 2], 2);
		answer->size = 3;
		return;
	}

	rec_ok = 0;	// сбрасываем флаг распознанной записи

	while(i < packet_len + 3) {

		tag = data[i];

		if( rec_ok > 1 && tag < 48 && cur_tag > tag ) {	// №№ тегов начали ходить по кругу
			// у этих устройств ID не обязателен в любой из записей
			// и если его нет, заполним
			if( !strlen(record->imei) && strlen(answer->lastpoint.imei) )
				strcpy(record->imei, answer->lastpoint.imei);

			record = new_record();
			rec_ok = 0;
		}

		switch(tag) {
		case 1:	// Hard Version of terminal

			snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", (int)data[i+1]);

			i += (1 + tag_len[tag]);	// следующий тег
			cur_tag = tag;

			break;
		case 2:	// Soft Version

			snprintf(record->soft, SIZE_TRACKER_FIELD, "%d", (int)data[i+1]);

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 3:	// IMEY

			// 1
			rec_ok = (snprintf(record->imei, SIZE_TRACKER_FIELD, "%.15s", &data[i+1]) == 15);
			if( rec_ok && strcmp(answer->lastpoint.imei, record->imei) != 0 )
				strcpy(answer->lastpoint.imei, record->imei);

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 4:	// ID

			if( !strlen(record->imei) && !strlen(answer->lastpoint.imei) ) {
				tpp = &data[i+1];	// prevent error: dereferencing type-punned pointer will break strict-aliasing rules
				// 1
				rec_ok = (snprintf(record->imei, SIZE_TRACKER_FIELD, "%d", *(unsigned short *)tpp) > 0);
				if( rec_ok )
					strcpy(answer->lastpoint.imei, record->imei);
			}

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 16:	// Номер записи в архиве

			tpp = &data[i+1];
			record->recnum = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 32:	// TimeDate

			// получаем локальное время (localtime_r is thread-safe)
			tpp = &data[i+1];
			ulliTmp = *(unsigned int *)tpp; // UTC time simple
			ulliTmp += GMT_diff;	// UTC -> local time simple
			gmtime_r(&ulliTmp, &tm_data);           // local time simple->local time as struct tm
			// получаем время как число секунд от начала суток
			record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
            /*
            В ночь на 07.04.19 обнулились счетчики дат в системе GPS. Старое оборудование свихнулось.
            Скорректировать надо только дату, время правильное.
            */
            if( tm_data.tm_year == 100 || tm_data.tm_year == 147 || tm_data.tm_year == 199 ){
                ulliTmp = time(NULL) + GMT_diff;
                gmtime_r(&ulliTmp, &tm_data);
            }
			// в tm_data обнуляем время
			tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
			// получаем дату
			record->data = timegm(&tm_data) - GMT_diff;	// local time as struct->local simple & mktime epoch

			++rec_ok;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 48:	// Спутники, валидность, Координаты

			record->satellites = data[i+1] & 15;
			record->valid = (data[i+1] >> 4) & 15;
			if( record->valid == 0 || record->valid == 2 )
				record->valid = 1;
			else
				record->valid = 0;

			tpp = &data[i+2];
			record->lat = 0.000001 * (*(int *)tpp);
			if( record->lat < 0.0 ) {
				record->lat = fabs(record->lat);
				record->clat = 'S';
			} else
				record->clat = 'N';

			tpp = &data[i+6];
			record->lon = 0.000001 * (*(int *)tpp);
			if( record->lon < 0.0 ) {
				record->lon = fabs(record->lon);
				record->clon = 'W';
			} else
				record->clon = 'E';

			++rec_ok;	// 3
			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 51:	// Speed(km/h) - 2 bytes; Course(deg) - 2 bytes

			tpp = &data[i+1];
			record->speed = 0.1 * (*(unsigned short *)tpp);
			tpp = &data[i+3];
			record->curs = (*(unsigned short *)tpp) / 10;

			++rec_ok;	// 4
			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 52:	// Нeight

			tpp = &data[i+1];
			record->height = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 53:	// HDOP

			record->hdop = 0.1 * ((unsigned char)data[i+1]);

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 56:	// Status of outs 2 bytes (old version)
		case 69:	// Status of outs 2 bytes (new version)

			tpp = &data[i+1];
			record->outputs = *(unsigned short *)tpp;
			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 57:	// Status of inputs 2 bytes (old version)
		case 70:	// Status of inputs 2 bytes (new version)

			tpp = &data[i+1];
			record->inputs = *(unsigned short *)tpp;
			record->ainputs[0] = record->inputs & 1;  //in0 > 0 SOS
			record->ainputs[1] = record->inputs & 2;  //in1 > 0 зажигание
			record->ainputs[2] = record->inputs & 4;  //in2 > 0 запрос связи
			record->ainputs[3] = record->inputs & 8;  //in3 > 0 двери

			record->alarm = record->ainputs[0];
			record->zaj = record->ainputs[1];

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 64:	// Status of device

			tpp = &data[i+1];
			record->status = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 65:	// Напряжение питания, мВ

			tpp = &data[i+1];
			record->vbort = 0.001 * (*(unsigned short *)tpp);

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 66:	// Напряжение аккумулятора, мВ

			tpp = &data[i+1];
			record->vbatt = 0.001 * (*(unsigned short *)tpp);

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 67:	// Температура терминала

			record->temperature = (int)data[i+1];

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 80:	// IN0  SOS

			tpp = &data[i+1];
			record->ainputs[0] = *(unsigned short *)tpp;
			record->alarm = record->ainputs[0] != 0;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 81:	// IN1  зажигание

			tpp = &data[i+1];
			record->ainputs[1] = *(unsigned short *)tpp;
			record->zaj = record->ainputs[1] != 0;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 82:	// IN2   запрос связи

			tpp = &data[i+1];
			record->ainputs[2] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 83:	// IN3  Аналогово-цифровой ДУТ или датчик дверей

			tpp = &data[i+1];
			record->ainputs[3] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 84:	// IN4

			tpp = &data[i+1];
			record->ainputs[4] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 85:	// IN5

			tpp = &data[i+1];
			record->ainputs[5] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 86:	// IN6

			tpp = &data[i+1];
			record->ainputs[6] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 87:	// IN7

			tpp = &data[i+1];
			record->ainputs[7] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 88:	// RS232 0
		case 96:	// RS485[0]. ДУТ с адресом 0

			tpp = &data[i+1];
			record->fuel[0] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 89:	// RS232 1
		case 97:	// RS485[0]. ДУТ с адресом 1

			tpp = &data[i+1];
			record->fuel[1] = *(unsigned short *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 212:	// Общий пробег по данным GPS / ГЛОНАСС - модулей, м

			tpp = &data[i+1];
			record->probeg = *(unsigned int *)tpp;

			i += (1 + tag_len[tag]);
			cur_tag = tag;

			break;
		case 225:	// ответ на команду от сервера

			galileo_cmd = (ST_COMMAND *)&data[i+1];

			i += sizeof(ST_COMMAND);
			if( galileo_cmd->SLen > 0 )
				i += (galileo_cmd->SLen + 2);

			cur_tag = tag;

			break;
		case 234:	// Массив данных пользователя (Младший байт–длина массива)

			i += ((unsigned int)data[i+1]);
			cur_tag = tag;

			break;
		default:	// не обрабатываемый или неизвестный тег

			i = packet_len + 3;	// break cycle

		}	// switch(tag)

	}	// while(i < packet_len + 3)


	// response
	if( answer->count || rec_ok ) {
		answer->answer[0] = 2;	// response code
		// copy CRC of the packet
		memcpy(&answer->answer[1], &data[3 + packet_len], 2);
		answer->size = 3;

		if( record )
			memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
	}	// if( answer->count || rec_ok )

	part_size = packet_len = 0;
	memset(&data, 0, SOCKET_BUF_SIZE);
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
