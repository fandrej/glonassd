/* различные общеупотребительные функции */
#ifndef __MYLIB__
#define __MYLIB__

#define WGS84 (0)
#define PZ90 (1)
/*
#define PI	(3.141592654)
#define D2R	(0.0174532925)	// Константа для преобразования градусов в радианы
#define R2D (57.2957795131)	// Константа для преобразования радиан в градусы
*/

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define min(X,Y) MIN(X,Y)
#define MAX(X,Y) ((X) < (Y) ? (Y) : (X))
#define max(X,Y) max(X,Y)
#define BETWEEN(V,X,Y) ((X) <= (V) && (V) <= (Y))
#define between(V,X,Y) BETWEEN(V,X,Y)

unsigned short CRC16 ( unsigned char *puchMsg, unsigned short usDataLen );
unsigned char CRC8(unsigned char *puchMsg, unsigned short usDataLen);
size_t base64_encode(unsigned char const* bytes_to_encode, unsigned char *ret, unsigned int retsize);
size_t base64_decode(unsigned char const *encoded_string, unsigned char *ret, unsigned int retsize);
double Round(double Value, int SignNumber);
void geoDistance(double dLon0, double dLat0, double dLon1, double dLat1, double *dDist, unsigned int *iBear);
void cp1251_to_utf8(char *out, const char *in);
void log2file(char *fname, void *content, size_t content_size);
void Geo2Geo(int iSourDatum, int iDestDatum, double *pdLon, double *pdLat);
unsigned long long int seconds(void);

#endif
