/*
   egts.c
   shared library for decode/encode gps/glonass terminal EGTS (ERA-GLONASS) messages

   help:
   see comments in code & egts.h

   compile:
   make -B egts
*/

#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <errno.h>  /* errno */
#include <stdint.h> /* uint8_t, etc... */
#include "glonassd.h"
#include "de.h"     // ST_ANSWER
#include "lib.h"    // MIN, MAX, BETWEEN, CRC, etc...
#include "egts.h"
#include "logger.h"

// for encode
#define MAX_TERMINALS 1000
#define UTS2010	(1262304000)	// unix timestamp 00:00:00 01.01.2010

/*
   decode function
   parcel - the raw data from socket
   parcel_size - it length
   answer - pointer to ST_ANSWER structure (de.h)

   реализовано только самое необходимое для приема навигационных данных и датчиков
*/
void terminal_decode(char *parcel, int parcel_size, ST_ANSWER *answer)
{
	int parcel_pointer, sdr_readed;
	EGTS_PACKET_HEADER *pak_head;
	EGTS_RECORD_HEADER *rec_head, st_rec_header;
	EGTS_SUBRECORD_HEADER *srd_head;
	ST_RECORD *record = NULL;
	/*
	   EGTS_PT_RESPONSE_HEADER *response_hdr = NULL;
	   EGTS_SR_RECORD_RESPONSE_RECORD *response_rec = NULL;
	   EGTS_SR_RESULT_CODE_RECORD *result_rec = NULL;
	*/

	if( !parcel || parcel_size <= 0 || !answer )
		return;

	// создаем ответ на пакет
	answer->size = packet_create(answer->answer, EGTS_PT_RESPONSE);

	// разбираем заголовок пакета
	pak_head = (EGTS_PACKET_HEADER *)parcel;
	if( !Parse_EGTS_PACKET_HEADER(answer, parcel, parcel_size) ) {
		answer->size += packet_finalize(answer->answer, answer->size);
		return;
	}
	parcel_pointer = pak_head->HL;

	// проверяем тип пакета
	switch( pak_head->PT ) {
	case EGTS_PT_RESPONSE:	// ответ на что-то
		//log2file("/var/www/locman.org/tmp/gcc/r_EGTS_PT_RESPONSE", parcel, parcel_size);
		/*
			response_hdr = (EGTS_PT_RESPONSE_HEADER *)&parcel[pak_head->HL];
			response_rec = (EGTS_SR_RECORD_RESPONSE_RECORD *)&parcel[pak_head->HL + sizeof(EGTS_PT_RESPONSE_HEADER)];
			result_rec = (EGTS_SR_RESULT_CODE_RECORD *)&parcel[pak_head->HL + sizeof(EGTS_PT_RESPONSE_HEADER) + sizeof(EGTS_SR_RECORD_RESPONSE_RECORD)];

		   logging("terminal_decode[egts]: EGTS_PT_RESPONSE packet[%u]=%u, rec[%u]=%u, res=%u\n",
																									response_hdr->RPID,
																									response_hdr->PR,
																									response_rec->CRN,
																									response_rec->RST,
																									result_rec->RCD);
		*/


		answer->size = 0;

		return; // отвечать на ответ моветон
	case EGTS_PT_SIGNED_APPDATA:	// данные с цифровой подписью
		//log2file("/var/www/locman.org/tmp/gcc/r_EGTS_PT_SIGNED_APPDATA", parcel, parcel_size);

		// пропускаем цифровую подпись: 2 байта SIGL(Signature Length) + Signature Length
		parcel_pointer += (*(uint16_t *)&parcel[parcel_pointer] + sizeof(uint16_t));

		break;
	case EGTS_PT_APPDATA:	// просто данные
		break;
	}	// switch( pak_head->PT )

	// вставляем в ответ на пакет OK для транспортного уровня пакета, получили, типа
	answer->size += responce_add_responce(answer->answer, answer->size, pak_head->PID, EGTS_PC_OK);


	// Чтение данных SFRD
	while( parcel_pointer < pak_head->HL + pak_head->FDL ) {

		// получаем указатель на SDR (Service Data Record)
		rec_head = (EGTS_RECORD_HEADER *)&parcel[parcel_pointer];
		// проверяем длинну присланных данных
		if( !rec_head->RL ) {	// EGTS_PC_INVDATALEN
			logging("terminal_decode[egts]: SDR:EGTS_PC_INVDATALEN error\n");
			answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_INVDATALEN);
			answer->size += packet_finalize(answer->answer, answer->size);
			return;
		}

		// разбираем SDR
		parcel_pointer += Parse_EGTS_RECORD_HEADER(rec_head, &st_rec_header, answer);

		sdr_readed = 0;	// прочитано данных, байт
		while(sdr_readed < rec_head->RL) {

			// получаем указатель на SRD (Subrecord Data) заголовок
			srd_head = (EGTS_SUBRECORD_HEADER *)&parcel[parcel_pointer];
			// проверяем длинну присланных данных
			if( !srd_head->SRL ) {
				logging("terminal_decode[egts]: SRD:EGTS_PC_INVDATALEN error\n");
				answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_INVDATALEN);
				answer->size += packet_finalize(answer->answer, answer->size);
				return;
			}

			sdr_readed += sizeof(EGTS_SUBRECORD_HEADER);
			parcel_pointer += sizeof(EGTS_SUBRECORD_HEADER);

			// разбираем SRD (Subrecord Data) в зависимости от типа записи
			switch( srd_head->SRT ) {
			case EGTS_SR_TERM_IDENTITY:	// авторизация

				answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_OK);

				if( !Parse_EGTS_SR_TERM_IDENTITY( (EGTS_SR_TERM_IDENTITY_RECORD *)&parcel[parcel_pointer], answer ) && !st_rec_header.OID ) { // нет ID терминала
					// формируем ответ пройдите на хуй, пожалуйста
					answer->size += responce_add_result(answer->answer, answer->size, EGTS_PC_AUTH_DENIED);
				} else {
					// формируем ответ добро пожаловать
					answer->size += responce_add_result(answer->answer, answer->size, EGTS_PC_OK);
				}

				break;
			case EGTS_SR_POS_DATA:	// навигационные данные

				// создаем запись для сохранения данных
				if( answer->count < MAX_RECORDS - 1 )
					answer->count++;
				record = &answer->records[answer->count - 1];

				if( Parse_EGTS_SR_POS_DATA( (EGTS_SR_POS_DATA_RECORD *)&parcel[parcel_pointer], record, answer ) ) {
					answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_OK);
					memcpy(&answer->lastpoint, record, sizeof(ST_RECORD));
				} else {
					answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_OK);
				}

				break;
			case EGTS_SR_EXT_POS_DATA:

				if( record ) {
					Parse_EGTS_SR_EXT_POS_DATA((EGTS_SR_EXT_POS_DATA_RECORD *)&parcel[parcel_pointer], record);
				}

				break;
			case EGTS_SR_LIQUID_LEVEL_SENSOR:	// датчики уровня жидкости

				if( record ) {
					Parse_EGTS_SR_LIQUID_LEVEL_SENSOR(srd_head->SRL, (EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD *)&parcel[parcel_pointer], record);
				}

				break;
			case EGTS_SR_COMMAND_DATA:	// команда

				// сформировать подтверждение в виде подзаписи EGTS_SR_COMMAND_DATA сервиса EGTS_COMMAND_SERVICE
				answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_OK);

				if( Parse_EGTS_SR_COMMAND_DATA(answer, (EGTS_SR_COMMAND_DATA_RECORD *)&parcel[parcel_pointer]) ) {
					// завершаем пакет
					answer->size += packet_finalize(answer->answer, answer->size);

					// сформировать подзапись EGTS_SR_POS_DATA сервиса EGRS_TELEDATA_SERVICE
					// создать новый пакет и вернуть его
					return;
				}	// if( Parse_EGTS_SR_COMMAND_DATA(

				break;
			default:	// мы такое не обрабатываем

				//log2file("/var/www/locman.org/tmp/gcc/r_default", parcel, parcel_size);
				answer->size += responce_add_record(answer->answer, answer->size, rec_head->RN, EGTS_PC_OK);

			}	// switch( srd_head->SRT )

			// переходим к следующей подзаписи
			sdr_readed += srd_head->SRL;
			parcel_pointer += srd_head->SRL;
		}	// while(sdr_readed < rec_head->RL)

	}	// while( parcel_pointer < pak_head->HL + pak_head->FDL )

	/* debug
	   if(record){
	   logging("terminal_decode[egts]: record->imei=%s\n", record->imei);
	   logging("terminal_decode[egts]: record->lat=%lf\n", record->lat);
	   logging("terminal_decode[egts]: record->lon=%lf\n", record->lon);
	   logging("terminal_decode[egts]: record->valid=%d\n", record->valid);
	   logging("terminal_decode[egts]: record->satellites=%d\n", record->satellites);
	   logging("terminal_decode[egts]: record->hdop=%d\n", record->hdop);
	   logging("terminal_decode[egts]: record->height=%d\n", record->height);
	   logging("terminal_decode[egts]: record->curs=%d\n", record->curs);
	   logging("terminal_decode[egts]: record->speed=%lf\n", record->speed);
	   logging("terminal_decode[egts]: record->probeg=%lf\n", record->probeg);
	   }
	*/

	if(answer->size) {
		//log2file("/var/www/locman.org/tmp/gcc/w_answer", answer->answer, answer->size);
		answer->size += packet_finalize(answer->answer, answer->size);
	}

}
//------------------------------------------------------------------------------

