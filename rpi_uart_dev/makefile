CC=gcc
CFLAGS=-Wall

all: rpi_uart

out: rpi_uart

rpi_uart: rpi_uart.o rpi_logger.o
	$(CC) $(CFLAGS) rpi_uart.o rpi_logger.o -o rpi_uart

rpi_uart.o: rpi_uart.c
	$(CC) -c rpi_uart.c -o rpi_uart.o

rpi_logger.o: rpi_logger.c
	$(CC) -c rpi_logger.c -o rpi_logger.o

clean:
	rm -rf *o rpi_uart
