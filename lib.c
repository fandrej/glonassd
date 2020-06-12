/*
   lib.c
   helper routines
*/
#include <stdlib.h> /* malloc */
#include <stdio.h>	/* FILENAME_MAX */
#include <math.h>   /* add -lm to libs string when compile */
#include <string.h>
#include <time.h>
#include <ctype.h>	/* isalnum */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include "lib.h"

#ifndef MILE
#define MILE 1.852	// miles to kilometers coeff.
#endif

static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const double pi = 3.1415926540;	    // PI
static const double R = 6378137.0;	        // Earth radius, meters, WGS84
static const double d2r = 0.0174532925;	    // degree to radians coeff.
static const double r2d = 57.2957795131;	// radians to degree coeff.

/* Table of CRC values for high–order byte */
static unsigned char auchCRCHi[] = {
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
	0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
	0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
	0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
	0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
	0x40
} ;

/* Table of CRC values for low–order byte */
static char auchCRCLo[] = {
	0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
	0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
	0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
	0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
	0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
	0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
	0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
	0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
	0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
	0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
	0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
	0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
	0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
	0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
	0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
	0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
	0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
	0x40
};

// CP-1251 to UTF-8 convert table
static const int utf_table[128] = {
	0x82D0,0x83D0,0x9A80E2,0x93D1,0x9E80E2,0xA680E2,0xA080E2,0xA180E2,
	0xAC82E2,0xB080E2,0x89D0,0xB980E2,0x8AD0,0x8CD0,0x8BD0,0x8FD0,
	0x92D1,0x9880E2,0x9980E2,0x9C80E2,0x9D80E2,0xA280E2,0x9380E2,0x9480E2,
	0,0xA284E2,0x99D1,0xBA80E2,0x9AD1,0x9CD1,0x9BD1,0x9FD1,
	0xA0C2,0x8ED0,0x9ED1,0x88D0,0xA4C2,0x90D2,0xA6C2,0xA7C2,
	0x81D0,0xA9C2,0x84D0,0xABC2,0xACC2,0xADC2,0xAEC2,0x87D0,
	0xB0C2,0xB1C2,0x86D0,0x96D1,0x91D2,0xB5C2,0xB6C2,0xB7C2,
	0x91D1,0x9684E2,0x94D1,0xBBC2,0x98D1,0x85D0,0x95D1,0x97D1,
	0x90D0,0x91D0,0x92D0,0x93D0,0x94D0,0x95D0,0x96D0,0x97D0,
	0x98D0,0x99D0,0x9AD0,0x9BD0,0x9CD0,0x9DD0,0x9ED0,0x9FD0,
	0xA0D0,0xA1D0,0xA2D0,0xA3D0,0xA4D0,0xA5D0,0xA6D0,0xA7D0,
	0xA8D0,0xA9D0,0xAAD0,0xABD0,0xACD0,0xADD0,0xAED0,0xAFD0,
	0xB0D0,0xB1D0,0xB2D0,0xB3D0,0xB4D0,0xB5D0,0xB6D0,0xB7D0,
	0xB8D0,0xB9D0,0xBAD0,0xBBD0,0xBCD0,0xBDD0,0xBED0,0xBFD0,
	0x80D1,0x81D1,0x82D1,0x83D1,0x84D1,0x85D1,0x86D1,0x87D1,
	0x88D1,0x89D1,0x8AD1,0x8BD1,0x8CD1,0x8DD1,0x8ED1,0x8FD1
};

/*
   CRC Generation Function (CRC16)
   The function returns the CRC as a unsigned short type
*/
unsigned short CRC16( unsigned char *puchMsg, unsigned short usDataLen )
{
	unsigned char uchCRCHi = 0xFF ;	/* high byte of CRC initialized */
	unsigned char uchCRCLo = 0xFF ;	/* low byte of CRC initialized */
	unsigned uIndex ;	/* will index into CRC lookup table	*/

	while (usDataLen--) {	/* pass through message buffer */
		uIndex = uchCRCLo ^ *puchMsg++ ; /* calculate the CRC */
		uchCRCLo = uchCRCHi ^ auchCRCHi[uIndex] ;
		uchCRCHi = auchCRCLo[uIndex] ;
	}

	return (uchCRCHi << 8 | uchCRCLo);
}
//------------------------------------------------------------------------------


