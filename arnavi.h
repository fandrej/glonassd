#ifndef __ARNAVI__
#define __ARNAVI__

/* обработчик сообщений ARNAVI 4
Описание типов данных:
#include <stdint.h> // uint8_t, etc...
GCC/standard C (stdint.h)																				Borland Builder C++
uint8_t - 8ми битный целый беззнаковый. 	unsigned char          unsigned __int8
int8_t - 8ми битный целый знаковый.       char                   __int8
uint16_t - 16ти битный целый беззнаковый. unsigned short int     unsigned __int16
int16_t - 16ти битный целый знаковый.     short int              __int16
uint32_t - 32х битный целый беззнаковый.  unsigned int           unsigned __int32
int32_t - 32х битный целый знаковый.      int                    __int32
uint64_t - 64х битный целый беззнаковый.  unsigned long long int
int64_t - 64х битный целый знаковый.      long long int

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

Structure of parcel:
1
HEADER

2
PACKAGE
	RECORD 1
	...
	RECORD n
0x5D

3
HEADER
PACKAGE
	RECORD 1
	...
	RECORD n
0x5D

RECORD {
	uint8_t		type;
	uint16_t  field_len;	// in bytes
	uint32_t	time;
	FIELD[]	rec;
  uint8_t		CRC;
}

FIELD {
	uint8_t		type;
	byte[4]		value;
}
*/

// Identificators of the parcel type
#define ARNAVI_ID_HEADER	0xFF
#define ARNAVI_ID_PACKAGE	0x5B

#pragma pack( push, 1 )
typedef struct {
    uint8_t		IH;	// идентификатор HEADER
    uint8_t		PV;	// версия протокола (0x22 - HEADER, 0x23 - HEADER2)
    uint64_t	ID;	// уникальный ID или IMEI модема (0x00030FC9F5450CF3 = 861785007918323)
} ARNAVI_HEADER;
#pragma pack( pop )
// sizeof(ARNAVI_HEADER) = 10

#pragma pack( push, 1 )
typedef struct {
    uint8_t		TYPE;	// type of packet
    uint16_t	SIZE;	// data length
    uint32_t	TIME;	// unixtime
} ARNAVI_RECORD_HEADER;
#pragma pack( pop )
// sizeof(ARNAVI_RECORD_HEADER) = 7

#endif
