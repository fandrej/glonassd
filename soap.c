/*
   soap.c
   shared library for decode/encode gps/glonass terminal Wialon NIS/ОлимпСтрой(SOAP) messages

   compile:
   make -B soap
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include "glonassd.h"	/* globals */
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"


/*
   decode function
   parcel - the raw data from socket
   parcel_size - the length of the data
   answer - a pointer to ST_ANSWER structure
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	ST_RECORD *record = NULL;
	char *cRec, cTime[25];
	int iTemp, rec_ok, i, iBufSize;
	struct tm tm_data;
	time_t ulliTmp;
	char cAswerBuf[1281];
	// 178 байт надо на заголовок ответа
	// 281 байт надо на строку ответа на каждую посылку
	// 1460 байт максимальный размер полезных данных стандартного TCP пакета без фрагментации
	iBufSize = 1460 - 178 - 1;	// нулевой байт в конце

	if( !parcel || !parcel_size || !answer )
		return;

	memset(cAswerBuf, 0, iBufSize);

	rec_ok = 1;
	cRec = strtok(parcel, "\r\n");
	while( cRec ) {

		// <ObjectID>01326273</ObjectID>
		if( strstr(cRec, "<ObjectID>") ) {
			if( rec_ok ) {
				if( answer->count < MAX_RECORDS - 1 )
					answer->count++;
				record = &answer->records[answer->count - 1];
				i = 0;
			}	// if( rec_ok )

			snprintf(record->tracker, SIZE_TRACKER_FIELD, "SOAP");
			snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", 1);
			snprintf(record->soft, SIZE_TRACKER_FIELD, "%f", 1.6);

			rec_ok = sscanf(cRec, "<ObjectID>%15[^<]</ObjectID>", record->imei);
		}	// <ObjectID>

		// <Coord time="2015-12-25T04:31:40Z" lon="65.222512" lat="55.403608" alt="0" speed="0.0" dir="0" valid="1" />
		if( rec_ok && strstr(cRec, "<Coord time") ) {

			//               1    2 3   4  5  6           7                8           9         10       11        12
			// <Coord time="2015-12-25T04:31:40Z" lon="65.222512" lat="55.403608" alt="0" speed="0.0" dir="0" valid="1" />
			//                                   1  2  3    4  5  6              7          8          9             10         11           12
			iTemp = sscanf(cRec, "<Coord time=\"%d-%d-%d%*c%d:%d:%d%*c\" lon=\"%lf\" lat=\"%lf\" alt=\"%d\" speed=\"%lf\" dir=\"%d\" valid=\"%d\"",
								&tm_data.tm_year, // 1
								&tm_data.tm_mon, 	// 2
								&tm_data.tm_mday,	// 3
								&tm_data.tm_hour,	// 4
								&tm_data.tm_min,	// 5
								&tm_data.tm_sec,	// 6
								&record->lon,			// 7
								&record->lat,			// 8
								&record->height,	// 9
								&record->speed,		// 10
								&record->curs,		// 11
								&record->valid		// 12
							  );

			rec_ok = (iTemp == 12);
			if( rec_ok ) {
				// переводим время GMT в местное
				// http://www.cplusplus.com/reference/ctime/tm/
				tm_data.tm_year -= 1900;
				tm_data.tm_mon--;

				ulliTmp = timegm(&tm_data) + GMT_diff;	// UTC struct->local simple
				gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
				// получаем время как число секунд от начала суток
				record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
				// в tm_data обнуляем время
				tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
				// получаем дату
				record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

				// индикаторы полушарий тут не присылаются
				record->clon = 'E';
				record->clat = 'N';

				if( iBufSize - strlen(cAswerBuf) > 281 ) {
					answer->size += snprintf(&cAswerBuf[answer->size], iBufSize - answer->size,
													 "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<soapenv:Envelope xmlns:env=\"http://schemas.xmlsoap.org/soap/envelope\">\n<soapenv:Header/>\n<soapenv:Body>\n<ws:PutCoordResponce>\n<ObjectID>%s</ObjectID>\n</ws:PutCoordResponce>\n</soapenv:Body>\n</soapenv:Envelope>\n",
													 record->imei);
				}
			}	// if( rec_ok )

		}	// <Coord time

		// <DigI inpnum="6" />
		if( rec_ok && i < 8 && strstr(cRec, "<DigI inpnum") ) {
			i += sscanf(cRec, "<DigI inpnum=\"%d\" />", &record->ainputs[i]);
		}	// <DigI inpnum

		// <AnalogI num="6" />
		if( rec_ok && i < 8 && strstr(cRec, "<AnalogI num") ) {
			i += sscanf(cRec, "<AnalogI num=\"%d\" />", &record->ainputs[i]);
		}	// <AnalogI num

		cRec = strtok(NULL, "\r\n");
	}	// while( cRec )


	ulliTmp = time(NULL);
	gmtime_r(&ulliTmp, &tm_data);
	strftime(cTime, 24, "%a, %d %b %Y %H:%M:%S", &tm_data);

	snprintf(answer->answer, 1460,
				"HTTP/1.1 200 OK\r\nServer: glonassd/1.0\r\nContent-Type: text/xml;charset=UTF-8\r\nConnection: keep-alive\r\nContent-Length: %d\r\nDate: %s GMT\r\n\r\n%s",
				(int)strlen(cAswerBuf),
				cTime,
				cAswerBuf);
	answer->size = strlen(answer->answer);

	if( answer->count && record ) {
		memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
	}	// if( answer->count )

}   // terminal_decode
//------------------------------------------------------------------------------

/*
   encode function
   records - pointer to array of ST_RECORD struct.
   reccount - number of structs (records) in array (negative if authentificate required)
   buffer - buffer for encoded data
   bufsize - size of the buffer
   return size of data in the buffer for encoded data in bytes
*/
int terminal_encode(ST_RECORD *records, int reccount, char *buffer, int bufsize)
{
	uint i, j, top, itemp, ContentLength, ContentLengthPosition;
	struct tm tm_data;
	time_t ulliTmp;
	char buf[5];

	top = ContentLength = 0;

	if( !records || !reccount || !buffer || !bufsize )
		return top;

	if( reccount < 0 )
		reccount *= -1;

	memset(buffer, 0, bufsize);

	top += sprintf(&buffer[top], "POST / HTTP/1.0\r\n");
	top += sprintf(&buffer[top], "Host: 0.0.0.0:0\r\n");
	top += sprintf(&buffer[top], "User-Agent: glonassd\r\n");
	top += sprintf(&buffer[top], "Accept-Encoding: gzip, deflate\r\n");
	top += sprintf(&buffer[top], "Connection: keep-alive\r\n");
	top += sprintf(&buffer[top], "Content-Length:     \r\n");	// reserving 4 byte for content-length
	ContentLengthPosition = top - 6;	// save position: 4 + \r\n
	top += sprintf(&buffer[top], "Content-Type: text/xml\r\n");
	top += sprintf(&buffer[top], "\r\n");
	ContentLength = top;

	top += sprintf(&buffer[top], "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n");
	top += sprintf(&buffer[top], "<soapenv:Envelope xmlns:env=\"http://schemas.xmlsoap.org/soap/envelope\">\r\n");
	top += sprintf(&buffer[top], "<soapenv:Header/>\r\n");
	top += sprintf(&buffer[top], "<soapenv:Body>\r\n");

	for(i = 0; i < reccount; i++) {

		// get local time from terminal record
		ulliTmp = records[i].data + records[i].time;
		memset(&tm_data, 0, sizeof(struct tm));
		// convert local time to UTC
		gmtime_r(&ulliTmp, &tm_data);


		top += snprintf(&buffer[top], bufsize - top, "<ws:PutCoord>\r\n");
		top += snprintf(&buffer[top], bufsize - top, "<ObjectID>%s</ObjectID>\r\n", records[i].imei);
		top += snprintf(&buffer[top], bufsize - top, "<Coord time=\"%4d-%02d-%02dT%02d:%02d:%02dZ\" lon=\"%3.7lf\" lat=\"%3.7lf\" alt=\"%d\" speed=\"%3.1lf\" dir=\"%d\" valid=\"%d\" />\r\n",
							 tm_data.tm_year + 1900,
							 tm_data.tm_mon + 1,
							 tm_data.tm_mday,
							 tm_data.tm_hour,
							 tm_data.tm_min,
							 tm_data.tm_sec,
							 records[i].lon,
							 records[i].lat,
							 records[i].height,
							 records[i].speed,
							 records[i].curs,
							 records[i].valid);

		// digital/analog inputs/outputs status
		itemp = 1;
		for(j = 0; j < 8; j++) {
			if( records[i].inputs & (itemp << j) )
				top += snprintf(&buffer[top], bufsize - top, "<DigI inpnum=\"%d\" />\r\n", j);

			if( records[i].outputs & (itemp << j) )
				top += snprintf(&buffer[top], bufsize - top, "<DigO outnum=\"%d\" />\r\n", j);

			if( records[i].ainputs[j] )
				top += snprintf(&buffer[top], bufsize - top, "<AnalogI num=\"%d\" val=\"%d\" />\r\n", j, records[i].ainputs[j]);
		}	// for(j = 0; j < 8; j++)

		top += snprintf(&buffer[top], bufsize - top, "</ws:PutCoord>\r\n");

		if( bufsize - top < 350 )
			break;

	}	// for(i = 0; i < reccount; i++)

	if( bufsize - top > 42 ) {
		top += snprintf(&buffer[top], bufsize - top, "</soapenv:Body>\r\n");
		top += snprintf(&buffer[top], bufsize - top, "</soapenv:Envelope>\r\n");
	}

	// insert ContentLength in saved position
	memset(buf, 0, 5);
	snprintf(buf, 5, "%u", top - ContentLength);
	for(i = 0; buf[i]; i++ ) {
		buffer[ContentLengthPosition + i] = buf[i];
	}

	return top;
}   // terminal_encode
//------------------------------------------------------------------------------