/*
   LRC Generation Function (CRC8)
   The function returns the LRC as a type unsigned char
*/
unsigned char CRC8(unsigned char *puchMsg, unsigned short usDataLen)
{
	unsigned char uchLRC = 0 ;

	while(usDataLen--)
		uchLRC += *puchMsg++ ;

	return((unsigned char)(-((char)uchLRC)));
}
//------------------------------------------------------------------------------


// rounding real numbers, steals from the Internet
double Round(double Value, int SignNumber)
{
	int i;
	int    Sign;
	double Fraction;
	double Integer;
	double Ratio;
	double Correction;

	// get sign
	if (Value < 0)
		Sign = -1;
	else
		Sign = 1;

	// get module
	Value *= Sign;

	Ratio = 1;
	Correction = 1;

	// for precision
	for (i = 0; i < SignNumber; i++) {
		Ratio *= 10;
		Correction /= 10;
	}

	// rounding correction
	Correction /= 1000;
	Value *= Ratio;
	Value += Correction;

	// get fractions
	Fraction = modf(Value, &Integer);

	// if fraction > 0.5 add 1
	Value = Integer;
	if(Fraction >= 0.5)
		Value += 1;
	Value /= Ratio;

	Value *= Sign;
	return( Value );
}
//---------------------------------------------------------------------------


size_t base64_chars_find(unsigned char c)
{
	size_t i;

	for(i = 0; i < strlen(base64_chars); i++) {
		if( base64_chars[i] == c )
			return i;
	}
	return -1;
}
//---------------------------------------------------------------------------

static inline int is_base64(unsigned char c)
{
	return (isalnum(c) || (c == '+') || (c == '/'));
}
//---------------------------------------------------------------------------

size_t base64_encode(unsigned char const* bytes_to_encode, unsigned char *ret, unsigned int retsize)
{
	unsigned int in_len, i = 0, j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	memset(ret, 0, retsize);

	in_len = strlen(bytes_to_encode);

	while (in_len-- && strlen(ret) < retsize) {
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for(i = 0; (i <4) ; i++)
				ret[strlen(ret)] = base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i) {
		for(j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1) && strlen(ret) < retsize; j++)
			ret[strlen(ret)] = base64_chars[char_array_4[j]];

		while((i++ < 3) && strlen(ret) < retsize)
			ret[strlen(ret)] = '=';

	}

	return strlen(ret);
}
//---------------------------------------------------------------------------

size_t base64_decode(unsigned char const *encoded_string, unsigned char *ret, unsigned int retsize)
{
	int in_len, i = 0, j = 0, in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];

	memset(ret, 0, retsize);

	in_len = strlen(encoded_string);

	while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_];
		in_++;
		if (i ==4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = base64_chars_find(char_array_4[i]);

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3) && strlen(ret) < retsize; i++)
				ret[strlen(ret)] = char_array_3[i];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j <4; j++)
			char_array_4[j] = 0;

		for (j = 0; j <4; j++)
			char_array_4[j] = base64_chars_find(char_array_4[j]);

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1) && strlen(ret) < retsize; j++) {
			ret[strlen(ret)] = char_array_3[j];
		}
	}

	return strlen(ret);
}
//---------------------------------------------------------------------------

// convertation from CP-1251 to UTF-8
// http://www.linux.org.ru/forum/development/3968525
void cp1251_to_utf8(char *out, const char *in)
{
	while (*in) {
		if (*in & 0x80) {
			int v = utf_table[(int)(0x7f & *in++)];
			if (!v)
				continue;
			*out++ = (char)v;
			*out++ = (char)(v >> 8);
			if (v >>= 16)
				*out++ = (char)v;
		} else
			*out++ = *in++;
	}	// while (*in)
	*out = 0;
}
//---------------------------------------------------------------------------