/* создание заголовка транспортного уровня (EGTS_PACKET_HEADER) ответа на сообщение
   buffer - укакзатель на буфер, в котором формируется пакет
   pt - Тип пакета Транспортного Уровня
*/
int packet_create(char *buffer, uint8_t pt)
{
	static unsigned short int PaketNumber = 0;	// packet number, joint for encode & decode
	EGTS_PACKET_HEADER *pak_head = (EGTS_PACKET_HEADER *)buffer;
	pak_head->PRV = 1;
	pak_head->SKID = 0;
	pak_head->PRF = 0;
	pak_head->HL = sizeof(EGTS_PACKET_HEADER);	// 11
	pak_head->HE = 0;
	pak_head->FDL = 0;
	pak_head->PID = PaketNumber++;
	pak_head->PT = pt;	// Тип пакета Транспортного Уровня
	//pak_head->HCS = CRC8((unsigned char *)pak_head, pak_head->HL-1); // see packet_finalize

	return pak_head->HL;
}
//------------------------------------------------------------------------------

// добавление в ответ результата обработки транспортного уровня
int responce_add_responce(char *buffer, int pointer, uint16_t pid, uint8_t pr)
{
	EGTS_PT_RESPONSE_HEADER *res_head = (EGTS_PT_RESPONSE_HEADER *)&buffer[pointer];
	res_head->RPID = pid;
	res_head->PR = pr;

	EGTS_PACKET_HEADER *pak_head = (EGTS_PACKET_HEADER *)buffer;
	pak_head->FDL = sizeof(EGTS_PT_RESPONSE_HEADER);

	return sizeof(EGTS_PT_RESPONSE_HEADER);
}
//------------------------------------------------------------------------------

// добавление в ответ результата обработки записей
int responce_add_record(char *buffer, int pointer, uint16_t crn, uint8_t rst)
{
	EGTS_SR_RECORD_RESPONSE_RECORD *record = (EGTS_SR_RECORD_RESPONSE_RECORD *)&buffer[pointer];
	record->CRN = crn;
	record->RST = rst;

	EGTS_PACKET_HEADER *pak_head = (EGTS_PACKET_HEADER *)buffer;
	pak_head->FDL += sizeof(EGTS_SR_RECORD_RESPONSE_RECORD);

	return sizeof(EGTS_SR_RECORD_RESPONSE_RECORD);
}
//------------------------------------------------------------------------------

// добавление в ответ результата обработки чего-нибудь
int responce_add_result(char *buffer, int pointer, uint8_t rcd)
{
	EGTS_SR_RESULT_CODE_RECORD *record = (EGTS_SR_RESULT_CODE_RECORD *)&buffer[pointer];
	record->RCD = rcd;

	EGTS_PACKET_HEADER *pak_head = (EGTS_PACKET_HEADER *)buffer;
	pak_head->FDL += sizeof(EGTS_SR_RESULT_CODE_RECORD);

	return sizeof(EGTS_SR_RESULT_CODE_RECORD);
}
//------------------------------------------------------------------------------

