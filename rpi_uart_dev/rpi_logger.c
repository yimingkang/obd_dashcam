#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>
#include "include/rpi_logger.h"

#define DUAL_PRINTF



FILE *logfile = NULL;
FILE *profiler_logfile = NULL;

void profiler_init(char *param){
	if(!profiler_logfile){
		profiler_logfile = fopen(PROFILER_PATH, "a");
		if(!profiler_logfile){
            fprintf(stderr, "ERROR: Cannot open file %s for writing!\n", PROFILER_PATH);
			exit(-1);
		}
		fprintf(profiler_logfile, "%s,%s\n", "timestamp", param);
	}
	
}

void _debug_profiler_logall(float value, int value1, int value2){

	struct timespec clock;
    int ret = clock_gettime(CLOCK_REALTIME, &clock);
    if(ret){
		fprintf(stderr, "ERROR: Unable to get system time!\n");
        exit(-1);
    }
    int sec  = clock.tv_sec;
    int nsec = clock.tv_nsec ;

	if(!profiler_logfile){
		if(logfile){
			logit(LOG_FATAL, "profiler_logfile has not been opened!");
		}
		fprintf(stderr, "ERROR: profiler_logfile has not been opened!");
	}
	// here timestamps are in 1/100 seoncds
	fprintf(profiler_logfile, "%lu,%f,%d,%d\n", sec*100 + nsec/10000000, value, value1, value2);
}

void profiler_logit(float value){

	struct timespec clock;
    int ret = clock_gettime(CLOCK_REALTIME, &clock);
    if(ret){
		fprintf(stderr, "ERROR: Unable to get system time!\n");
        exit(-1);
    }
    int sec  = clock.tv_sec;
    int nsec = clock.tv_nsec ;

	if(!profiler_logfile){
		if(logfile){
			logit(LOG_FATAL, "profiler_logfile has not been opened!");
		}
		fprintf(stderr, "ERROR: profiler_logfile has not been opened!");
	}
	// here timestamps are in 1/100 seoncds
	fprintf(profiler_logfile, "%lu,%f\n", sec*100 + nsec/10000000, value);
}

void vlog(char *msg, LOG_SEVERITY sv, const char *file, const char *fn, int ln){
    if(!logfile){
        logfile = fopen(LOGPATH, "a");
        if(!logfile){
            fprintf(stderr, "ERROR: Cannot open file %s for writing!\n", LOGPATH);
            exit(-1);
        }
    }
    time_t rawtime;
    struct tm * timeinfo;

    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    char *cur_time = asctime(timeinfo);
    cur_time[strlen(cur_time) - 1] = '\0';

    switch(sv){
        case LOG_INFO:
            dual_printf(logfile, "[%s] INFO:    %s\n", cur_time, msg);
            fflush(logfile);
            break;
        case LOG_WARNING:
            dual_printf(logfile, "[%s] WARNING: %s: %s\n", cur_time, file,  msg);
            fflush(logfile);
            break;
        case LOG_ERROR:
            dual_printf(logfile, "[%s] ERROR:   %s::%s()::%d : %s\n", cur_time, file, fn, ln, msg);
            fflush(logfile);
            break;
        case LOG_FATAL:
            dual_printf(logfile, "[%s] FATAL:   %s::%s()::%d : %s (exiting)\n", cur_time, file, fn, ln, msg);
            fflush(logfile);
            exit(-1);
        default:
            dual_printf(logfile, "[%s] UNKNOWN: %s::%s()::%d : %s (exiting)\n", cur_time, file, fn, ln, msg);
            fflush(logfile);
            exit(-1);
    }
}

void dual_printf(FILE *fp,char *fmt,  ...)
{
    va_list ap;

	#ifdef DUAL_PRINTF
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
	#endif

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
}

