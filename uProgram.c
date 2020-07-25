#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>  


int main(int argc, char* argv[]) {
	int i;
	system("sudo rm /dev/OS_phase1_driver");
	system("sudo mknod -m 666 /dev/OS_phase1_driver c 241 0");
	char users[1000][32];
	char files[1000][256];
	int userCnt = 0, fileCnt = 0;

	printf("Welcome!\nCommand schema: [cmd] ...\n\nCommands:\n  0- add new user [sl] [username]\n  1- add new file [sl] [file path]\n  2- user [x] [read/write/readwrite] file [y]\n");

	while(1) {
		int cmd, sl;
		char detail[256];
		int size = 35;
		char buf[259];
		scanf("%d %d %s", &cmd, &sl, detail);
		if (cmd == 2) {
			int fid = atoi(detail);
			int fd;
			fd = open(files[fid], O_RDWR|O_APPEND|O_CREAT);
			if (sl == 0) {
				read(fd, buf, 256);
				printf("%s", buf);
			}
			else if (sl == 1) {
				write(fd, files[fid], strlen(files[fid]));
			}
			else {
				write(fd, files[fid], strlen(files[fid]));
				read(fd, buf, 256);
				printf("%s", buf);
			}
			close(fd);
			continue;
		}

		buf[0] = '0';
		buf[1] = sl;
		if (cmd == 0) {
			printf("userId: %d", userCnt);
			int j;
			for (j = 0; j < 32; j++) {
				users[userCnt++][j] = detail[j];
				buf[j + 2] = detail[j];
			}
		} else if (cmd == 1) {
			printf("fileId: %d", fileCnt);
			int j;
			size = 259;
			for (j = 0; j < 256; j++) {
				files[fileCnt++][j] = detail[j];
				buf[j + 2] = detail[j];
			}
			// int fd;
			// fd = open(files[fileCnt - 1], O_WRONLY|O_APPEND|O_CREAT);
			// write(fd, files[fileCnt - 1], strlen(files[fileCnt - 1]));
			// close(fd);
		}
		buf[size - 1] = '\n';
		
		// write to driver file
		int fd;
		fd = open("/dev/OS_phase1_driver", O_RDWR);
		if (0 > write(fd, buf, size))
			printf("problem in write\n");			
		close(fd);
	}

	return 0;
}