// расчет CRC16 & CRC8 для данных егтс пакета
int packet_finalize(char *buffer, int pointer)
{
	EGTS_PACKET_HEADER *pak_head = (EGTS_PACKET_HEADER *)buffer;

	if( pointer - pak_head->HL > pak_head->FDL ) {
		logging("terminal_decode[egts]: pak_head->FDL correct from %u to %u\n", pak_head->FDL, pointer - pak_head->HL);
		pak_head->FDL = pointer - pak_head->HL;
	}

	// добавляем CRC16 в конец пакета
	unsigned short *SFRCS = (unsigned short *)&buffer[pointer];
	// рассчитываем CRC16
	*SFRCS = CRC16EGTS( (unsigned char *)&buffer[pak_head->HL], pak_head->FDL );

	// рассчитываем CRC8
	pak_head->HCS = CRC8EGTS((unsigned char *)pak_head, pak_head->HL - 1);	// последний байт это CRC

	return sizeof(unsigned short);
}
//------------------------------------------------------------------------------

/*
   Name : CRC-8
   Poly : 0x31 x^8 + x^5 + x^4 + 1
   Init : 0xFF
   Revert: false
   XorOut: 0x00
   Check : 0xF7 ("123456789")
*/
unsigned char CRC8EGTS(unsigned char *lpBlock, unsigned char len)
{
	unsigned char crc = 0xFF;
	while (len--)
		crc = CRC8Table[crc ^ *lpBlock++];
	return crc;
}
//------------------------------------------------------------------------------

/*
   Name : CRC-16 CCITT
   Poly : 0x1021 x^16 + x^12 + x^5 + 1
   Init : 0xFFFF
   Revert: false
   XorOut: 0x0000
   Check : 0x29B1 ("123456789")
*/
unsigned short CRC16EGTS(unsigned char * pcBlock, unsigned short len)
{
	unsigned short crc = 0xFFFF;
	while (len--)
		crc = (crc << 8) ^ Crc16Table[(crc >> 8) ^ *pcBlock++];
	return crc;
}
//------------------------------------------------------------------------------

