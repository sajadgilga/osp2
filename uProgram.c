#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>  


int main(int argc, char* argv[]) {
    	int i;
	long pid, period;
    	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--period")==0) {
			period = atoi(argv[i+1]);
			i++;
		}
		else if (strcmp(argv[i], "--pid") == 0) {
			pid = atoi(argv[i+1]);
			i++;
		}
	}
	
	int size = 16;
	char buf[size];
        snprintf(buf, size, "%li", pid);
    	system("sudo rm /dev/OS_phase1_driver");
    	system("sudo mknod -m 666 /dev/OS_phase1_driver c 241 0");
	
	int fd;
	fd = open("/dev/OS_phase1_driver", O_RDWR);
	if (0 > write(fd, buf, 8))
		printf("problem in write\n");
	close(fd);
	printf("Result is: \n");
	int time = 0;	
	while(1) {
		printf("time: %d\n", time);
    		system("sudo cat /dev/OS_phase1_driver");
		sleep(period);
		time+=period;
	}
	return 0;
}