// log to file
void log2file(char *fname, void *content, size_t content_size)
{
	int fHandle;
	char fName[FILENAME_MAX];   // full filename buffer
	time_t t;
	struct tm local;

	if( content && content_size ) {
		t = time(NULL);
		localtime_r(&t, &local);

		memset(fName, 0, FILENAME_MAX);
		snprintf(fName, FILENAME_MAX, "%s_%02d%02d%02d_%02d%02d%02d",
					fname,
					local.tm_mday, local.tm_mon+1, local.tm_year-100,
					local.tm_hour, local.tm_min, local.tm_sec);

		if( (fHandle = open(fName, O_APPEND | O_CREAT | O_WRONLY, 0644)) != -1 ) {
			if( write(fHandle, content, content_size) < 0 ) {
                syslog(LOG_NOTICE, "%s write error %d: %s\n", fName, errno, strerror(errno));
			}
			close(fHandle);
		}

	}	// if( content && content_size )
}
//---------------------------------------------------------------------------

/*
   calculation of the distance and azimuth between two points, given coordinates
   https://www.kobzarev.com/programming/calculation-of-distances-between-cities-on-their-coordinates.html
   http://gis-lab.info/qa/great-circles.html

   dLon0, dLat0 - coordinates of the first point
   dLon1, dLat1 - coordinates of the second point
   dDist - a pointer to a variable that records the distance between points, in meters
   dBear - a pointer to the variable, which is recorded azimuth from the first to the second point, in degrees
*/
void geoDistance(double dLon0, double dLat0, double dLon1, double dLat1,
					  double *dDist, unsigned int *iBear)
{
	double dLon0r, dLat0r, dLon1r, dLat1r, // coordinates in radians
			 deltalon, deltalat; // variables to accelerate calculations

	dLon0r = d2r * dLon0;
	dLat0r = d2r * dLat0;
	dLon1r = d2r * dLon1;
	dLat1r = d2r * dLat1;

	deltalon = dLon1r - dLon0r;
	deltalat = dLat1r - dLat0r;

	// Formula haversine
	// mileage is calculated in meters (to calculate in kilometers. change R at km.)
	if( dDist ) {
		if( deltalon || deltalat )
			*dDist = Round((2.0 * asin(sqrt(pow(sin(deltalat/2.0), 2) + cos(dLat0r) * cos(dLat1r) * pow(sin(deltalon/2.0), 2)))) * R, 0);
		else
			*dDist = 0.0;
	}

	// direction calculation
	if( iBear ) {
		// http://edu.dvgups.ru/METDOC/ITS/GEOD/LEK/l2/L3_1.htm
		// Обратная геодезическая задача (Inverse position computation)
		// заключается в том, что при известных координатах точек А( XA, YA ) и В( XB, YB )
		// необходимо найти длину AB и направление линии АВ: румб и  дирекционный угол
		if( deltalat > 0.0 && deltalon >= 0.0 )	// 1 четверть (СВ) r = a
			*iBear = Round(atan(deltalon/deltalat) * r2d, 0);
		else if( deltalat < 0.0 && deltalon >= 0.0 )	// 2 четверть (ЮВ) a = 180° – r
			*iBear = 180 - Round(abs(atan(deltalon/deltalat)) * r2d, 0);
		else if( deltalat < 0.0 && deltalon < 0.0 )	// 3 четверть (ЮЗ) a = r + 180°
			*iBear = 180 + Round(abs(atan(deltalon/deltalat)) * r2d, 0);
		else if( deltalat > 0.0 && deltalon < 0.0 )	// 4 четверть (СЗ) a = 360° – r
			*iBear = 360 - Round(abs(atan(deltalon/deltalat)) * r2d, 0);
		else
			*iBear = 0;
	}	// if( dBear )
}
//---------------------------------------------------------------------------