int Parse_EGTS_PACKET_HEADER(ST_ANSWER *answer, char *pc, int parcel_size)
{
	EGTS_PACKET_HEADER *ph = (EGTS_PACKET_HEADER *)pc;

	if( ph->PRV != 1 || (ph->PRF & 192) ) {
		logging("terminal_decode[egts]: EGTS_PC_UNS_PROTOCOL error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_UNS_PROTOCOL);
		return 0;
	}

	if( ph->HL != 11 && ph->HL != 16 ) {
		logging("terminal_decode[egts]: EGTS_PC_INC_HEADERFORM error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_INC_HEADERFORM);
		return 0;
	}

	if( CRC8EGTS((unsigned char *)ph, ph->HL-1) != ph->HCS ) {
		logging("terminal_decode[egts]: EGTS_PC_HEADERCRC_ERROR error %u/%u\n", CRC8EGTS((unsigned char *)ph, ph->HL-1), ph->HCS);
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_HEADERCRC_ERROR);
		return 0;
	}

	if( B5 & ph->PRF ) {
		logging("terminal_decode[egts]: EGTS_PC_TTLEXPIRED error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_TTLEXPIRED);
		return 0;
	}

	if( !ph->FDL ) {
		logging("terminal_decode[egts]: EGTS_PC_OK\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_OK);
		return 0;
	}

	// проверяем CRC16
	unsigned short *SFRCS = (unsigned short *)&pc[ph->HL + ph->FDL];
	if( *SFRCS != CRC16EGTS( (unsigned char *)&pc[ph->HL], ph->FDL) ) {
		logging("terminal_decode[egts]: EGTS_PC_DATACRC_ERROR error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_DATACRC_ERROR);
		return 0;
	}

	// проверяем шифрование данных
	if( ph->PRF & 24 ) {
		logging("terminal_decode[egts]: EGTS_PC_DECRYPT_ERROR error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_DECRYPT_ERROR);
		return 0;
	}

	// проверяем сжатие данных
	if( ph->PRF & B2 ) {
		logging("terminal_decode[egts]: EGTS_PC_INC_DATAFORM error\n");
		answer->size += responce_add_responce(answer->answer, answer->size, ph->PID, EGTS_PC_INC_DATAFORM);
	}

	/*
	   logging("terminal_decode[egts]: pak_head->PRV=%d\n", ph->PRV);
	   logging("terminal_decode[egts]: pak_head->SKID=%d\n", ph->SKID);
	   logging("terminal_decode[egts]: pak_head->PRF=%d\n", ph->PRF);
	   logging("terminal_decode[egts]: pak_head->HL=%d\n", ph->HL);
	   logging("terminal_decode[egts]: pak_head->HE=%d\n", ph->HE);
	   logging("terminal_decode[egts]: pak_head->FDL=%d\n", ph->FDL);
	   logging("terminal_decode[egts]: pak_head->PID=%d\n", ph->PID);
	   logging("terminal_decode[egts]: pak_head->PT=%d\n", ph->PT);
	   logging("terminal_decode[egts]: pak_head->HCS=%d\n", ph->HCS);
	*/

	return 1;
}
//------------------------------------------------------------------------------

int Parse_EGTS_RECORD_HEADER(EGTS_RECORD_HEADER *rec_head, EGTS_RECORD_HEADER *st_header, ST_ANSWER *answer)
{

	int rec_head_size = 2 * sizeof(uint16_t) + sizeof(uint8_t);
	char *pc = (char *)rec_head;

	memset(st_header, 0, sizeof(EGTS_RECORD_HEADER));

	st_header->RL = rec_head->RL;
	st_header->RN = rec_head->RN;
	st_header->RFL = rec_head->RFL;

	// OBFE	0		наличие в данном пакете поля OID 1 = присутствует 0 = отсутствует
	if( st_header->RFL & B0 ) {
		st_header->OID = *(uint32_t *)&pc[rec_head_size];
		if( st_header->OID && !strlen(answer->lastpoint.imei) ) {	// D:\Work\Borland\Satellite\egts\Рекомендации по реализации протокола передачи данных в РНИЦ.doc 5.1.1	Идентификация АС посредством поля OID
			memset(answer->lastpoint.imei, 0, SIZE_TRACKER_FIELD);
			snprintf(answer->lastpoint.imei, SIZE_TRACKER_FIELD, "%d", st_header->OID);
		}
		rec_head_size += sizeof(uint32_t);
	}

	// EVFE	1		наличие в данном пакете поля EVID 1 = присутствует 0 = отсутствует
	if( st_header->RFL & B1 ) {
		st_header->EVID = *(uint32_t *)&pc[rec_head_size];
		rec_head_size += sizeof(uint32_t);
	}

	// TMFE	2		наличие в данном пакете поля TM 1 = присутствует 0 = отсутствует
	if( st_header->RFL & B2 ) {
		st_header->TM = *(uint32_t *)&pc[rec_head_size];
		rec_head_size += sizeof(uint32_t);
	}

	st_header->SST = *(uint8_t *)&pc[rec_head_size];
	rec_head_size += sizeof(uint8_t);
	st_header->RST = *(uint8_t *)&pc[rec_head_size];
	rec_head_size += sizeof(uint8_t);

	/*
	   logging("terminal_decode[egts]: st_header->RL=%d\n", st_header->RL);
	   logging("terminal_decode[egts]: st_header->RN=%d\n", st_header->RN);
	   logging("terminal_decode[egts]: st_header->RFL=%d\n", st_header->RFL);
	   logging("terminal_decode[egts]: st_header->OID=%d\n", st_header->OID);
	   logging("terminal_decode[egts]: st_header->EVID=%d\n", st_header->EVID);
	   logging("terminal_decode[egts]: st_header->TM=%d\n", st_header->TM);
	   logging("terminal_decode[egts]: st_header->SST=%d\n", st_header->SST);
	   logging("terminal_decode[egts]: st_header->RST=%d\n", st_header->RST);
	   logging("terminal_decode[egts]: rec_head_size=%d\n", rec_head_size);
	*/

	return rec_head_size;
}
//------------------------------------------------------------------------------

// разбор записи EGTS_SR_TERM_IDENTITY (Подзапись используется АТ при запросе авторизации на ТП и содержит учётные данные АТ)
// D:\Work\Borland\Satellite\egts\Рекомендации по реализации протокола передачи данных в РНИЦ.doc
// 5.1.2	Идентификация АС посредством сервиса EGTS_AUTH_SERVICE
int Parse_EGTS_SR_TERM_IDENTITY(EGTS_SR_TERM_IDENTITY_RECORD *record, ST_ANSWER *answer)
{
	int record_size = sizeof(uint32_t) + sizeof(uint8_t);
	char *pc = (char *)record;

	memset(answer->lastpoint.imei, 0, SIZE_TRACKER_FIELD);

	if( record->FLG & B1 ) { // наличие поля IMEI в подзаписи
		if( record->FLG & B0 ) {	// наличие поля HDID в подзаписи
			record_size += sizeof(uint16_t);	// пропускаем поле HDID, если есть
		}
		memcpy(answer->lastpoint.imei, &pc[record_size], 15);
		record_size += EGTS_IMEI_LEN;
	}	// if( record->FLG & B1 )
	else if( record->TID ) {
		snprintf(answer->lastpoint.imei, SIZE_TRACKER_FIELD, "%d", record->TID);
	}
	// если не прислан IMEI и record->TID = 0, то answer->lastpoint.imei окажется пустым

	/*
	   if( record->FLG & B2 ){ // наличие поля IMSI в подзаписи
		record_size += EGTS_IMSI_LEN;
	   }

	   if( record->FLG & B3 ){ // наличие поля LNGC в подзаписи
		record_size += EGTS_LNGC_LEN;
	   }

	   if( record->FLG & B5 ){ // наличие поля NID в подзаписи
		record_size += (3 * sizeof(uint8_t));
	   }

	   if( record->FLG & B6 ){ // наличие поля BS в подзаписи
		record_size += sizeof(uint16_t);
	   }

	   if( record->FLG & B7 ){ // наличие поля MSISDN в подзаписи
		record_size += EGTS_MSISDN_LEN;
	   }

	   return record_size;
	*/

	return strlen(answer->lastpoint.imei);	// всех пускать
}
//------------------------------------------------------------------------------

// разбираем запись с навигационными данными
int Parse_EGTS_SR_POS_DATA(EGTS_SR_POS_DATA_RECORD *posdata, ST_RECORD *record, ST_ANSWER *answer)
{
	char *pc = (char *)posdata;
	void *tpp;
	struct tm tm_data;
	time_t ulliTmp;

	if( !record )
		return 0;

	ulliTmp = posdata->NTM + GMT_diff;	// UTC ->local
	gmtime_r(&ulliTmp, &tm_data);           // local simple->local struct
	// получаем время как число секунд от начала суток
	record->time = 3600 * tm_data.tm_hour + 60 * tm_data.tm_min + tm_data.tm_sec;
	// в tm_data обнуляем время
	tm_data.tm_hour = tm_data.tm_min = tm_data.tm_sec = 0;
	// получаем дату
	tm_data.tm_year = (tm_data.tm_year + 2010 - 1970);
	record->data = timegm(&tm_data) - GMT_diff;	// local struct->local simple & mktime epoch

	// координаты
	record->lat = 90.0 * posdata->LAT / 0xFFFFFFFF;
	record->lon = 180.0 * posdata->LONG / 0xFFFFFFFF;
	/* пиздят, как сивый мерин, присылаются в WGS84
	   if( posdata->FLG & B1 ){ // прислано в ПЗ-90.02, надо перевести в WGS-84 ибо Если координаты не трансформировать, то возникнет погрешность до 10 м
		//Geo2Geo(PZ90, WGS84, &record->lon, &record->lat);
	   }	// if( posdata->FLG & B1 )
	*/

	record->valid = (posdata->FLG & B0);

	if(posdata->FLG & B5)
		record->clat = 'S';
	else
		record->clat = 'N';

	if(posdata->FLG & B6)
		record->clon = 'W';
	else
		record->clon = 'E';

	// 14 младших бит, скорость в км/ч с дискретностью 0,1 км/ч
	record->speed = (posdata->SPD & 16383) / 10;
	// направление движения
	// DIRH  :15		(Direction the Highest bit) старший бит (8) параметра DIR
	record->curs = posdata->DIR;
	if( posdata->SPD & 32768 )
		record->curs = posdata->DIR + 256;

	// пробег в км, с дискретностью 0,1 км
	tpp = &posdata->ODM;
	record->probeg = *(uint16_t *)tpp;
	if(posdata->ODM[2])
		record->probeg += (((unsigned int)posdata->ODM[2]) << 16);
	record->probeg *= 100;	// 0,1 км -> м.

	// состояние основных дискретных входов 1 ... 8
	record->ainputs[0] = (posdata->DIN & B0);
	record->ainputs[1] = (posdata->DIN & B1);
	record->ainputs[2] = (posdata->DIN & B2);
	record->ainputs[3] = (posdata->DIN & B3);
	record->ainputs[4] = (posdata->DIN & B4);
	record->ainputs[5] = (posdata->DIN & B5);
	record->ainputs[6] = (posdata->DIN & B6);
	record->ainputs[7] = (posdata->DIN & B7);

	// высота над уровнем моря
	if( posdata->FLG & B7 ) {	// есть поле ALT
		record->height = *(uint16_t *)&pc[sizeof(EGTS_SR_POS_DATA_RECORD)];
		record->height += (((unsigned int)pc[sizeof(EGTS_SR_POS_DATA_RECORD) + 2]) << 16);

		if( posdata->SPD & B14 )
			record->height *= -1;
	}

	snprintf(record->imei, SIZE_TRACKER_FIELD, "%s", answer->lastpoint.imei);
	snprintf(record->tracker, SIZE_TRACKER_FIELD, "EGTS");
	//snprintf(record->hard, SIZE_TRACKER_FIELD, "%d", iHard);
	snprintf(record->soft, SIZE_TRACKER_FIELD, "%f", 1.6);

	return 1;
}
//------------------------------------------------------------------------------

// Используется абонентским терминалом при передаче дополнительных данных
int Parse_EGTS_SR_EXT_POS_DATA(EGTS_SR_EXT_POS_DATA_RECORD *posdata, ST_RECORD *record)
{
	char *pc = (char *)posdata;
	int data_size = sizeof(EGTS_SR_EXT_POS_DATA_RECORD);

	if( !record )
		return 0;

	if( posdata->FLG & B0 ) {	// VDOP Field Exists
		data_size += sizeof(uint16_t);
	}

	if( posdata->FLG & B1 ) {	// HDOP Field Exists
		record->hdop = Round(0.1 * (*(uint16_t *)&pc[data_size]), 0);
		data_size += sizeof(uint16_t);
	}

	if( posdata->FLG & B2 ) {	// PDOP Field Exists
		data_size += sizeof(uint16_t);
	}

	if( posdata->FLG & B3 ) {	// Satellites Field Exists
		record->satellites = *(uint8_t *)&pc[data_size];
		data_size += sizeof(uint8_t);
	}

	if( posdata->FLG & B4 ) {	// Navigation System Field Exists
		data_size += sizeof(uint16_t);
	}

	return data_size;
}
//------------------------------------------------------------------------------

// Используется абонентским терминалом при передаче данных датчиков уровня жидкости
// rlen - длинна данной записи в байтах
int Parse_EGTS_SR_LIQUID_LEVEL_SENSOR(int rlen, EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD *posdata, ST_RECORD *record)
{
	int data_size;

	if( !record )
		return 0;

	if( posdata->FLG & B3 ) {	// размер поля LLSD определяется исходя из общей длины данной подзаписи и размеров расположенных перед LLSD полей
		data_size = rlen;	// здесь нас интересует общая длинна записи
		// ибо как хранить такие данные хз
	} else {	// поле LLSD имеет размер 4 байта
		data_size = sizeof(EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD);

		if( !(posdata->FLG & B6) ) {	// ошибок не обнаружено
			if( record->fuel[0] ) {	// показания первого датчика уже записаны
				record->fuel[1] = posdata->LLSD;
				if( posdata->FLG & 32 )	// показания ДУЖ в литрах с дискретностью в 0,1 литра
					record->fuel[1] = 0.1 * posdata->LLSD;
			} else {
				record->fuel[0] = posdata->LLSD;
				if( posdata->FLG & 32 )	// показания ДУЖ в литрах с дискретностью в 0,1 литра
					record->fuel[0] = 0.1 * posdata->LLSD;
			}
		}	// if( !(posdata->FLG & B6) )
	}

	return data_size;
}
//------------------------------------------------------------------------------


/* обработка команды и ответ на команду
   D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
   6.3 EGTS_COMMANDS_SERVICE
*/
int Parse_EGTS_SR_COMMAND_DATA(ST_ANSWER *answer, EGTS_SR_COMMAND_DATA_RECORD *record)
{
	EGTS_SR_COMMAND_DATA_FIELD *CD;
	uint8_t	*ACL;

	if( record->CT_CCT & 80 ) {	// CT_COM - команда для выполнения на АТ

		// calculate address off field CD
		CD = (EGTS_SR_COMMAND_DATA_FIELD *)(&record->ACFE + sizeof(uint8_t));

		if( record->ACFE & 1 )	// поле CHS присутствует в подзаписи
			CD += sizeof(uint8_t);
		if( record->ACFE & 2 ) {	// поля ACL и AC присутствуют в подзаписи
			ACL = (uint8_t *)CD;
			CD += (sizeof(uint8_t) + (*ACL));
		}

		// check field CD
		if( CD->SZ_ACT & 1 ) {	// запрос значения
			// http://www.zakonprost.ru/content/base/part/1038461
			// http://docs.cntd.ru/document/1200119664

			// сформировать подтверждение в виде подзаписи EGTS_SR_COMMAND_DATA сервиса EGTS_COMMAND_SERVICE
			answer->size += responce_add_subrecord_EGTS_SR_COMMAND_DATA(answer->answer, answer->size, record);

			switch( CD->CCD ) {	// Запрашиваемый параметр определяется кодом из поля CCD
			case EGTS_FLEET_GET_DOUT_DATA:	// Команда запроса состояния дискретных выходов
			case EGTS_FLEET_GET_POS_DATA:	// Команда запроса текущих данных местоположения
			case EGTS_FLEET_GET_SENSORS_DATA:	// Команда запроса состояния дискретных и аналоговых входов
			case EGTS_FLEET_GET_LIN_DATA:	// Команда запроса состояния шлейфовых входов
			case EGTS_FLEET_GET_CIN_DATA:	// Команда запроса состояния счетных входов
			case EGTS_FLEET_GET_STATE:	// Команда запроса состояния абонентского терминала
				/* При получении данной команды помимо подтверждения в виде
				   подзаписи EGTS_SR_COMMAND_DATA сервиса EGTS_COMMAND_SERVICE
					абонентский терминал отправляет телематическое сообщение, содержащее
				   подзапись EGTS_SR_POS_DATA сервиса EGRS_TELEDATA_SERVICE
				*/
				return 1;
			}	// switch( CD->CCD )

		}	// if( CD->SZ_ACT & 1 )

	}	// if( record->CT_CCT & 80 )

	return 0;
}
//------------------------------------------------------------------------------


/* добавление в ответ позаписи EGTS_SR_COMMAND_DATA_RECORD с ответом на команду
   pointer - размер уже сформированного ответа (конец массива байт)
   cmdrec - указатель на пришедшую запись EGTS_SR_COMMAND_DATA_RECORD, на которую отвечаем
*/
int responce_add_subrecord_EGTS_SR_COMMAND_DATA(char *buffer, int pointer, EGTS_SR_COMMAND_DATA_RECORD *cmdrec)
{
	EGTS_SR_COMMAND_DATA_RECORD *cmdresponse;	// формируемая запись
	EGTS_SR_COMMAND_DATA_FIELD *CD, *cmdcd;
	uint8_t	*ACL;

	cmdcd = (EGTS_SR_COMMAND_DATA_FIELD *)(&cmdrec->ACFE + sizeof(uint8_t));
	if( cmdrec->ACFE & 1 )	// поле CHS присутствует в подзаписи
		cmdcd += sizeof(uint8_t);
	if( cmdrec->ACFE & 2 ) {	// поля ACL и AC присутствуют в подзаписи
		ACL = (uint8_t *)cmdcd;
		cmdcd += (sizeof(uint8_t) + (*ACL));
	}

	cmdresponse = (EGTS_SR_COMMAND_DATA_RECORD *)&buffer[pointer];
	cmdresponse->CT_CCT = 16;	// CT_COMCONF + CC_OK
	cmdresponse->CID = cmdrec->CID;
	cmdresponse->SID = cmdrec->SID;
	cmdresponse->ACFE = 0;	// поля ACL и AC отсутствуют в подзаписи + поле CHS отсутствует в подзаписи
	CD = (EGTS_SR_COMMAND_DATA_FIELD *)(&cmdresponse->ACFE + sizeof(uint8_t));
	CD->ADR = cmdcd->ADR;
	CD->SZ_ACT = 0;
	CD->CCD = cmdcd->CCD;

	return sizeof(EGTS_SR_COMMAND_DATA_RECORD);
}
//------------------------------------------------------------------------------



/* encode function
   records - pointer to array of ST_RECORD struct.
   reccount - number of struct in array, and returning (negative if authentificate required)
   buffer - buffer for encoded data
   bufsize - size of buffer
   return size of data in the buffer for encoded data
*/
int terminal_encode(ST_RECORD *records, int reccount, char *buffer, int bufsize)
{
	EGTS_RECORD_HEADER *record_header = NULL;
	EGTS_SUBRECORD_HEADER *subrecord_header = NULL;
	int i, top = 0;

	// create egts packet header
	top = packet_create(buffer, EGTS_PT_APPDATA);

	/*
	   test for imei logging on the remote server
	*/
	if( reccount < 0 ) {	// not logged to remote server
		// EGTS_AUTH_SERVICE
		// add record (SDR) EGTS_RECORD_HEADER
		record_header = (EGTS_RECORD_HEADER *)&buffer[top];
		top = packet_add_record_header(buffer, top, EGTS_AUTH_SERVICE, EGTS_AUTH_SERVICE);

		// add subrecord header (SRD) EGTS_SR_TERM_IDENTITY
		subrecord_header = (EGTS_SUBRECORD_HEADER *)&buffer[top];
		top = packet_add_subrecord_header(buffer, top, record_header, EGTS_SR_TERM_IDENTITY);

		// add subrecord (SRD) EGTS_SR_TERM_IDENTITY
		top = packet_add_subrecord_EGTS_SR_TERM_IDENTITY(buffer, top, record_header, subrecord_header, records[0].imei);

		// add CRC
		top += packet_finalize(buffer, top);

		// authentificate complete, return
		return top;
		// !!! считаем терминал залогиненым на сервер, не дожидаясь ответа сервера
	}	// if( reccount < 0 )

	// here we logged to remote server,
	// create naviagtion data records (EGTS_SR_POS_DATA_RECORD, EGTS_SR_EXT_POS_DATA_RECORD, EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD)
	// EGTS_TELEDATA_SERVICE

	for(i = 0; i < reccount; i++) {

		// add record (SDR) EGTS_RECORD_HEADER
		record_header = (EGTS_RECORD_HEADER *)&buffer[top];
		top = packet_add_record_header(buffer, top, EGTS_TELEDATA_SERVICE, EGTS_TELEDATA_SERVICE);

		// navigation data
		// add subrecord header (SRD) EGTS_SR_POS_DATA
		subrecord_header = (EGTS_SUBRECORD_HEADER *)&buffer[top];
		top = packet_add_subrecord_header(buffer, top, record_header, EGTS_SR_POS_DATA);

		// add subrecord (SRD) EGTS_SR_POS_DATA
		top = packet_add_subrecord_EGTS_SR_POS_DATA_RECORD(buffer, top, record_header, subrecord_header, &records[i]);

		// extended navigation data
		// add subrecord header (SRD) EGTS_SR_EXT_POS_DATA
		subrecord_header = (EGTS_SUBRECORD_HEADER *)&buffer[top];
		top = packet_add_subrecord_header(buffer, top, record_header, EGTS_SR_EXT_POS_DATA);

		// add subrecord (SRD) EGTS_SR_EXT_POS_DATA
		top = packet_add_subrecord_EGTS_SR_EXT_POS_DATA_RECORD(buffer, top, record_header, subrecord_header, &records[i]);

		// fuel & inputs data
		// add subrecord header (SRD) EGTS_SR_LIQUID_LEVEL_SENSOR
		subrecord_header = (EGTS_SUBRECORD_HEADER *)&buffer[top];
		top = packet_add_subrecord_header(buffer, top, record_header, EGTS_SR_LIQUID_LEVEL_SENSOR);

		// add subrecord (SRD) EGTS_SR_LIQUID_LEVEL_SENSOR
		top = packet_add_subrecord_EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD(buffer, top, record_header, subrecord_header, &records[i]);

	}	// for(i = 0; i < reccount; i++)

	// add CRC
	top += packet_finalize(buffer, top);

	return top;
}
//------------------------------------------------------------------------------


/* добавление в егтс пакет записи EGTS_RECORD_HEADER (SDR)
   packet - указатель на буфер формирования пакета
   position - позиция в буфере, куда вставляется запись, фактически это длинна заполненной части буфера
   sst - идентификатор тип Сервиса-отправителя
   rst - идентификатор тип Сервиса-получателя
   возвращает новый размер данных в буфере
*/
static int packet_add_record_header(char *packet, int position, uint8_t sst, uint8_t rst)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	EGTS_RECORD_HEADER *record_header = (EGTS_RECORD_HEADER *)&packet[position];
	static unsigned short int RecordNumber = 0;	// record number for EGTS_RECORD_HEADER
	int new_size, rh_size;
	uint8_t *psst, *prst, rfl;
	uint32_t *poid, *pevid, *ptm;

	rfl = B7 + B4 + B3 + B2;	// битовые флаги (запись сформирована в абонентском терминале + приоритет низкий + есть поле TM)
	/* у меня это лишнее, но пусть будет:
	   расчет адресов полей и
	   расчет длинны EGTS_RECORD_HEADER в хависимости от поля rfl
	*/
	rh_size = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t);	// первые обязательные поля
	new_size = position + rh_size;
	if( rfl & B0 )	// наличие в данном пакете поля OID
		new_size += sizeof(uint32_t);

	if( rfl & B1 )	// наличие в данном пакете поля EVID
		new_size += sizeof(uint32_t);

	if( rfl & B2 )	// наличие в данном пакете поля TM
		new_size += sizeof(uint32_t);

	new_size += sizeof(uint8_t);	// SST
	new_size += sizeof(uint8_t);	// RST

	record_header->RL = 0;		// размер данных из поля RD
	record_header->RN = RecordNumber++;		// номер записи от 0 до 65535
	/* дождавшись ответа EGTS_PC_OK с CRN == record_header->RN
	   надо внести IMEI в массив terminals
	   а можно и не ждать ответа, а тупо внести
	*/
	record_header->RFL = rfl;

	// необязательные поля
	if( rfl & B0 ) {	// наличие в данном пакете поля OID
		poid = (uint32_t *)&packet[position + rh_size];
		rh_size += sizeof(uint32_t);
	} else
		poid = NULL;

	if( rfl & B1 ) {	// наличие в данном пакете поля EVID
		pevid = (uint32_t *)&packet[position + rh_size];
		rh_size += sizeof(uint32_t);
	} else
		pevid = NULL;

	if( rfl & B2 ) {	// наличие в данном пакете поля TM
		ptm = (uint32_t *)&packet[position + rh_size];
		rh_size += sizeof(uint32_t);
	} else
		ptm = NULL;

	// идентификатор объекта, сгенерировавшего данную запись
	if( poid )
		*poid = 50;	// чисто для прикола
	// уникальный идентификатор события
	if( pevid )
		*pevid = 0;
	// время формирования записи на стороне Отправителя
	if( ptm ) {
		*ptm = (uint32_t)(time(NULL) - UTS2010);
	}

	// снова обязательные поля
	psst = (uint8_t *)&packet[position + rh_size];
	*psst = sst;	// идентификатор тип Сервиса-отправителя, сгенерировавшего данную запись (=EGTS_TELEDATA_SERVICE)
	rh_size += sizeof(uint8_t);

	prst = (uint8_t *)&packet[position + rh_size];
	*prst = rst;	// идентификатор тип Сервиса-получателя данной записи (=EGTS_TELEDATA_SERVICE)
	rh_size += sizeof(uint8_t);

	packet_header->FDL += rh_size;

	return new_size;
}
//------------------------------------------------------------------------------

/* добавление в егтс пакет записи EGTS_SUBRECORD_HEADER (SRD)
*/
static int packet_add_subrecord_header(char *packet, int position, EGTS_RECORD_HEADER *record_header, uint8_t srt)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	EGTS_SUBRECORD_HEADER *subrecord_header = (EGTS_SUBRECORD_HEADER *)&packet[position];
	int new_size = position + sizeof(EGTS_SUBRECORD_HEADER);

	packet_header->FDL += sizeof(EGTS_SUBRECORD_HEADER);
	record_header->RL += sizeof(EGTS_SUBRECORD_HEADER);
	subrecord_header->SRT = srt;

	return new_size;
}
//------------------------------------------------------------------------------

/* добавление в пакет поздаписи EGTS_SR_TERM_IDENTITY
   с данными идентификации терминала
*/
static int packet_add_subrecord_EGTS_SR_TERM_IDENTITY(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, char *imei)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	//                       TID & FLG(B1 + B6)                IMEI              BS
	int rec_size = sizeof(EGTS_SR_TERM_IDENTITY_RECORD) + EGTS_IMEI_LEN + sizeof(uint16_t);
	int new_size = position + rec_size;
	char *pimei;
	EGTS_SR_TERM_IDENTITY_RECORD *record;
	uint16_t *bs;

	packet_header->FDL += rec_size;
	record_header->RL += rec_size;
	subrecord_header->SRL = rec_size;

	record = (EGTS_SR_TERM_IDENTITY_RECORD *)&packet[position];
	// уникальный идентификатор, назначаемый при программировании АТ
	if( record_header->RFL & B0 )
		record->TID = *(uint32_t *)(&record_header->RFL + sizeof(uint8_t));
	else
		record->TID = 50;	// :)

	// Flags
	record->FLG = B1 + B6;	// наличие поля IMEI + наличие поля BS в подзаписи

	// imei
	pimei = (char *)&packet[position + sizeof(EGTS_SR_TERM_IDENTITY_RECORD)];
	memset(pimei, 0, EGTS_IMEI_LEN);
	snprintf(pimei, EGTS_IMEI_LEN+1, "%s", imei);	// EGTS_IMEI_LEN=15, SIZE_TRACKER_FIELD=16

	// BS
	bs = (uint16_t *)&packet[position + sizeof(EGTS_SR_TERM_IDENTITY_RECORD) + EGTS_IMEI_LEN];
	*bs = 1492;

	return new_size;
}
//------------------------------------------------------------------------------

/* добавление в пакет поздаписи EGTS_SR_POS_DATA
   с данными навигации
*/
static int packet_add_subrecord_EGTS_SR_POS_DATA_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	int rec_size = sizeof(EGTS_SR_POS_DATA_RECORD) + sizeof(uint8_t) * 3;	// высота над уровнем моря
	int new_size = position + rec_size;
	EGTS_SR_POS_DATA_RECORD *pos_data_rec;
	uint16_t *puint16;

	packet_header->FDL += rec_size;
	record_header->RL += rec_size;
	subrecord_header->SRL = rec_size;

	pos_data_rec = (EGTS_SR_POS_DATA_RECORD *)&packet[position];

	// time since 2010
	pos_data_rec->NTM = (uint32_t)(record->data + record->time - UTS2010);

	pos_data_rec->LAT = record->lat / 90 * 0xFFFFFFFF;
	pos_data_rec->LONG = record->lon / 180 * 0xFFFFFFFF;

	pos_data_rec->FLG = B2;			// тип определения координат: 0 - 2D fix; 1 - 3D fix
	pos_data_rec->FLG += B7;		// определяет наличие поля ALT в подзаписи
	if( record->valid )         // признак "валидности"
		pos_data_rec->FLG += B0;
	if( record->speed )        // признак движения: 1 - движение
		pos_data_rec->FLG += B4;
	if( record->clat == 'S' )  // определяет полушарие широты:  0 - северная 1 - южная
		pos_data_rec->FLG += B5;
	if( record->clon == 'W' )  // определяет полушарие долготы 0 - восточная 1 - западная
		pos_data_rec->FLG += B6;

	// 14 младших бит, скорость в км/ч с дискретностью 0,1 км/ч
	pos_data_rec->SPD = 10 * record->speed;
	// ALTS  :14		определяет высоту относительно уровня моря и имеет смысл только при установленном флаге ALTE: 0 - точка выше уровня моря; 1 - ниже уровня моря
	if( record->height < 0 )
		pos_data_rec->SPD = pos_data_rec->SPD | 16384;
	// DIRH  :15		(Direction the Highest bit) старший бит (8) параметра DIR
	if( record->curs > 255 )
		pos_data_rec->SPD = pos_data_rec->SPD | 32768;

	// направление движения
	if( record->curs > 255 )
		pos_data_rec->DIR = record->curs - 255;
	else
		pos_data_rec->DIR = record->curs;

	// пройденное расстояние (пробег) в км, с дискретностью 0,1 км
	pos_data_rec->ODM[2] = 0;
	puint16 = (uint16_t *)&pos_data_rec->ODM;
	*puint16 = (uint16_t)(record->probeg / 100);
	// битовые флаги, определяют состояние основных дискретных входов 1 ... 8
	pos_data_rec->DIN = record->inputs;
	// источник (событие), инициировавший посылку данной навигационной информации
	if( record->alarm )
		pos_data_rec->SRC = 13;
	else if( record->zaj )
		pos_data_rec->SRC = 0;
	else
		pos_data_rec->SRC = 5;

	// высота над уровнем моря, м (опциональный параметр
	puint16 = (uint16_t *)(&pos_data_rec->SRC + sizeof(uint8_t));
	memset(puint16, 0, sizeof(char) * 3);
	*puint16 = (uint16_t)record->height;

	return new_size;
}
//------------------------------------------------------------------------------

