#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>			//Used for UART
#include <fcntl.h>			//Used for UART
#include <termios.h>        //Used for UART
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "include/rpi_logger.h"
#include "include/rpi_uart.h"
#include "../include/video_record.h"
#include "../include/sys_stat.h"



void *collect_vehicle_data(void *overlay){
	int uart_fd = UART_init();
	char *msg   = (char *) calloc(BUFFER_SIZE, sizeof(char));
	if(!msg){
		logit(LOG_FATAL,"Undable to allocate mem");
		exit(-1);
	}

	logit(LOG_INFO, "--- Program %s has been invoked ---", __FILE__);

	#ifdef FRESH_START
    // recieve init message
    logit(LOG_INFO, "Checking for an init message");
    char *init =  (char *) malloc(sizeof(char) * BUFFER_SIZE);

    if(!init){
        logit(LOG_FATAL,"Undable to allocate mem");
        exit(-1);
    }


    memset(init, '\0', BUFFER_SIZE);
    receive_msg(uart_fd, init);
    logit(LOG_INFO, "Init message recieved: \"%s\"", init);
    // free init and clear mem for msg
    free(init);
	#else
    logit(LOG_INFO, "Not a fresh start, skipping welcome message check");
	#endif

   	sprintf(msg, "ATZ"); 
	logit(LOG_INFO, "Resetting device");
	request(uart_fd, msg);

   	sprintf(msg, "AT E 0"); 
	request(uart_fd, msg);
	
	logit(LOG_INFO, "Turning echo off");
	do{
		usleep(UPDATE_INTV);
		// turn echo off
    	sprintf(msg, "AT E 0"); 
		request(uart_fd, msg);
	} while(msg[0] != 'O' || msg[1] != 'K');

    // now everything should be all setup
    logit(LOG_INFO, "Init is completed");

	// First check DTC
	check_DTC(uart_fd, msg);

	overlay_info_t *info_ptr = (overlay_info_t *) overlay;
	pthread_mutex_lock(&(info_ptr->overlay_info_lock));
	strcpy(info_ptr->err, msg);
	pthread_mutex_unlock(&(info_ptr->overlay_info_lock));

	// then loop + check speed and engine RPM
	float rpm 	= 0.0;
	int speed 	= 0;

	#ifdef PRI_PROFILER
	//Initialize profiler to record temperature
	profiler_init("Temp,rpm,speed");
	#endif

	while(1){
		usleep(UPDATE_INTV);
		get_RPM(uart_fd, msg, &rpm);
		get_speed(uart_fd, msg, &speed);

    	pthread_mutex_lock(&(info_ptr->overlay_info_lock));
    	info_ptr->free_space  = get_perc_free_int("/home/pi");
    	info_ptr->tmp         = get_sys_tmp();
    	info_ptr->speed		  = speed;
    	info_ptr->rpm		  = rpm;
    	// use strcpy to get err from info
		#ifdef RPI_PROFILER
		_debug_profiler_logall(info_ptr->tmp, info_ptr->rpm, info_ptr->speed);
		#endif
    	pthread_mutex_unlock(&(info_ptr->overlay_info_lock));
	}
	
    

	free(msg);
	close(uart_fd);
	return NULL;	
}

void get_speed(int uart_fd, char *msg, int *speed){
	sprintf(msg, "01 0D 1");
	request(uart_fd, msg);
	if(strlen(msg) != 6){
		logit(LOG_WARNING, "Incorrect message length, message was : %s", msg);
		if(strncmp(msg, "NODATA", 6)){
			logit(LOG_INFO, "Engine has been shut off, decreasing update frequency");
			*speed = 0;
			usleep(IDLE_UPDATE_INTV);
			return;
		}
	}
	if(convert_byte(msg) != 0x41 || convert_byte(&msg[2]) != 0x0D){
		free(msg);
		logit(LOG_FATAL, "Incorrect message returned, message was: %s", msg);
	}

	*speed = convert_byte(&msg[4]);
}

void get_RPM(int uart_fd, char *msg, float *rpm){
	sprintf(msg, "01 0C 1");
	request(uart_fd, msg);
	if(strlen(msg) != 8){
		logit(LOG_WARNING, "Incorrect message length, message was : %s", msg);
		if(strncmp(msg, "NODATA", 6)){
			logit(LOG_INFO, "Engine has been shut off, decreasing update frequency");
			*rpm = 0;
			usleep(IDLE_UPDATE_INTV);
			return;
		}
	}
	if(convert_byte(msg) != 0x41 || convert_byte(&msg[2]) != 0x0C){
		free(msg);
		logit(LOG_FATAL, "Incorrect message returned, message was: %s", msg);
	}

	*rpm = (convert_byte(&msg[4]) * 256 + convert_byte(&msg[6]))/4 ;
}

