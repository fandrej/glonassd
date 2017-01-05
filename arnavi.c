/*
   arnavi.c
   shared library for decode/encode gps/glonass terminal ARNAVI 4 messages

   help:
   https://docs.google.com/spreadsheets/d/15s-2ZbqOQ1bZvAtFFm9sIEuKy3jbJzxdeynp72sjoYU/edit?usp=sharing

   compile:
   make -B arnavi
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include <stdint.h> /* uint8_t, etc... */
#include <sys/time.h>
//#include <syslog.h>
#include "glonassd.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "logger.h"
#include "arnavi.h"


// functions

/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure (de.h)
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	static __thread uint32_t uiPrevProbeg = 0;
	ARNAVI_HEADER *arnavi_header;
	ARNAVI_RECORD_HEADER *record_header;
	unsigned int iTemp, iDataSize, iDataReaded, iBuffPosition;
	uint32_t uiTemp;
	uint8_t iPackageNumber = 0;
	time_t ulliTmp;
	struct tm tm_data;
	ST_RECORD *record = NULL;

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	iBuffPosition = 0;
	while( iBuffPosition < parcel_size ) {

		switch((uint8_t)parcel[iBuffPosition]) {
		case ARNAVI_ID_HEADER:
			arnavi_header = (ARNAVI_HEADER *)parcel;

			snprintf(answer->lastpoint.imei, SIZE_TRACKER_FIELD, "%lu", arnavi_header->ID);
			snprintf(answer->lastpoint.tracker, SIZE_TRACKER_FIELD, "arnavi");
			snprintf(answer->lastpoint.hard, SIZE_TRACKER_FIELD, "%d", 4);
			snprintf(answer->lastpoint.soft, SIZE_TRACKER_FIELD, "%d", arnavi_header->PV);

			// confirmation HEADER (7B 00 00 7D)
			iTemp = answer->size;
			answer->answer[iTemp] = 0x7B;
			answer->answer[iTemp + 1] = 0;
			answer->answer[iTemp + 2] = 0;
			answer->answer[iTemp + 3] = 0x7D;
			answer->size += 4;

			iBuffPosition += sizeof(ARNAVI_HEADER);
			break;
		case ARNAVI_ID_PACKAGE:

			iPackageNumber = (uint8_t)parcel[iBuffPosition + 1];	//  from 0x01 to 0xFB

			// confirmation PACKAGE id it not answer to server command
			if( iPackageNumber && iPackageNumber != 0xFD ) {
				iTemp = answer->size;
				answer->answer[iTemp] = 0x7B;
				answer->answer[iTemp + 1] = 0;
				answer->answer[iTemp + 2] = iPackageNumber;
				answer->answer[iTemp + 3] = 0x7D;
				answer->size += 4;
			}	// if( iPackageNumber )

			iBuffPosition += 2;	// to record header

		case 1:	// RECORD is a set of fields (Tags) (one or more) having the same time

			record_header = (ARNAVI_RECORD_HEADER *)&parcel[iBuffPosition];
			iDataSize = record_header->SIZE;
			iDataReaded = 0;

			record = &answer->records[answer->count];
			strcpy(record->imei, answer->lastpoint.imei);
			strcpy(record->tracker, answer->lastpoint.tracker);
			strcpy(record->hard, answer->lastpoint.hard);
			strcpy(record->soft, answer->lastpoint.soft);

			ulliTmp = record_header->TIME + GMT_diff;	// UTC ->local
			gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
			// получаем время как число секунд от начала суток
			record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
			// в tm_data обнуляем время
			tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
			// получаем дату
			record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

			iBuffPosition += sizeof(ARNAVI_RECORD_HEADER);

			// fields start ----------------------------------------------------------
			while( iDataReaded < iDataSize ) {

				switch( (uint8_t)parcel[iBuffPosition] ) {

				case 1:	// External voltage in mV + Internal voltage (battery) in mV

					record->vbort = 0.001 * (*(uint16_t *)&parcel[iBuffPosition + 3]);
					record->vbatt = 0.001 * (*(uint16_t *)&parcel[iBuffPosition + 1]);

					break;
				case 3:	// Latitude (latitude)

					record->lat = *(float *)&parcel[iBuffPosition + 1];
					if( record->lat < 0.0 ) {
						record->lat = fabs(record->lat);
						record->clat = 'S';
					} else
						record->clat = 'N';

					break;
				case 4:	// Longitude (longitude)

					record->lon = *(float *)&parcel[iBuffPosition + 1];
					if( record->lon < 0.0 ) {
						record->lon = fabs(record->lon);
						record->clon = 'W';
					} else
						record->clon = 'E';

					break;
				case 5:	// speed (high byte), satellites, height, course (least significant byte)

					// speed (high byte): 0x08 = 8 * 1.852 = 14.81 km / h above the speed in knots
					record->speed = MILE * ((uint8_t)parcel[iBuffPosition + 4]);
					if( record->speed > 10.0 )
						record->speed = Round(record->speed, 0);

					// satellites: 0xE7 = 0-3 bits - the number of GPS (0-15) 7 (0x07) satellites 4-7 bits - the number of Glonass (0-15) 14 (0x0E) satellites, the total number of satellites 21
					iTemp = (uint8_t)parcel[iBuffPosition + 3];
					record->satellites = ((iTemp & 240) >> 4) + (iTemp & 15);

					// height: 0x19 = 25 * 10 = 250 m altitude in meters / 10 - its lie
					record->height = (uint8_t)parcel[iBuffPosition + 2] * 10;

					// course (least significant byte): 0x11 = 17 * 2 = rate multiplied by 2
					record->curs = (uint8_t)parcel[iBuffPosition + 1] * 2;

					break;
				case 6:	// DINx - number of digital input, its mode value

					iTemp = (uint8_t)parcel[iBuffPosition + 4];	// first byte - number of inputs
					// second byte - input mode
					// 6 - impulse mode
					// 7 - frequency mode
					// 8 - analog voltage mode
					if( iTemp < 8 ) {
						record->ainputs[iTemp] = *(uint16_t *)&parcel[iBuffPosition + 1];
						record->inputs = record->inputs & (1 << iTemp);
						record->zaj = record->ainputs[1];
					}

					break;
				case 9:	// Device status (DS)

					uiTemp = *(uint32_t *)&parcel[iBuffPosition + 1];
					// 0x4FC1C000 = 1338097664 = 1001111110000011100000000000000
					//                                     20   15    987      0
					record->status = uiTemp & 16777215;	// delete 31-24 bits - ext voltage
					record->inputs = uiTemp & 255;		// bits 0 - 7
					record->outputs = uiTemp & 3840;	// bits 8 - 11
					record->alarm = uiTemp & 1048576;	// bit 20
					record->zaj = record->inputs & 1;

					if( record->vbort < 0.1 ) {	// if TAG=1 not found
						record->vbort = Round(0.001 * (((uiTemp & 4278190080) >> 24) * 150), 1);
					}

					/*
					   00 - less than 3V or not connected,
					   01 - from 3V to 3.8 V,
					   10 - from 3.8V to 4.1V,
					   11 - more than 4.1V (normal)
					   110000000000000000000000 = 12582912
					   100000000000000000000000 = 8388608
					   10000000000000000000000 = 4194304
					*/
					if( record->vbatt < 0.1 ) {	// if TAG=1 not found
						switch( uiTemp & 12582912 ) {
						case 12582912:
							record->vbatt = 4.1;
							break;
						case 8388608:
							record->vbatt = 3.8;
							break;
						case 4194304:
							record->vbatt = 3.0;
							break;
						default:
							record->vbatt = 0.0;
						}	// switch
					}	// if( record->vbatt < 0.1 )

					break;
				case 20:	// Frequency on IN_0 - IN_7
				case 21:
				case 22:
				case 23:
				case 24:
				case 25:
				case 26:
				case 27:

					iTemp = (uint8_t)parcel[iBuffPosition] - 20;
					if( iTemp < 8 ) {	// my cpecific - 8 inputs
						record->ainputs[iTemp] = *(uint32_t *)&parcel[iBuffPosition + 1];
					}

					break;
				case 30:	// The analog sensor in mV on IN_0 - IN_7
				case 31:
				case 32:
				case 33:
				case 34:
				case 35:
				case 36:
				case 37:

					iTemp = (uint8_t)parcel[iBuffPosition] - 30;
					if( iTemp < 8 ) {	// my cpecific - 8 inputs
						record->ainputs[iTemp] = *(uint32_t *)&parcel[iBuffPosition + 1];
					}

					break;
				case 70:	// The value of the relative level and temperature DUT protocol LLS 0
				case 71:	//
				case 72:	// the first 2 bytes - level value
				case 73:	// 3rd byte - temperature in degrees Celsius (-128 ... 127)
				case 74:  // 4th - reserve
				case 75:	//
				case 76:	//
				case 77:	//
				case 78:	//
				case 79:	// The value of the relative level and temperature DUT protocol LLS 9

					iTemp = (uint8_t)parcel[iBuffPosition] - 70;
					if( iTemp < 2 ) {	// my cpecific - 2 fuel value
						record->fuel[iTemp] = *(uint16_t *)&parcel[iBuffPosition + 1];
					}

					break;
				case 80:	// The value of the relative level and temperature DUT protocol LLS

					// four bytes - the number of sensor 0x03 - sensor №3
					iTemp = (uint8_t)parcel[iBuffPosition + 4];
					if( iTemp < 2 ) {
						record->fuel[iTemp] = *(uint16_t *)&parcel[iBuffPosition + 1];
					}

					break;
				case 150:	// full mileage of vehicle (km) over satellite, multiplied by 100

					// my cpecific: probeg in meters from prev. mark
					uiTemp = 10 * (*(uint32_t *)&parcel[iBuffPosition + 1]);
					if( uiPrevProbeg && uiPrevProbeg <= uiTemp )
						record->probeg = uiTemp - uiPrevProbeg;
					uiPrevProbeg = uiTemp;

					break;
				case 151:	// Bit 0-15 - hdop, multiplied by 100, Bit 16-31 - reserved

					uiTemp = *(uint16_t *)&parcel[iBuffPosition + 3];
					record->hdop = (0.01 * uiTemp);

					break;
				case 250:	// informational messages
					;
				} // switch( (uint8_t)parcel[iBuffPosition] )

				iBuffPosition += 5;
				iDataReaded += 5;
			}	// while( iDataReaded < iDataSize )
			// fields end ------------------------------------------------------------

			record->valid = record->lat > 0.0 && record->lon > 0.0 && record->satellites > 2 && record->hdop < 10;

			answer->count++;
			iBuffPosition += 1;	// CRC

			break;
		case 3:	// RECORD is a text message, like answer to text command (message to driver)

			record_header = (ARNAVI_RECORD_HEADER *)&parcel[iBuffPosition];
			//iDataSize = record_header->SIZE;
			//iDataReaded = 0;

			iBuffPosition += (sizeof(ARNAVI_RECORD_HEADER) + record_header->SIZE + 1/*CRC*/);

			break;
		case 4:	// FILE DATA field structure of the transmission attributes of a file

			record_header = (ARNAVI_RECORD_HEADER *)&parcel[iBuffPosition];
			//iDataSize = record_header->SIZE;
			//iDataReaded = 0;

			iBuffPosition += (sizeof(ARNAVI_RECORD_HEADER) + record_header->SIZE + 1/*CRC*/);

			break;
		case 6:	// PACKET DATA_BINARY is a binary data to be transmitted to server

			record_header = (ARNAVI_RECORD_HEADER *)&parcel[iBuffPosition];
			//iDataSize = record_header->SIZE;
			//iDataReaded = 0;

			iBuffPosition += (sizeof(ARNAVI_RECORD_HEADER) + record_header->SIZE + 1/*CRC*/);

			break;

		case 0x5D:	// end of package

			iBuffPosition += 1;

			break;
		default:	// parcel error

			//syslog(LOG_NOTICE, "PACKAGE ERROR, iBuffPosition=%d, parcel[%d]=0x%X\n", iBuffPosition, iBuffPosition, (uint8_t)parcel[iBuffPosition]);
			//logging("terminal_decode[arnavi]: error in parcel[%d]=0x%X\n", iBuffPosition, (uint8_t)parcel[iBuffPosition]);

			return;
		}	// swith( (uint8_t)parcel[iBuffPosition] )

	}	// while( iBuffPosition < parcel_size )
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
