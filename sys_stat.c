#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include "include/sys_stat.h"

double get_sys_tmp(){
	FILE *fd = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
	if(!fd){
		fprintf(stderr, "ERROR: Unable to open file for read!\n");
		return -1.0;
	}
	int raw_tmp = -1;
	if(fscanf(fd, "%d", &raw_tmp) != 1 || raw_tmp == -1){
		fprintf(stderr, "ERROR: Unable to read file /sys/class/thermal/thermal_zone0/temp \n");
	}
	fclose(fd);
	return (double) raw_tmp / 1000.00;
	
}

double get_perc_used(char *path){
	struct statvfs vfs;
	statvfs(path, &vfs);
	double prc  = 100.0 * (double) (vfs.f_blocks - vfs.f_bfree) / (double) (vfs.f_blocks - vfs.f_bfree + vfs.f_bavail);
	return prc;
}

double get_perc_free(char *path){
	return 100 - get_perc_used(path);
}

int get_perc_free_int(char *path){
	int perc = get_perc_free(path);
	return perc;
}

/*
int main(){
	double tmp = get_sys_tmp();
	int freespc = get_perc_free_int(".");
	printf("tmp = %f 'C\nfreespc = %d\%\n", tmp, freespc);
	return 0;
}
*/
