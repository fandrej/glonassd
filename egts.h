/*
Описание типов данных:
#include <stdint.h> // uint8_t, etc...
GCC/standard C (stdint.h)																				Borland Builder C++
uint8_t - 8ми битный целый беззнаковый. 	unsigned char          unsigned __int8
int8_t - 8ми битный целый знаковый.       char                   __int8
uint16_t - 16ти битный целый беззнаковый. unsigned short int     unsigned __int16
int16_t - 16ти битный целый знаковый.     short int              __int16
uint32_t - 32х битный целый беззнаковый.  unsigned int           unsigned __int32
int32_t - 32х битный целый знаковый.      int                    __int32

32-bit data types, sizes, and ranges
http://cppstudio.com/post/271/
--------------------------------------------------------------------------------
Type								Size (bits)	Range																Sample applications
--------------------------------------------------------------------------------
unsigned char						8			0 <= X <= 255													Small numbers and full PC character set
char										8			-128 <= X <= 127											Very small numbers and ASCII characters
short int								16		-32,768 <= X <= 32,767								Counting, small numbers, loop control
unsigned short int			16    0 <= X <= 65 535
unsigned int						32		0 <= X <= 4,294,967,295								Large numbers and loops
int											32		-2,147,483,648 <= X <= 2,147,483,647	Counting, small numbers, loop control
unsigned long						32		0 <= X <= 4,294,967,295								Astronomical distances
enum										32		-2,147,483,648 <= X <= 2,147,483,647	Ordered sets of values
long [int]							32		-2,147,483,648 <= X <= 2,147,483,647	Large numbers, populations
long long int       		64    -9223372036854775808 <= X <= 9223372036854775807
unsigned long long int	64    0 <= X <= 8446744073709551615
float										32		1.18  10^-38 < |X| < 3.40  10^38			Scientific (7-digit) precision)
double									64		2.23  10^-308 < |X| < 1.79  10^308		Scientific (15-digit precision)
long double							80		3.37  10^-4932 < |X| < 1.18  10^4932	Financial (18-digit precision)
*/

/* EGTS:
D:\Work\Borland\Satellite\egts
D:\Work\Borland\Satellite\egts\EGTS_PT_APPDATA.xls
Упаковка: (Пример передачи данных о местоположении и состоянии транспортного средства.doc)
EGTS_PACKET {
	EGTS_PACKET_HEADER
  SFRD	Структура данных, зависящая от типа Пакета EGTS_PACKET_HEADER.PT
  SFRCS Контрольная сумма SFRD CRC-16
}
Общая длина пакета Протокола Транспортного Уровня не превышает значения 65535 байт
АТ - абонентский терминал
ТП - телематическая платформа
*/

#ifndef __EGTS__
#define __EGTS__

// для проверки битов
#define B15 32768
#define B14 16384
#define B13 8192
#define B12 4096
#define B11 2048
#define B10 1024
#define B9  512
#define B8  256
#define B7 	128
#define B6 	64
#define B5 	32
#define B4 	16
#define B3 	8
#define B2 	4
#define B1 	2
#define B0 	1

// Тип пакета Транспортного Уровня
#define EGTS_PT_RESPONSE 0
#define EGTS_PT_APPDATA 1
#define EGTS_PT_SIGNED_APPDATA 2

/* пакет Протокола Транспортного Уровня
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_transport_v.1.6_RUS.pdf
8.1.2 ФОРМАТ ПАКЕТА
*/
#pragma pack( push, 1 )
typedef struct {
    uint8_t		PRV;	// (Protocol Version)
    uint8_t		SKID;	// (Security Key ID)
    uint8_t		PRF;	// Flags:
    uint8_t		HL;		// Длина заголовка Транспортного Уровня в байтах с учётом байта контрольной суммы (поля HCS)
    uint8_t		HE;		// метод кодирования следующей за данным параметром части заголовка Транспортного Уровня = 0
    uint16_t	FDL;	// размер в байтах поля данных SFRD, если 0: отправка EGTS_PT_RESPONSE с RPID=PID и PR=EGTS_PC_OK
    uint16_t	PID;	// номер пакета Транспортного Уровня от 0 до 65535
    uint8_t		PT;		// Тип пакета Транспортного Уровня
    uint8_t		HCS;	// Контрольная сумма заголовка CRC-8
} EGTS_PACKET_HEADER;
#pragma pack( pop )

/* PRF:
Name	Bit Value
PRF		7-6	префикс заголовка Транспортного Уровня и для данной версии должен содержать значение 00
RTE		5		определяет необходимость дальнейшей маршрутизации = 1, то необходима
ENA		4-3	шифрование данных из поля SFRD, значение 0 0 , то данные в поле SFRD не шифруются
CMP		2		сжатие данных из поля SFRD, = 1, то данные в поле SFRD считаются сжатыми
PR		1-0	приоритет маршрутизации, 1 0 – средний
*/
/* PT
0 – EGTS_PT_RESPONSE (подтверждение на пакет Транспортного Уровня);
1 – EGTS_PT_APPDATA (пакет, содержащий данные Протокола Уровня Поддержки Услуг);
2 – EGTS_PT_SIGNED_APPDATA (пакет, содержащий данные Протокола Уровня Поддержки Услуг с цифровой подписью);
*/


