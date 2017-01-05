/*
   loadconfig.c
   read config file, fill structures with initial parameters
*/
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>  /* errno */
#include "glonassd.h"
#include "forwarder.h"
#include "lib.h"

extern void logging(char *template, ...);

// load list of the forwarding terminals
int load_terminals(char *fname, ST_FORWARD_TERMINAL **list)
{
	char *buffer = calloc(FILENAME_MAX, sizeof(char));
	char *imei = calloc(SIZE_TRACKER_FIELD, sizeof(char));
	char *prot = calloc(SIZE_TRACKER_FIELD, sizeof(char));
	int i = 0;
	FILE *fp;

	if( fname[0] != '/' ) {	// file name without path
		// add start daemon path to file name
		snprintf(buffer, FILENAME_MAX, "%s/%s", stParams.start_path, fname);
	} else {
		snprintf(buffer, FILENAME_MAX, "%s", fname);
	}

	// open and scan file
	if( (fp = fopen(buffer, "r")) != NULL ) {
		while( NULL != fgets(buffer, FILENAME_MAX-1, fp) ) {
			switch(buffer[0]) {
			case ';':
			case '#':
			case ' ':
			case 13:
			case 10:
				continue;
			default:
				if( sscanf(buffer, "%15[^=]=%15s\n", imei, prot) == 2 ) {
					i++;
					*list = (ST_FORWARD_TERMINAL *)realloc(*list, i * sizeof(ST_FORWARD_TERMINAL));
					snprintf((*list)[i-1].imei, SIZE_TRACKER_FIELD, "%s", imei);
					snprintf((*list)[i-1].forward, SIZE_TRACKER_FIELD, "%s", prot);
				}
			}	// switch(buffer[0])

			memset(buffer, 0, FILENAME_MAX);
			memset(imei, 0, SIZE_TRACKER_FIELD);
			memset(prot, 0, SIZE_TRACKER_FIELD);
		}	// while( NULL != fgets

		fclose(fp);
	}	// if( (fp = fopen(
	else {
		logging("loadConfig: fopen(%s) error %d: %s\n", buffer, errno, strerror(errno));
	}

	free(buffer);
	free(imei);
	free(prot);
	return i;
}
//------------------------------------------------------------------------------

// fill timer structure ST_TIMER (glonassd.h)
void fill_timer(ST_TIMER *st_timer, char *params)
{
	int args, h, m, s, p;
	char *buffer;
	FILE *fp;

	// gutting line parameters
	if( strlen(params) < FILENAME_MAX ) {
		buffer = calloc(1, FILENAME_MAX);
		args = h = m = s = p = 0;

		// start time, period, script
		args = sscanf(params, "%2d:%2d:%2d,%5d,%s", &h, &m, &s, &p, buffer);
		if( args != 5 ) {
			// start time, script
			args = sscanf(params, "%2d:%2d:%2d,%s", &h, &m, &s, buffer);
			if( args != 4 ) {
				// start time, period
				args = sscanf(params, "%5d,%s", &p, buffer);
				if( args != 2 ) {
					logging("loadConfig: Error in config file: timer params %s\n", params);
				}
			}
		}

		// check the path to the script
		if( buffer[0] != '/' ) {	// is not a full path to the script
			// add the directory to launch the daemon file name
			snprintf(st_timer->script_path, FILENAME_MAX, "%s/%s", stParams.start_path, buffer);
		} else {
			snprintf(st_timer->script_path, FILENAME_MAX, "%s", buffer);
		}

		// check the availability of a script file
		if( (fp = fopen(st_timer->script_path, "r")) != NULL ) {
			fclose(fp);

			// first run, a second from the start of the day, or -1 if the run periodically
			if( args >= 4 )
				st_timer->start = 3600 * h + 60 * m + s;
			else
				st_timer->start = -1;

			// run period
			st_timer->period = p;
		} else {
			memset(st_timer->script_path, 0, FILENAME_MAX);
			logging("loadConfig: timer script '%s' not found\n", st_timer->script_path);
		}

		free(buffer);
	}	// if( strlen(params) < FILENAME_MAX )
	else {
		logging("loadConfig: Error in config file: timer params too long: '%s'", params);
	}
}
//------------------------------------------------------------------------------