void get_trouble_code(char *msg, char *dtc){
	char *ptr = dtc;
	char byte;
	// get first byte after msg
	convert(msg, &byte);
	switch ((byte & 0xC) >> 2){
		// check A7A6	
		case 0x0:
			*ptr = 'P';
			break;
		case 0x1:
			*ptr = 'C';
			break;
		case 0x2:
			*ptr = 'B';
			break;
		case 0x3:
			*ptr = 'U';
			break;
		default:
			free(msg);
			logit(LOG_FATAL, "Unable to decode byte 0x%X", byte);
	}

	ptr++;
	*ptr = '0' + (byte & 0x3);

    ptr++;
    sprintf(ptr, "%s", &msg[1]);
	
	logit(LOG_INFO, "DTC code retrieved: %s", dtc);
	strcpy(msg, dtc);
}

void decode_DTC(int uart_fd, char *msg, int num_DTC){
    sprintf(msg, "03"); 
    request(uart_fd, msg);

	if(strlen(msg) < 8){
		logit(LOG_ERROR, "Message returned is too short, unable to retrieve DTC");
		return;
	}
	if(convert_byte(msg) == 0x43 && convert_byte(&msg[2]) == 0x01){
		// one message 
		// example: p0441\0 total 6 bytes
		char *dtc = calloc(6, sizeof(char));
		
		// pass in the 5th byte in the stream, start decoding
		get_trouble_code(&msg[4], dtc);

		// free trouble code
		free(dtc);
	}
}

int check_DTC(int uart_fd, char *msg){
    sprintf(msg, "01 01"); 
    request(uart_fd, msg);

	if(strlen(msg) != 12){
		free(msg);
		logit(LOG_FATAL, "Did not recieve the correct number of bytes \"%s\"", msg);
	}else if(convert_byte(&msg[0]) != 0x41 || convert_byte(&msg[2]) != 0x01){
		free(msg);
		logit(LOG_FATAL, "Incorrect response recieved, aborting");
	}else{
		char byte_A = convert_byte(&msg[4]); 
		if(byte_A >> 7){
			// if A7 != 0
			byte_A = byte_A & 0x7F;
			// retreive A6-A0
			logit(LOG_WARNING, "MIL should be on, there's %d emission-related DTCs available for display", (int) byte_A);
			if(byte_A > 1){
				logit(LOG_WARNING, "Program is not yet capable of dealing with >1 trouble codes, decode yourself");
				sprintf(msg, "%dDTC", byte_A);
				return (int) byte_A;
			}

			decode_DTC(uart_fd, msg, byte_A);
			return 1;
		}
		else{
			logit(LOG_INFO, "No MIL should be on, no emission-related DTC");
			sprintf(msg, "NOERROR");
			return 0;
		}
	}
}

char convert_byte(char *from){
	//convert first 2 bytes of from to a 1-byte hex
	char ret = '\0';
	convert(from, &ret);
	ret = ret << 4;
	convert((from + 1), &ret);
	return ret;
}

void convert(char *from, char *to){
	switch(*from){
		case '0':
			*to = *to | 0x0;
			break;
		case '1':
			*to = *to | 0x1;
			break;
		case '2':
			*to = *to | 0x2;
			break;
		case '3':
			*to = *to | 0x3;
			break;
		case '4':
			*to = *to | 0x4;
			break;
		case '5':
			*to = *to | 0x5;
			break;
		case '6':
			*to = *to | 0x6;
			break;
		case '7':
			*to = *to | 0x7;
			break;
		case '8':
			*to = *to | 0x8;
			break;
		case '9':
			*to = *to | 0x9;
			break;
		case 'A':
		case 'a':
			*to = *to | 0xA;
			break;
		case 'B':
		case 'b':
			*to = *to | 0xB;
			break;
		case 'C':
		case 'c':
			*to = *to | 0xC;
			break;
		case 'D':
		case 'd':
			*to = *to | 0xD;
			break;
		case 'E':
		case 'e':
			*to = *to | 0xE;
			break;
		case 'F':
		case 'f':
			*to = *to | 0xF;
			break;
		default:
			logit(LOG_FATAL, "Cannot convert %c to a valid hex number", *from);
	}
}

void strip_space(char *msg){
	char *put   = msg;
	char *read  = msg;
	while(*read != '\0'){
		if(*read != ' '){
			*put = *read;
			put++;
		}
		read++;
	}
	*put = '\0';
}


void filter_ctrl_chars(char *msg){
    // clear control charactors, replace with ' '
    char *ptr = msg;
    while(*ptr != '\0' && *(ptr+1) != '\0'){
        if(*ptr == '\r' || *ptr == '\n' || *ptr == '>') *ptr = ' ';
		if(*ptr == '\0'){
			// TODO: FIXME: !!!CAUTION!!!
			// as per documentation, \0 might be returned under certain conditions.
			logit(LOG_ERROR, "Got non-terminating \0 in I/O stream, removing \0");
			*ptr = ' ';
		}
		ptr++;
    }
	// remove tailing space
	while(--ptr && *ptr == ' '){
		*ptr = '\0';
	}
}