/* SFRD
В зависимости от типа пакета EGTS_PACKET_HEADER.PT структура поля SFRD имеет различный
формат.
EGTS_PACKET_HEADER.PT = EGTS_PT_APPDATA
	SFRD {
		SDR 1	(Service Data Record)
		[
		SDR 2
		SDR n
		]
	}
где SDR = {
	EGTS_SDR
	[	SRD 1 (Subrecord Data) 	]
	[	SRD 2	]
	...
}
где SDR = EGTS_SR_POS_DATA_RECORD, например

EGTS_PACKET_HEADER.PT = EGTS_PT_RESPONSE
	SFRD {
  	EGTS_PT_RESPONSE_HEADER
		[
		SDR 1	(Service Data Record)
		SDR 2
		SDR n
		]
	}
где SDR = ?

EGTS_PACKET_HEADER.PT = EGTS_PT_SIGNED_APPDATA
обрабатывать не будем.
*/

/* Сервисы
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.1 СПИСОК СЕРВИСОВ
*/
#define EGTS_AUTH_SERVICE         (1)
#define EGTS_TELEDATA_SERVICE     (2)
#define EGTS_COMMANDS_SERVICE     (4)
#define EGTS_FIRMWARE_SERVICE     (9)
#define EGTS_ECALL_SERVICE        (10)


/* SFRD = EGTS_PT_RESPONSE
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_transport_v.1.6_RUS.pdf
8.2.2 СТРУКТУРА ДАННЫХ ПАКЕТА EGTS_PT_RESPONSE
*/
#pragma pack( push, 1 )
typedef struct {
    uint16_t	RPID;	// Идентификатор пакета Транспортного Уровня, подтверждение на который сформировано EGTS_PACKET_HEADER.PID
    uint8_t		PR;		// Код результата обработки части Пакета, относящейся к Транспортному Уровню
} EGTS_PT_RESPONSE_HEADER;
#pragma pack( pop )

/*
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.2.1.1 ПОДЗАПИСЬ EGTS_SR_RECORD_RESPONSE
*/
#pragma pack( push, 1 )
typedef struct {
    uint16_t	CRN;	// номер подтверждаемой записи (значение поля RN из обрабатываемой записи)
    uint8_t		RST;	// статус обработки записи
} EGTS_SR_RECORD_RESPONSE_RECORD;
#pragma pack( pop )

/*
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.2.1.8 ПОДЗАПИСЬ EGTS_SR_RESULT_CODE
*/
#pragma pack( push, 1 )
typedef struct {
    uint8_t	RCD;	// код, определяющий результат выполнения операции авторизации ()
} EGTS_SR_RESULT_CODE_RECORD;
#pragma pack( pop )



/* D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
5.3 ОБЩАЯ СТРУКТУРА ПОДЗАПИСЕЙ
*/
#pragma pack( push, 1 )
typedef struct {
    uint8_t		SRT;	// тип подзаписи EGTS_SR_TERM_IDENTITY/EGTS_SR_POS_DATA/... (see RD_HEADER.SRT: below)
    uint16_t	SRL;	// длина данных в байтах подзаписи SRD (Subrecord Data)
} EGTS_SUBRECORD_HEADER;
#pragma pack( pop )

/* SFRD = EGTS_PT_APPDATA
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_transport_v.1.6_RUS.pdf
8.2.1 СТРУКТУРА ДАННЫХ ПАКЕТА EGTS_PT_APPDATA
далее читать
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
где SDR = запись Протокола Уровня Поддержки Услуг
5.2.2 СТРУКТУРА ЗАПИСИ
*/
#pragma pack( push, 1 )
typedef struct {
    uint16_t	RL;		// размер данных из поля RD
    uint16_t	RN;		// номер записи от 0 до 65535
    uint8_t		RFL;	// битовые флаги
    // необязательные поля
    uint32_t	OID;	// идентификатор объекта, сгенерировавшего данную запись, или для которого данная запись предназначена
    uint32_t	EVID;	// уникальный идентификатор события
    uint32_t  TM;		// время формирования записи на стороне Отправителя (секунды с 00:00:00 01.01.2010 UTC)
    // обязательные поля
    uint8_t		SST;	// идентификатор тип Сервиса-отправителя, сгенерировавшего данную запись (=EGTS_TELEDATA_SERVICE)
    uint8_t		RST;	// идентификатор тип Сервиса-получателя данной записи (=EGTS_TELEDATA_SERVICE)
} EGTS_RECORD_HEADER;
#pragma pack( pop )

