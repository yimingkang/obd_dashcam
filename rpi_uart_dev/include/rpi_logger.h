#ifndef __RPI_LOGGER_H
#define __RPI_LOGGER_H


#define LOGPATH "./rpi_uart_log"
#define PROFILER_PATH "./rpi_profiler_log"
#define RPI_PROFILER
#define logit(sv, format, ...) do{ char *tmp = malloc(255); sprintf(tmp, format, ##__VA_ARGS__); vlog(tmp, sv, __FILE__, __func__, __LINE__); free(tmp);}while(0)
typedef enum {LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL, _LOG_UNKNOWN} LOG_SEVERITY;
void vlog(char *msg, LOG_SEVERITY sv, const char *file, const char *fn, int ln);
void profiler_init(char *param);
void _debug_profiler_logall(float val, int val1, int val2);
void profiler_logit(float value);
void dual_printf(FILE *fp,char *fmt,  ...);







#endif
