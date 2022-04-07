PROJECT = glonassd

CC = gcc
LIBS = -lpthread -L/usr/lib/nptl -rdynamic -ldl -lrt -lm
INCLUDE = -I/usr/include/nptl
# https://gcc.gnu.org/onlinedocs/gcc/Option-Summary.html#Option-Summary
CFLAGS = -std=gnu99 -D_REENTERANT -m64
SOCFLAGS = -std=gnu99 -D_REENTERANT -m64 -fpic -Wall
# https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
OPTIMIZE = -O2 -flto -g0
#OPTIMIZE = -O0 -flto -g -fno-stack-protector
# https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html#Debugging-Options
DEBUG = -g

SOURCE = glonassd.c loadconfig.c todaemon.c logger.c worker.c lib.c forwarder.c

HEADERS = $(wildcard *.h)

# ATTENTION: $(LIBS) MUST BE AFTER $(SOURCE) !!!

$(PROJECT): $(SOURCE) $(HEADERS)
	$(CC) $(CFLAGS) $(OPTIMIZE) $(INCLUDE) $(SOURCE) $(LIBS) -o $(PROJECT)

run: $(PROJECT)
	./$(PROJECT) start

# shared library for decode/encode GALILEO
galileo: galileo.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) galileo.c -o galileo.o
	$(CC) -shared -o galileo.so galileo.o
	rm galileo.o

# shared library for decode/encode SAT-LITE/SAT-LITE2
satlite: satlite.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) satlite.c -o satlite.o
	$(CC) -shared -o satlite.so satlite.o
	rm satlite.o

# shared library for decode/encode ARNAVI 4
arnavi: arnavi.c arnavi.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) arnavi.c -o arnavi.o
	$(CC) -shared -o arnavi.so arnavi.o
	rm arnavi.o

# shared library for decode/encode ARNAVI 5
arnavi5: arnavi5.c arnavi.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) arnavi5.c -o arnavi5.o
	$(CC) -shared -o arnavi5.so arnavi5.o
	rm arnavi5.o

# shared library for decode/encode Wialon IPS
wialonips: wialonips.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) wialonips.c -o wialonips.o
	$(CC) -shared -o wialonips.so wialonips.o
	rm wialonips.o

# shared library for decode/encode GPS-101 - GPS-103
gps103: gps103.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) gps103.c -o gps103.o
	$(CC) -shared -o gps103.so gps103.o
	rm gps103.o

# shared library for decode/encode SOAP
soap: soap.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) soap.c -o soap.o
	$(CC) -shared -o soap.so soap.o
	rm soap.o

# shared library for decode/encode EGTS
egts: egts.c egts.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) egts.c -o egts.o
	$(CC) -shared -o egts.so egts.o
	rm egts.o

# shared library for decode/encode FAVW
favw: favw.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) favw.c -o favw.o
	$(CC) -shared -o favw.so favw.o
	rm favw.o

# shared library for decode/encode FAVA
fava: fava.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) fava.c -o fava.o
	$(CC) -shared -o fava.so fava.o
	rm fava.o

# shared library for decode/encode TQ GPRS
tqgprs: tqgprs.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) tqgprs.c -o tqgprs.o
	$(CC) -shared -o tqgprs.so tqgprs.o
	rm tqgprs.o

# shared library for decode/encode GOSAFE
gosafe: gosafe.c de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) gosafe.c -o gosafe.o
	$(CC) -shared -o gosafe.so gosafe.o
	rm gosafe.o

# shared library for test/log protocol
prototest: prototest.c de.h glonassd.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) prototest.c -o prototest.o
	$(CC) -shared -o prototest.so prototest.o
	rm prototest.o

# shared library for database PostgreSQL
pg: pg.c glonassd.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) $(INCLUDE) -I/usr/include/postgresql pg.c $(LIBS) -o pg.o -lpq
	$(CC) -shared -o pg.so pg.o -lpq
	rm pg.o

# shared library for database REDIS
rds: rds.c glonassd.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) $(INCLUDE) -I/usr/local/include/hiredis rds.c -I/usr/local/include/json-c/ $(LIBS) -o rds.o -lhiredis -ljson-c
	$(CC) -shared -o rds.so rds.o -lhiredis
	rm rds.o

# shared library for database ORACLE
oracle: oracle.c glonassd.h de.h logger.h
	$(CC) -c $(SOCFLAGS) $(OPTIMIZE) $(INCLUDE) -I/usr/local/include oracle.c $(LIBS) -o oracle.o -lodpic
	$(CC) -shared -o oracle.so oracle.o -lodpic
	rm oracle.o

# all
all: $(PROJECT) galileo satlite wialonips gps103 soap egts arnavi arnavi5 favw fava tqgprs prototest pg rds oracle

clean:
	rm -f *.o