/* RFL:
Name	Bit Value
SSOD	7		1 = Сервис-отправитель расположен на стороне АТ, 0 = Сервис- отправитель расположен на ТП
RSOD	6		1 = Сервис-получатель расположен на стороне АТ, 0 = Сервис-получатель расположен на ТП
GRP		5		принадлежность передаваемых данных определённой группе идентификатор которой указан в поле OID
RPP		4-3	приоритет обработки данной записи 00 – наивысший 01 – высокий 10 – средний 11 – низкий
TMFE	2		наличие в данном пакете поля TM 1 = присутствует 0 = отсутствует
EVFE	1		наличие в данном пакете поля EVID 1 = присутствует 0 = отсутствует
OBFE	0		наличие в данном пакете поля OID 1 = присутствует 0 = отсутствует
*/


/* RST = EGTS_TELEDATA_SERVICE
subrecords:
*/
#define EGTS_SR_RECORD_RESPONSE			0
#define EGTS_SR_POS_DATA						16
#define EGTS_SR_EXT_POS_DATA				17
#define EGTS_SR_AD_SENSORS_DATA			18
#define EGTS_SR_COUNTERS_DATA				19
#define EGTS_SR_ACCEL_DATA					20
#define EGTS_SR_STATE_DATA					21	// http://forum.gurtam.com/viewtopic.php?pid=48848#p48848
#define EGTS_SR_LOOPIN_DATA 				22
#define EGTS_SR_ABS_DIG_SENS_DATA		23
#define EGTS_SR_ABS_AN_SENS_DATA		24
#define EGTS_SR_ABS_CNTR_DATA				25
#define EGTS_SR_ABS_LOOPIN_DATA			26
#define EGTS_SR_LIQUID_LEVEL_SENSOR	27
#define EGTS_SR_PASSENGERS_COUNTERS	28

/* SRD (Subrecord Data)
EGTS_SR_POS_DATA_RECORD
Используется абонентским терминалом при передаче основных данных определения местоположения
http://www.zakonprost.ru/content/base/part/1038460
*/
#pragma pack( push, 1 )
typedef struct {
    uint32_t NTM; 	// 4 байта Navigation Time , seconds since 00:00:00 01.01.2010 UTC
    uint32_t LAT;		// 4 байта Latitude, degree,  (WGS - 84) / 90 * 0xFFFFFFFF
    uint32_t LONG;	// 4 байта Longitude, degree,  (WGS - 84) / 180 * 0xFFFFFFFF
    uint8_t  FLG;		// 1 байт Flagsб см. ниже
    uint16_t SPD;		// 2 байта, см. ниже
    uint8_t  DIR;     // направление движения
    uint8_t  ODM[3];  // пройденное расстояние (пробег) в км, с дискретностью 0,1 км;
    uint8_t  DIN;     // битовые флаги, определяют состояние основных дискретных входов 1 ... 8
    uint8_t  SRC;     // источник (событие), инициировавший посылку данной навигационной информации (http://www.zakonprost.ru/content/base/part/1038460 Таблица N 3)
    // опциональные поля, могут отсутствовать
    //uint8_t		ALT[3];	// высота над уровнем моря, м (опциональный параметр, наличие которого определяется битовым флагом ALTE);
    //uint16_t	SRCD;		// (см. ниже) данные, характеризующие источник (событие) из поля src. Наличие и интерпретация значения данного поля определяется полем src
} EGTS_SR_POS_DATA_RECORD;
#pragma pack( pop )

/* FLG:
Name	Bit Value
VLD   :0	признак "валидности" координатных данных: 1 - данные "валидны";
CS    :1	тип используемой системы: 0 - WGS-84; 1 - ПЗ-90.02;
FIX   :2	тип определения координат: 0 - 2D fix; 1 - 3D fix;
BB    :3	признак отправки данных из памяти ("черный ящик"): 0 - актуальные данные
MV    :4	признак движения: 1 - движение
LAHS  :5	определяет полушарие широты:  0 - северная 1 - южная
LOHS  :6	определяет полушарие долготы 0 - восточная 1 - западная
ALTE  :7	определяет наличие поля ALT в подзаписи 0 - не передается
*/
/* SPD:
Name	Bit Value
SPD 	:0-13	14 младших бит, скорость в км/ч с дискретностью 0,1 км/ч
ALTS  :14		определяет высоту относительно уровня моря и имеет смысл только при установленном флаге ALTE: 0 - точка выше уровня моря; 1 - ниже уровня моря;
DIRH  :15		(Direction the Highest bit) старший бит (8) параметра DIR;
*/


/* SRD (Subrecord Data)
EGTS_SR_EXT_POS_DATA_RECORD
Используется абонентским терминалом при передаче дополнительных данных
http://www.zakonprost.ru/content/base/part/1038460
*/
#pragma pack( push, 1 )
typedef struct {
    uint8_t	FLG;	// битовые флаги
    // опциональные поля, могут отсутствовать
    //uint16_t	VDOP;	// снижение точности в вертикальной плоскости (значение, умноженное на 100)
    //uint16_t  HDOP;	// снижение точности в горизонтальной плоскости (значение, умноженное на 100) (а Galileo умножает на 10)
    //uint16_t  PDOP;	// снижение точности по местоположению (значение, умноженное на 100)
    //uint8_t	SAT;	// количество видимых спутников
    //uint16_t  NS;		// битовые флаги, характеризующие используемые навигационные спутниковые системы
} EGTS_SR_EXT_POS_DATA_RECORD;
#pragma pack( pop )