// fill daemon config structure ST_CONFIG_SERVER (glonassd.h)
int set_config(char *section, char *param, char *value)
{
	char c;
	int i;

	if( !section || !strlen(section) || !param || !strlen(param) )
		return 0;

	if( strcmp(section, "server") == 0 ) {
		// main server configuration

		if( strcmp(param, "listen") == 0 ) {
			snprintf(stConfigServer.listen, INET_ADDRSTRLEN, "%s", value);
		}

		if( strcmp(param, "transmit") == 0 ) {
			snprintf(stConfigServer.transmit, INET_ADDRSTRLEN, "%s", value);
		}

		if( strcmp(param, "log_file") == 0 && strlen(value) > 0 ) {
			snprintf(stConfigServer.log_file, FILENAME_MAX, "%s", value);
		}

		if( strcmp(param, "log_enable") == 0 ) {
			if( strlen(value) )
				stConfigServer.log_enable = atoi(value);
			else
				stConfigServer.log_enable = 0;
		}

		if( strcmp(param, "log_maxsize") == 0 ) {
			if( sscanf(value, "%ld%c", &stConfigServer.log_maxsize, &c) == 2 ) {
				switch(c) {
				case 'k':
				case 'K':
					stConfigServer.log_maxsize *= 1024;
					break;
				case 'm':
				case 'M':
					stConfigServer.log_maxsize *= (1024 * 1024);
					break;
				case 'g':
				case 'G':
					stConfigServer.log_maxsize *= (1024 * 1024 * 1024);
				}	// switch(c)
			}	// if( sscanf(value, "%d%c"
		}	// f( strcmp(param, "log_maxsize") == 0 )

		if( strcmp(param, "db_type") == 0 ) {
			snprintf(stConfigServer.db_type, STRLEN, "%s", value);
		}

		if( strcmp(param, "db_host") == 0 ) {
			snprintf(stConfigServer.db_host, STRLEN, "%s", value);
		}

		if( strcmp(param, "db_port") == 0 ) {
			if( strlen(value) )
				stConfigServer.db_port = abs(atoi(value));
			else
				stConfigServer.db_port = 0;
		}

		if( strcmp(param, "db_name") == 0 ) {
			snprintf(stConfigServer.db_name, STRLEN, "%s", value);
		}

		if( strcmp(param, "db_schema") == 0 ) {
			snprintf(stConfigServer.db_schema, STRLEN, "%s", value);
		}

		if( strcmp(param, "db_user") == 0 ) {
			snprintf(stConfigServer.db_user, STRLEN, "%s", value);
		}

		if( strcmp(param, "db_pass") == 0 ) {
			snprintf(stConfigServer.db_pass, STRLEN, "%s", value);
		}

		if( strcmp(param, "socket_queue") == 0 ) {
			if( strlen(value) )
				stConfigServer.socket_queue = abs(atoi(value));
			else
				stConfigServer.socket_queue = 50;
		}

		if( strcmp(param, "socket_timeout") == 0 ) {
			if( strlen(value) )
				stConfigServer.socket_timeout = abs(MIN(atoi(value), 600));
			else
				stConfigServer.socket_timeout = 600;
		}

		if( strcmp(param, "forward_files_dir") == 0 && strlen(value) > 0 ) {
			snprintf(stConfigServer.forward_files, FILENAME_MAX, "%s", value);
		}

		if( strcmp(param, "timer") == 0 && strlen(value) ) {
			for(i = 0; i < TIMERS_MAX; i++) {
				if( !strlen(stConfigServer.timers[i].script_path) ) {
					fill_timer(&stConfigServer.timers[i], value);
					break;
				}
			}
		}	// if( strcmp(param, "timer")

	}	// if( strcmp(section, "server") == 0 )
	else if( strcmp(section, "forward") == 0 ) {

		if( strcmp(param, "list") == 0 ) {
			// load list of forwarding terminals
			stForwarders.listcount = load_terminals(value, &stForwarders.list);
		} else {
			// froward packets configuration
			stForwarders.count++;
			stForwarders.forwarder = (ST_FORWARDER *)realloc(stForwarders.forwarder, sizeof(ST_FORWARDER) * stForwarders.count);
			i = stForwarders.count - 1;
			memset(&stForwarders.forwarder[i], 0, sizeof(ST_FORWARDER));
			snprintf(stForwarders.forwarder[i].name, STRLEN, "%s", param);
			sscanf(value, "%15[^,],%5d,%1d,%15s",
					 stForwarders.forwarder[i].server,
					 &stForwarders.forwarder[i].port,
					 &stForwarders.forwarder[i].protocol,
					 stForwarders.forwarder[i].app);
			if(stForwarders.forwarder[i].protocol == 0)
				stForwarders.forwarder[i].protocol = SOCK_STREAM;
			else
				stForwarders.forwarder[i].protocol = SOCK_DGRAM;
		}

	}	// else if( strcmp(section, "forward") == 0 )
	else {
		// packets listeners configuration

		i = -1;
		if( !stListeners.count ) {
			stListeners.listener = (ST_LISTENER *)malloc(sizeof(ST_LISTENER));
			if(stListeners.listener) {
				i = stListeners.count;
				stListeners.count++;
				memset(&stListeners.listener[i], 0, sizeof(ST_LISTENER));
				snprintf(stListeners.listener[i].name, STRLEN, "%s", section);
			}
		} else {
			for(i=0; i<stListeners.count; i++) {
				if( strcmp(section, stListeners.listener[i].name) == 0 ) {
					break;
				}
			}	// for(i=0; i<stListeners.count; i++)

			if(i >= stListeners.count) {
				i = stListeners.count;
				stListeners.count++;
				stListeners.listener = (ST_LISTENER *)realloc(stListeners.listener, sizeof(ST_LISTENER)*stListeners.count);
				memset(&stListeners.listener[i], 0, sizeof(ST_LISTENER));
				snprintf(stListeners.listener[i].name, STRLEN, "%s", section);
			}
		}	// if( !stListeners.count )

		if( i != -1) {
			if( strcmp(param, "protocol") == 0 ) {
				if( strcmp(value, "TCP") == 0 || strcmp(value, "tcp") == 0 )
					stListeners.listener[i].protocol = SOCK_STREAM;
				else
					stListeners.listener[i].protocol = SOCK_DGRAM;
			}

			if( strcmp(param, "port") == 0 ) {
				if( strlen(value) )
					stListeners.listener[i].port = abs(atoi(value));
				else
					stListeners.listener[i].port = 0;
			}

			if( strcmp(param, "enabled") == 0 ) {
				if( strlen(value) )
					stListeners.listener[i].enabled = abs(atoi(value));
				else
					stListeners.listener[i].enabled = 0;
			}

			if( strcmp(param, "log_all") == 0 ) {
				if( strlen(value) )
					stListeners.listener[i].log_all = abs(atoi(value));
				else
					stListeners.listener[i].log_all = 0;
			}

			if( strcmp(param, "log_err") == 0 ) {
				if( strlen(value) )
					stListeners.listener[i].log_err = abs(atoi(value));
				else
					stListeners.listener[i].log_err = 0;
			}
		}	// if( i != -1)
		else {
			logging("set_config: Can't find or create config for service %s", section);
		}

	}	// else if( strcmp(section, "server") == 0 )

	return 1;
}	// set_config
//------------------------------------------------------------------------------