// transform geodetic coordinates between the datums
// used Molodensky transformation
void Geo2Geo(int iSourDatum, int iDestDatum, double *pdLon, double *pdLat)
{
	double aW, alW, e2W, e2P, a, e2, da, de2, dDeltaF, dDeltaL;
	double B, L, M, N, H = 0.0;
	double SinB, CosB, SinL, CosL, Cos2B;
	double d1e2Cos2B, SinBCosB, e2SinBCosB, e2SinB2, e12;

	/*
	   ellipsoid parameters
	   WGS84
	   aEllips[WGS84][ELLIPS_AXISA] = 6378137.0;
	   aEllips[WGS84][ELLIPS_AXISB] = 6356752.3142;
	   aEllips[WGS84][ELLIPS_FLATT] = 1.0 / 298.257223563;	// flattening f = 1 / (a /(a – b))
	   aEllips[WGS84][ELLIPS_EXCEN] = 0.08181919;
	   aEllips[WGS84][ELLIPS_EXCEN2] = sqrt( pow(aEllips[WGS84][ELLIPS_EXCEN], 2) / (1.0 - pow(aEllips[ELLIPS_WGS84][ELLIPS_EXCEN], 2)));
	   PZ-90 parameters of GOST Earth
	   aEllips[PZ90][ELLIPS_AXISA] = 6378136.0;
	   aEllips[PZ90][ELLIPS_AXISB] = 6367558.4968;
	   aEllips[PZ90][ELLIPS_FLATT] = 1.0 / 298.25784;
	   aEllips[PZ90][ELLIPS_EXCEN] = 0.08182091;
	   aEllips[PZ90][ELLIPS_EXCEN2] = sqrt( pow(aEllips[PZ90][ELLIPS_EXCEN], 2) / (1.0 - pow(aEllips[ELLIPS_PZ90][ELLIPS_EXCEN], 2)));
	*/

	if( iSourDatum == iDestDatum )
		return;

	static const double ro = 206264.8062;   // number of seconds of arc in radians
	// Ellipsoid PZ-90.02
	static const double aP = 6378136.0; // semimajor axis
	static const double alP = 1.0 / 298.25784;  // Compression
	e2P = 2.0 * alP - pow(alP, 2);  // square of the eccentricity
	// Ellipsoid WGS84 (GRS80, these two are similar in most of the ellipsoid parameters)
	aW = 6378137.0; // semimajor axis
	alW = 1.0 / 298.257223563;  // Compression
	e2W = 2.0 * alW - pow(alW, 2);  // square of the eccentricity
	// Auxiliary values for transformation of ellipsoids
	a = (aP + aW) / 2;
	e2 = (e2P + e2W) / 2;
	da = aW - aP ;
	de2 = e2W - e2P ;
	// Transforming the linear elements in meters
	static const double dx = 23.92;
	static const double dy = -141.27;
	static const double dz = -80.9;
	// Angular elements of transformation, in seconds
	static const double wx = 0.0;
	static const double wy = 0.0;
	static const double wz = 0.0;
	// Differential difference scale
	static const double ms = 0.0;

	B = *pdLat * d2r;
	L = *pdLon * d2r;
	SinB = sin(B);
	CosB = cos(B);
	Cos2B = cos(2.0 * B);
	d1e2Cos2B = (1.0 + e2 * Cos2B);
	SinBCosB = SinB * CosB;
	e2SinBCosB = e2 * SinBCosB;
	e2SinB2 = 1.0 - e2 * pow(SinB, 2);
	e12 = 1.0 - e2;
	SinL = sin(L);
	CosL = cos(L);

	N = a * pow(e2SinB2, -0.5);

	M = a * e12 / pow(e2SinB2, 1.5);

	dDeltaF = ro / (M + H) * (N / a * e2SinBCosB * da + (pow(N, 2) /
									  pow(a, 2) + 1) * N * SinBCosB * de2 / 2 - (dx * CosL +
											  dy * SinL) * SinB + dz * CosB) - wx * SinL *
				 d1e2Cos2B + wy * CosL * d1e2Cos2B -
				 ro * ms * e2SinBCosB;

	dDeltaL = ro / ((N + H) * CosB) * (-dx * SinL + dy * CosL) + tan(B) *
				 e12 * (wx * CosL + wy * SinL) - wz;

	if( iSourDatum == WGS84 ) {
		*pdLat -= Round(dDeltaF / 3600.0, 7);
		*pdLon -= Round(dDeltaL / 3600.0, 7);
	} else { //if( iSourDatum == PZ90 )
		*pdLat += Round(dDeltaF / 3600.0, 7);
		*pdLon += Round(dDeltaL / 3600.0, 7);
	}
}
//------------------------------------------------------------------------------

/*
   calculate intervals in seconds
*/
unsigned long long int seconds(void)
{
	time_t t;
	struct tm stm;

	t = time(NULL);
	localtime_r(&t, &stm);

	return( (stm.tm_year * 365 + stm.tm_yday) * 86400 + stm.tm_hour * 3600 + stm.tm_min * 60 + stm.tm_sec );
}
//------------------------------------------------------------------------------