/* FLG:
Name	Bit Value
VFE 	0		- (VDOP Field Exists) определяет наличие поля VDOP: 0 - не передается
HFE 	1		- (HDOP Field Exists) определяет наличие поля HDOP: 0 - не передается
PFE 	2		- (PDOP Field Exists) определяет наличие поля PDOP: 0 - не передается
SFE 	3		- (Satellites Field Exists) определяет наличие поля SAT: 0 - не передается
NSFE 	4		- (Navigation System Field Exists) определяет наличие поля NS: 0 - не передается
остальные биты не используются
*/
/* NS:
0	- система не определена;
1 - ГЛОНАСС;
2 - GPS;
4 - Galileo;
8 - Compass;
16 - Beidou;
32 - DORIS;
64 - IRNSS;
128 - QZSS.
Остальные значения зарезервированы.
*/

/* SRD (Subrecord Data)
EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD
Используется абонентским терминалом при передаче данных датчиков уровня жидкости
http://www.zakonprost.ru/content/base/part/1038460
*/
#pragma pack( push, 1 )
typedef struct {
    uint8_t		FLG;		// битовые флаги
    uint16_t	MADDR;	// адрес модуля, данные о показаниях ДУЖ с которого поступили в абонентский терминал
    uint32_t	LLSD;		// показания ДУЖ в формате, определяемом флагом RDF
} EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD;
#pragma pack( pop )

/* FLG:
Name	Bit Value
LLSN	0-2	порядковый номер датчика, 3 бита
RDF		3		флаг, определяющий формат поля LLSD данной подзаписи:
					0 - поле LLSD имеет размер 4 байта (тип данных UINT) и содержит показания ДУЖ в формате,
					определяемом полем LLSVU;
					1 - поле LLSD содержит данные ДУЖ в неизменном виде, как они поступили из внешнего
					порта абонентского терминала (размер поля LLSD при этом определяется исходя из
					общей длины данной подзаписи и размеров расположенных перед LLSD полей).
LLSVU	4-5	битовый флаг, определяющий единицы измерения показаний ДУЖ:
					00 - нетарированное показание ДУЖ.
					01 - показания ДУЖ в процентах от общего объема емкости;
					10 - показания ДУЖ в литрах с дискретностью в 0,1 литра.
LLSEF	6		битовый флаг, определяющий наличие ошибок при считывании значения датчика уровня жидкости
			7		не используется
*/



/* RST = EGTS_AUTH_SERVICE
subrecords:
*/
#define EGTS_SR_TERM_IDENTITY     (1)
#define EGTS_SR_MODULE_DATA       (2)
#define EGTS_SR_VEHICLE_DATA      (3)
#define EGTS_SR_AUTH_PARAMS       (6)
#define EGTS_SR_AUTH_INFO         (7)
#define EGTS_SR_SERVICE_INFO      (8)
#define EGTS_SR_RESULT_CODE       (9)

/* SRD (Subrecord Data)
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.2 EGTS_AUTH_SERVICE
6.2.1.2 ПОДЗАПИСЬ EGTS_SR_TERM_IDENTITY
*/
#define EGTS_IMEI_LEN     15U
#define EGTS_IMSI_LEN     16U
#define EGTS_LNGC_LEN     3U
#define EGTS_NID_LEN      3U
#define EGTS_MSISDN_LEN   15U

#pragma pack( push, 1 )
typedef struct {
    uint32_t	TID;	// уникальный идентификатор, назначаемый при программировании АТ
    uint8_t		FLG;	// Flags
    /*[
    	uint16_t	HDID;	// идентификатор «домашней» ТП
    	char			IMEI[EGTS_IMEI_LEN];	// идентификатор мобильного устройства
    	char			IMSI[EGTS_IMSI_LEN];	// идентификатор мобильного абонента
    	char			LNGC[EGTS_LNGC_LEN];	// код языка, предпочтительного к использованию на стороне АТ, по ISO 639-2, например, rus – русский
    	uint8_t		NID[3];	// идентификатор сети оператора, в которой зарегистрирован АТ на данный момент
    	uint16_t	BS;	// максимальный размер буфера приёма АТ в байтах.
    	char 			MSISDN[EGTS_MSISDN_LEN];	// телефонный номер мобильного абонента. При невозможности определения данного параметра, устройство должно заполнять данное поле значением 0 во всех 15-ти символах
    	]*/
} EGTS_SR_TERM_IDENTITY_RECORD;
#pragma pack( pop )