// open & read config file
int loadConfig(char *cPathToFile)
{
	int i, iRetval;
	char *cBuf, *cSection, *cName, *cValue;
	FILE *fHandle = fopen(cPathToFile, "r");
	if(!fHandle) {
		logging("loadConfig: Can't open file %s", cPathToFile);
		return 0;
	}

	cBuf = (char *)malloc(FILENAME_MAX);
	cSection = (char *)malloc(FILENAME_MAX);
	cName = (char *)malloc(FILENAME_MAX);
	cValue = (char *)malloc(FILENAME_MAX);

	// clear config
	memset(&stConfigServer, 0, sizeof(ST_CONFIG_SERVER));
	// set defaults
	stConfigServer.log_maxsize = 1024 * 1024;
	memset(stConfigServer.log_file, 0, FILENAME_MAX);
	strcpy(stConfigServer.log_file, "/var/log/glonassd.log");
	stConfigServer.socket_queue = 50;
	snprintf(stConfigServer.forward_files, FILENAME_MAX, "%s", stParams.start_path);

	iRetval = 1;
	i = 0;
	while( NULL != fgets(cBuf, FILENAME_MAX-1, fHandle) && iRetval == 1 ) {
		i++;

		switch(cBuf[0]) {
		case 10:
		case 13:
		case ' ':
		case '#':
		case ';':
			break;
		case '[':
			memset(cSection, 0, FILENAME_MAX);
			if( 1 != sscanf(cBuf, "[%[A-Za-z0-9]]", cSection) || !strlen(cSection) ) {
				logging("loadConfig: Error in config file, line %d", i);
				iRetval = 0;
			}
			break;
		default:
			memset(cName, 0, FILENAME_MAX);
			memset(cValue, 0, FILENAME_MAX);
			if( 2 == sscanf(cBuf, "%[A-Za-z0-9.,:_/]=%[A-Za-z0-9.,:_/]", cName, cValue) ||
					2 == sscanf(cBuf, "%[A-Za-z0-9.,:_/]= %[A-Za-z0-9.,:_/]", cName, cValue) ||
					2 == sscanf(cBuf, "%[A-Za-z0-9.,:_/] =%[A-Za-z0-9.,:_/]", cName, cValue) ||
					2 == sscanf(cBuf, "%[A-Za-z0-9.,:_/] = %[A-Za-z0-9.,:_/]", cName, cValue) ) {
				set_config(cSection, cName, cValue);
			}	// if( sscanf
			else {
				logging("loadConfig: Error in config file, line %d, param %s", i, cName);
				iRetval = 0;
			}
		}	// switch(cBuf[0])

	}	// while( NULL != fgets(cBuf, FILENAME_MAX, fHandle) )

	free(cBuf);
	free(cSection);
	free(cName);
	free(cValue);

	fclose(fHandle);
	return iRetval;
}	// loadConfig
//------------------------------------------------------------------------------