int UART_init(){
	//-------------------------
	//----- SETUP USART 0 -----
	//-------------------------
	//At bootup, pins 8 and 10 are already set to UART0_TXD, UART0_RXD (ie the alt0 function) respectively
	int uart0_filestream = -1;

    uart0_filestream = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY  /* | O_NDELAY*/);		//Open in blocking read/write mode
    if (uart0_filestream == -1)
    {
    	//ERROR - CAN'T OPEN SERIAL PORT
    	logit(LOG_FATAL,"Unable to open UART.  Ensure it is not in use by another application");
    }
    
    struct termios options;
    tcgetattr(uart0_filestream, &options);
    options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;		//<Set baud rate
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    tcflush(uart0_filestream, TCIFLUSH);
    tcsetattr(uart0_filestream, TCSANOW, &options);
	return uart0_filestream;
}

void request(int uart0_filestream, char *msg){
	if(!msg){
		logit(LOG_FATAL, "Null msg is being send, aborting");
	}
	if(msg[strlen(msg) - 1] == '\r'){
		free(msg);
		logit(LOG_FATAL, "Message should not end with \\r, request() will add padding automatically, aborting");
	}

    if(VERBOSE) logit(LOG_INFO, "Transmitting command \"%s\"", msg);
	int len = strlen(msg);
	msg[len] = '\r';
	msg[len+1] = '\0';

	transmit(uart0_filestream, msg);
	memset(msg, '\0', BUFFER_SIZE);
	receive_msg(uart0_filestream, msg);
    if(VERBOSE) logit(LOG_INFO, "Got \"%s\" back, in response to earlier command", msg);
	strip_space(msg);

	if(!strncmp(msg, "STOPPED", 7)){
		free(msg);
		logit(LOG_FATAL, "Previous request has been interrupted, \"STOPPED\" is returned");
	}
}


int transmit (int uart0_filestream, unsigned char *trasmit_cmd){
	//----- TX BYTES -----
	if(trasmit_cmd[0] == '\0'){
		if(DEBUG) logit(LOG_ERROR, "Cannot write NULL to UART!");
		return 0;
	}
	if(DEBUG) logit(LOG_INFO, "Writing \"%s\" to UART...", trasmit_cmd);
	int cmd_len = strlen(trasmit_cmd);
	
	if (uart0_filestream != -1)
	{
		int count = write(uart0_filestream, trasmit_cmd, cmd_len);		//Filestream, bytes to write, number of bytes to write
		if (count < 0)
		{
			logit(LOG_FATAL, "UART TX error");
		}
	}
	return 1;
}    

int receive(int uart0_filestream, char *recv_msg){
	//----- CHECK FOR ANY RX BYTES -----
	if (uart0_filestream != -1)
	{
		// Read up to 255 characters from the port if they are there
		unsigned char rx_buffer[256];
		int rx_length = read(uart0_filestream, (void*)rx_buffer, 255);		//Filestream, buffer to store in, number of bytes to read (max)
		if (rx_length < 0)
		{
			logit(LOG_ERROR, "Rx error, no bytes when O_NONBLOCK is cleared");
			//An error occured (will occur if there are no bytes)
			return 1;
		}
		else if (rx_length == 0)
		{
			logit(LOG_ERROR, "Rx error, no data is waiting");
			//No data waiting
			return 1;
		}
		else
		{
			//Bytes received
			rx_buffer[rx_length] = '\0';
			if(DEBUG) logit(LOG_INFO, "%i bytes read : %s", rx_length, rx_buffer);
			// Copy str to recv_msg
			strcpy(recv_msg, &rx_buffer[0]);
			if(rx_buffer[rx_length - 1] == '>') return 0;
			return 1;
		}
	}
}

void receive_msg(int uart0_filestream, char *recv_msg){
	char *buffer = malloc(sizeof(char) * BUFFER_SIZE);
	if(!buffer) {
		free(recv_msg);
		logit(LOG_FATAL,"Undable to allocate mem");
	}
	memset(buffer, '\0', BUFFER_SIZE);
	memset(recv_msg, '\0', BUFFER_SIZE);
	while(receive(uart0_filestream, buffer)){
		if(DEBUG) logit(LOG_INFO, "recv_msg is \"%s\", buffer is \"%s\"", recv_msg, buffer);
		strcat(recv_msg, buffer);
	}
	strcat(recv_msg, buffer);
    filter_ctrl_chars(recv_msg);
}