/* FLG:
Name	Bit Value
HDIDE :0	– битовый флаг, который определяет наличие поля HDID в подзаписи (1=передаётся, 0=не передаётся)
IMEIE :1	– битовый флаг, который определяет наличие поля IMEI в подзаписи (1=передаётся, 0=не передаётся)
IMSIE :2	– битовый флаг, который определяет наличие поля IMSI в подзаписи (1=передаётся, 0=не передаётся)
LNGCE :3	– битовый флаг, который определяет наличие поля LNGC в подзаписи (1=передаётся, 0=не передаётся)
SSRA 	:4	– битовый флаг предназначен для определения алгоритма использования Сервисов (1=«простой» алгоритм, 0=«запросов» на использование сервисов)
NIDE 	:5	– битовый флаг определяет наличие поля NID в подзаписи (1=передаётся, 0=не передаётся)
BSE 	:6	– битовый флаг, определяющий наличие поля BS в подзаписи (1=передаётся, 0=не передаётся)
MNE 	:7	- битовый флаг, определяющий наличие поля MSISDN в подзаписи (1=передаётся, 0=не передаётся)
*/


/*
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.2.1.6 ПОДЗАПИСЬ EGTS_SR_AUTH_INFO
*/
#pragma pack( push, 1 )
typedef struct {
    char	UNM[32];	// User Name, имя пользователя
    char	D0;				// Delimiter, разделитель строковых параметров (всегда имеет значение 0)
    char UPSW[32];	// User Password, пароль пользователя
    char	D1;				// Delimiter
    char SS[255];		/* Server Sequence, специальная серверная последовательность байт, передаваемая в подзаписи
											EGTS_SR_AUTH_PARAMS (необязательное поле, наличие зависит от используемого
											алгоритма шифрования) */
    char	D2;				// Delimiter
} EGTS_SR_AUTH_INFO_RECORD;
#pragma pack( pop )


/* RST = EGTS_COMMANDS_SERVICE
http://www.zakonprost.ru/content/base/part/1038461
http://docs.cntd.ru/document/1200119664
D:\Work\Borland\Satellite\egts\EGTS\EGTS 1.6\RUS\protocol_EGTS_services_v.1.6_p1_RUS.pdf
6.3 EGTS_COMMANDS_SERVICE
6.3.2 ОПИСАНИЕ КОМАНД, ПАРАМЕТРОВ И ПОДТВЕРЖДЕНИЙ
subrecords:
*/
#define EGTS_SR_COMMAND_DATA			51

/* Формат команд терминала (поле CD структуры EGTS_SR_COMMAND_DATA_RECORD)
Таблица 18: Формат команд терминала
*/
#pragma pack( push, 1 )
typedef struct {
    uint16_t	ADR;		// (Address)(Action)
    uint8_t		SZ_ACT;	// (Size)
    uint16_t	CCD;		// (Command Code)
    /* необязательные поля
    uint8_t		DT[65200];	// (Data)
    */
} EGTS_SR_COMMAND_DATA_FIELD;
#pragma pack( pop )

/* CT_CCT:
Name	Bit Value
SZ		:7-4	объём памяти для параметра (используется совместно с действием ACT=3
ACT		:3-0 – описание действия, используется в случае типа команды (поле CT=CT_COM подзаписи EGTS_SR_COMMAND _DATA).
			Значение поля может быть одним из следующих вариантов:
			0 = параметры команды. Используется для передачи параметров для команды, определяемой кодом из поля CCD
			1 = запрос значения. Используется для запроса информации, хранящейся в АТ. Запрашиваемый параметр определяется кодом из поля CCD
			2 = установка значения. Используется для установки нового значения определённому параметру в АТ. Устанавливаемый параметр определяется кодом из поля CCD, а его значение полем DT
			3 = добавление нового параметра в АТ. Код нового параметра указывается в поле CCD, его тип в поле SZ, а значение в поле DT
			4 = удаление имеющегося параметра из АТ. Код удаляемого параметра указывается в поле CCD
*/

// 6.3.1.1 ПОДЗАПИСЬ EGTS_SR_COMMAND_DATA
#pragma pack( push, 1 )
typedef struct {
    uint8_t		CT_CCT;	// CT-тип команды, CCT-тип подтверждения (имеет смысл для типов команд CT_COMCONF, CT_MSGCONF, CT_DELIV)
    uint32_t	CID;		// (Command Identifier) идентификатор команды, сообщения. Значение из данного поля должно быть использовано стороной, обрабатывающей/выполняющей команду или сообщение, для создания подтверждения. Подтверждение должно содержать в поле CID то же значение, что содержалось в самой команде или сообщении при отправке
    uint32_t	SID;		// (Source Identifier) идентификатор отправителя (уровня прикладного ПО, например, уникальный идентификатор пользователя в системе диспетчеризации) данной команды или подтверждения
    uint8_t		ACFE;		//  Flags
    /* необязательные поля
    uint8_t		CHS;		// (Charset) кодировка символов, используемая в поле CD, содержащем тело команды. При отсутствии данного поля по умолчанию должна использоваться кодировка CP-1251
    uint8_t		ACL;		// (Authorization Code Length) длина в байтах поля AC, содержащего код авторизации на стороне получателя
    uint8_t		AC[255];	// (Authorization Code) код авторизации, использующийся на принимающей стороне (АТ)
    EGTS_SR_COMMAND_DATA_FIELD	CD;	// (Command Data) тело команды, параметры, данные возвращаемые на команду-запрос, использующие кодировку из поля CHS, или значение по умолчанию Формат команды описан в Таблице 18
    */
} EGTS_SR_COMMAND_DATA_RECORD;
#pragma pack( pop )

