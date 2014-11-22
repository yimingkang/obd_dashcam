#ifndef __RPI_UART_H
#define __RPI_UART_H

#define BUFFER_SIZE 256
#define UPDATE_INTV 50000  			//Update interval in microseconds
#define IDLE_UPDATE_INTV 500000 	//Idle Update interval in microseconds
#define DEBUG 0
#define VERBOSE 0

void *collect_vehicle_data(void *);
int transmit(int, unsigned char *);
int receive(int , char *);
void receive_msg(int, char*);
void filter_ctrl_chars(char *);
int check_DTC(int, char *);
void request(int, char *);
void convert(char *, char *);
char convert_byte(char *);
void decode_DTC(int, char *, int );
void get_trouble_code(char *, char *);
void get_speed(int, char *, int *);
void get_RPM(int, char *, float *);

#endif