/* добавление в пакет поздаписи EGTS_SR_EXT_POS_DATA
   с дополнительными данными навигации
*/
static int packet_add_subrecord_EGTS_SR_EXT_POS_DATA_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	int rec_size = sizeof(EGTS_SR_EXT_POS_DATA_RECORD) + sizeof(uint8_t);	// количество видимых спутников
	int new_size = position + rec_size;
	EGTS_SR_EXT_POS_DATA_RECORD *pos_data_rec;
	uint8_t *puint8_t;

	packet_header->FDL += rec_size;
	record_header->RL += rec_size;
	subrecord_header->SRL = rec_size;

	pos_data_rec = (EGTS_SR_EXT_POS_DATA_RECORD *)&packet[position];
	pos_data_rec->FLG = B3;	// определяет наличие поля SAT

	puint8_t = (uint8_t *)(&pos_data_rec->FLG + sizeof(uint8_t));
	*puint8_t = record->satellites;

	return new_size;
}
//------------------------------------------------------------------------------

/* добавление в пакет поздаписи EGTS_SR_LIQUID_LEVEL_SENSOR
   с дополнительными данными навигации
*/
static int packet_add_subrecord_EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record)
{
	EGTS_PACKET_HEADER *packet_header = (EGTS_PACKET_HEADER *)packet;
	int rec_size = sizeof(EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD);
	int new_size = position + rec_size;
	EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD *pos_data_rec;

	packet_header->FDL += rec_size;
	record_header->RL += rec_size;
	subrecord_header->SRL = rec_size;

	pos_data_rec = (EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD *)&packet[position];

	/* поле LLSD имеет размер 4 байта (тип данных UINT) и содержит показания ДУЖ в формате,
	   определяемом полем LLSVU: нетарированное показание ДУЖ
	*/
	pos_data_rec->FLG = 0;
	pos_data_rec->MADDR = 0;	// адрес модуля, данные о показаниях ДУЖ с которого поступили
	pos_data_rec->LLSD = record->fuel[0];		// показания ДУЖ

	return new_size;
}
//------------------------------------------------------------------------------