/* CT_CCT:
Name	Bit Value
CT	:7-4	- Command Type:
	0001 = (16) = CT_COMCONF - подтверждение о приёме, обработке или результат выполнения команды
	0010 = (32) = CT_MSGCONF - подтверждение о приёме, отображении и/или обработке информационного сообщения
	0011 = (48) = CT_MSGFROM - информационное сообщение от АТ 0100 = CT_MSGTO - информационное сообщение для вывода на устройство отображения АТ
	0101 = (80) = CT_COM - команда для выполнения на АТ
	0110 = (96) = CT_DELCOM - удаление из очереди на выполнение переданной ранее команды
	0111 = (112) = CT_SUBREQ - дополнительный подзапрос для выполнения (к переданной ранее команде)
	1000 = (128) = CT_DELIV - подтверждение о доставке команды или информационного сообщения
CCT	:3-0 - Command Confirmation Type:
	0000 = (0) = CC_OK - успешное выполнение, положительный ответ;
	0001 = (1) = CC_ERROR - обработка завершилась ошибкой
	0010 = (2) = CC_ILL - команда не может быть выполнена по причине отсутствия в списке разрешённых (определённых протоколом) команд или отсутствия разрешения на выполнение данной команды
	0011 = (3) = CC_DEL - команда успешно удалена
	0100 = (4) = CC_NFOUND - команда для удаления не найдена
	0101 = (5) = CC_NCONF - успешное выполнение, отрицательный ответ
	0110 = (6) = CC_INPROG - команда передана на обработку, но для её выполнения требуется длительное время
*/
/* ACFE:
Name	Bit Value
CHSFE	:0  – (Charset Field Exists) битовый флаг, определяющий наличие поля CHS в подзаписи 1 = поле CHS присутствует в подзаписи 0 = поле CHS отсутствует в подзаписи
ACFE	:1 – (Authorization Code Field Exists) битовый флаг, определяющий наличие полей ACL и AC в подзаписи 1 = поля ACL и AC присутствуют в подзаписи 0 = поля ACL и AC отсутствуют в подзаписи
остальные не используются
*/

/* команды EGTS_SR_COMMAND_DATA_FIELD.CCD
http://www.zakonprost.ru/content/base/part/1038461
*/
#define EGTS_FLEET_GET_DOUT_DATA 0x000B
#define EGTS_FLEET_GET_POS_DATA  0x000C
#define EGTS_FLEET_GET_SENSORS_DATA 0x000D
#define EGTS_FLEET_GET_LIN_DATA 0x000E
#define EGTS_FLEET_GET_CIN_DATA 0x000F
#define EGTS_FLEET_GET_STATE 0x0010
#define EGTS_FLEET_ODOM_CLEAR 0x0011



// EGTS_PT_RESPONSE_HEADER.PR: ПРИЛОЖЕНИЕ 1 - КОДЫ РЕЗУЛЬТАТОВ ОБРАБОТКИ
#define EGTS_PC_OK							0		// успешно обработано
#define EGTS_PC_IN_PROGRESS			1		// в процессе обработки
#define EGTS_PC_UNS_PROTOCOL		128	// неподдерживаемый протокол
#define EGTS_PC_DECRYPT_ERROR		129	// ошибка декодирования
#define EGTS_PC_PROC_DENIED			130	// обработка запрещена
#define EGTS_PC_INC_HEADERFORM 	131	// неверный формат заголовка
#define EGTS_PC_INC_DATAFORM 		132	// неверный формат данных
#define EGTS_PC_UNS_TYPE 				133	// неподдерживаемый тип
#define EGTS_PC_NOTEN_PARAMS 		134	// неверное количество параметров
#define EGTS_PC_DBL_PROC 				135	// попытка повторной обработки
#define EGTS_PC_PROC_SRC_DENIED 136 // обработка данных от источника запрещена
#define EGTS_PC_HEADERCRC_ERROR 137 // ошибка контрольной суммы заголовка
#define EGTS_PC_DATACRC_ERROR 	138 // ошибка контрольной суммы данных
#define EGTS_PC_INVDATALEN 			139 // некорректная длина данных
#define EGTS_PC_ROUTE_NFOUND 		140 // маршрут не найден
#define EGTS_PC_ROUTE_CLOSED 		141 // маршрут закрыт
#define EGTS_PC_ROUTE_DENIED 		142 // маршрутизация запрещена
#define EGTS_PC_INVADDR 				143 // неверный адрес
#define EGTS_PC_TTLEXPIRED 			144 // превышено количество ретрансляции данных
#define EGTS_PC_NO_ACK 					145 // нет подтверждения
#define EGTS_PC_OBJ_NFOUND 			146 // объект не найден
#define EGTS_PC_EVNT_NFOUND 		147 // событие не найдено
#define EGTS_PC_SRVC_NFOUND 		148 // сервис не найден
#define EGTS_PC_SRVC_DENIED 		149 // сервис запрещён
#define EGTS_PC_SRVC_UNKN 			150 // неизвестный тип сервиса
#define EGTS_PC_AUTH_DENIED 		151 // авторизация запрещена
#define EGTS_PC_ALREADY_EXISTS 	152 // объект уже существует
#define EGTS_PC_ID_NFOUND 			153 // идентификатор не найден
#define EGTS_PC_INC_DATETIME 		154 // неправильная дата и время
#define EGTS_PC_IO_ERROR 				155 // ошибка ввода/вывода
#define EGTS_PC_NO_RES_AVAIL 		156 // недостаточно ресурсов
#define EGTS_PC_MODULE_FAULT 		157 // внутренний сбой модуля
#define EGTS_PC_MODULE_PWR_FLT 	158 // сбой в работе цепи питания модуля
#define EGTS_PC_MODULE_PROC_FLT 159 // сбой в работе микроконтроллера модуля
#define EGTS_PC_MODULE_SW_FLT 	160 // сбой в работе программы модуля
#define EGTS_PC_MODULE_FW_FLT 	161 // сбой в работе внутреннего ПО модуля
#define EGTS_PC_MODULE_IO_FLT 	162 // сбой в работе блока ввода/вывода модуля
#define EGTS_PC_MODULE_MEM_FLT 	163 // сбой в работе внутренней памяти модуля
#define EGTS_PC_TEST_FAILED 		164 // тест не пройден

const unsigned char CRC8Table[256] = {
    0x00, 0x31, 0x62, 0x53, 0xC4, 0xF5, 0xA6, 0x97,
    0xB9, 0x88, 0xDB, 0xEA, 0x7D, 0x4C, 0x1F, 0x2E,
    0x43, 0x72, 0x21, 0x10, 0x87, 0xB6, 0xE5, 0xD4,
    0xFA, 0xCB, 0x98, 0xA9, 0x3E, 0x0F, 0x5C, 0x6D,
    0x86, 0xB7, 0xE4, 0xD5, 0x42, 0x73, 0x20, 0x11,
    0x3F, 0x0E, 0x5D, 0x6C, 0xFB, 0xCA, 0x99, 0xA8,
    0xC5, 0xF4, 0xA7, 0x96, 0x01, 0x30, 0x63, 0x52,
    0x7C, 0x4D, 0x1E, 0x2F, 0xB8, 0x89, 0xDA, 0xEB,
    0x3D, 0x0C, 0x5F, 0x6E, 0xF9, 0xC8, 0x9B, 0xAA,
    0x84, 0xB5, 0xE6, 0xD7, 0x40, 0x71, 0x22, 0x13,
    0x7E, 0x4F, 0x1C, 0x2D, 0xBA, 0x8B, 0xD8, 0xE9,
    0xC7, 0xF6, 0xA5, 0x94, 0x03, 0x32, 0x61, 0x50,
    0xBB, 0x8A, 0xD9, 0xE8, 0x7F, 0x4E, 0x1D, 0x2C,
    0x02, 0x33, 0x60, 0x51, 0xC6, 0xF7, 0xA4, 0x95,
    0xF8, 0xC9, 0x9A, 0xAB, 0x3C, 0x0D, 0x5E, 0x6F,
    0x41, 0x70, 0x23, 0x12, 0x85, 0xB4, 0xE7, 0xD6,
    0x7A, 0x4B, 0x18, 0x29, 0xBE, 0x8F, 0xDC, 0xED,
    0xC3, 0xF2, 0xA1, 0x90, 0x07, 0x36, 0x65, 0x54,
    0x39, 0x08, 0x5B, 0x6A, 0xFD, 0xCC, 0x9F, 0xAE,
    0x80, 0xB1, 0xE2, 0xD3, 0x44, 0x75, 0x26, 0x17,
    0xFC, 0xCD, 0x9E, 0xAF, 0x38, 0x09, 0x5A, 0x6B,
    0x45, 0x74, 0x27, 0x16, 0x81, 0xB0, 0xE3, 0xD2,
    0xBF, 0x8E, 0xDD, 0xEC, 0x7B, 0x4A, 0x19, 0x28,
    0x06, 0x37, 0x64, 0x55, 0xC2, 0xF3, 0xA0, 0x91,
    0x47, 0x76, 0x25, 0x14, 0x83, 0xB2, 0xE1, 0xD0,
    0xFE, 0xCF, 0x9C, 0xAD, 0x3A, 0x0B, 0x58, 0x69,
    0x04, 0x35, 0x66, 0x57, 0xC0, 0xF1, 0xA2, 0x93,
    0xBD, 0x8C, 0xDF, 0xEE, 0x79, 0x48, 0x1B, 0x2A,
    0xC1, 0xF0, 0xA3, 0x92, 0x05, 0x34, 0x67, 0x56,
    0x78, 0x49, 0x1A, 0x2B, 0xBC, 0x8D, 0xDE, 0xEF,
    0x82, 0xB3, 0xE0, 0xD1, 0x46, 0x77, 0x24, 0x15,
    0x3B, 0x0A, 0x59, 0x68, 0xFF, 0xCE, 0x9D, 0xAC
};

const unsigned short Crc16Table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};


// функции общие для encode/decode
int packet_create(char *buffer, uint8_t pt);
int packet_finalize(char *buffer, int pointer);

// функции для decode
int responce_add_responce(char *buffer, int pointer, uint16_t pid, uint8_t pr);
int responce_add_record(char *buffer, int pointer, uint16_t crn, uint8_t rst);
int responce_add_result(char *buffer, int pointer, uint8_t rcd);
int responce_add_subrecord_EGTS_SR_COMMAND_DATA(char *buffer, int pointer, EGTS_SR_COMMAND_DATA_RECORD *cmdrec);
unsigned char CRC8EGTS(unsigned char *lpBlock, unsigned char len);
unsigned short CRC16EGTS(unsigned char * pcBlock, unsigned short len);
int Parse_EGTS_PACKET_HEADER(ST_ANSWER *answer, char *pc, int parcel_size);
int Parse_EGTS_RECORD_HEADER(EGTS_RECORD_HEADER *rec_head, EGTS_RECORD_HEADER *st_header, ST_ANSWER *answer);
int Parse_EGTS_SR_TERM_IDENTITY(EGTS_SR_TERM_IDENTITY_RECORD *record, ST_ANSWER *answer);
int Parse_EGTS_SR_POS_DATA(EGTS_SR_POS_DATA_RECORD *posdata, ST_RECORD *record, ST_ANSWER *answer);
int Parse_EGTS_SR_EXT_POS_DATA(EGTS_SR_EXT_POS_DATA_RECORD *posdata, ST_RECORD *record);
int Parse_EGTS_SR_LIQUID_LEVEL_SENSOR(int rlen, EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD *posdata, ST_RECORD *record);
int Parse_EGTS_SR_COMMAND_DATA(ST_ANSWER *answer, EGTS_SR_COMMAND_DATA_RECORD *record);

// функции для encode
static int packet_add_record_header(char *packet, int position, uint8_t sst, uint8_t rst);
static int packet_add_subrecord_header(char *packet, int position, EGTS_RECORD_HEADER *record_header, uint8_t srt);
static int packet_add_subrecord_EGTS_SR_TERM_IDENTITY(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, char *imei);
static int packet_add_subrecord_EGTS_SR_POS_DATA_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record);
static int packet_add_subrecord_EGTS_SR_EXT_POS_DATA_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record);
static int packet_add_subrecord_EGTS_SR_LIQUID_LEVEL_SENSOR_RECORD(char *packet, int position, EGTS_RECORD_HEADER *record_header, EGTS_SUBRECORD_HEADER *subrecord_header, ST_RECORD *record);

#endif

/* EGTS_SR_POS_DATA_RECORD.SRCD
http://zakonbase.ru/content/part/1191927
Таблица N 3. Список источников посылок координатных данных Сервиса EGTS_TELEDATA_SERVICE
Код 	Описание
0 	таймер при включенном зажигании
1 	пробег заданной дистанции
2 	превышение установленного значения угла поворота
3 	ответ на запрос
4 	изменение состояния входа X
5 	таймер при выключенном зажигании
6 	отключение периферийного оборудования
7 	превышение одного из заданных порогов скорости
8 	перезагрузка центрального процессора (рестарт)
9 	перегрузка по выходу Y
10 	сработал датчик вскрытия корпуса прибора
11 	переход на резервное питание/отключение внешнего питания
12 	снижение напряжения источника резервного питания ниже порогового значения
13 	нажата "тревожная кнопка"
14 	запрос на установление голосовой связи с оператором
15 	экстренный вызов
16 	появление данных от внешнего сервиса
17 	зарезервировано
18 	зарезервировано
19 	неисправность резервного аккумулятора
20 	резкий разгон
21 	резкое торможение
22 	отключение или неисправность навигационного модуля
23 	отключение или неисправность датчика автоматической идентификации события ДТП
24 	отключение или неисправность антенны GSM/UMTS
25 	отключение или неисправность антенны навигационной системы
26 	зарезервировано
27 	снижение скорости ниже одного из заданных порогов
28 	перемещение при выключенном зажигании
29 	таймер в режиме "экстренное слежение"
30 	начало/окончание навигации
31 	"нестабильная навигация" (превышение порога частоты прерывания режима навигации при включенном зажигании или режиме экстренного слежения)
32 	установка IP соединения
33 	нестабильная регистрация в сети подвижной радиотелефонной связи
34 	"нестабильная связь" (превышение порога частоты прерывания/восстановления IP соединения при включенном зажигании или режиме экстренного слежения)
35 	изменение режима работы
*/
