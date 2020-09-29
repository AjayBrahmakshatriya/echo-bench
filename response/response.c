#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
int main(int argc, char* argv[]) {
	if (argc < 3) 
		exit(-1);

	int fd = atoi(argv[1]);
	int packet_size = atoi(argv[2]);

	if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
                fprintf(stderr, "Bad file descriptor passed\n");
		while(1);
                return -1;
        }
	
	printf("Starting receive\n");

	char *reserve = malloc(packet_size * 2);
	char *buffer = malloc(packet_size * 16);
	char *buffer_process = malloc(packet_size * 18);
	int reserve_size = 0;

	while(1) {
		fd_set rdfs;
		FD_ZERO(&rdfs);
		FD_SET(fd, &rdfs);
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;	
		int retval = select(fd + 1, &rdfs, NULL, NULL, &tv);
		if (retval == -1) {
			printf("Error in select\n");
			return -1;
		} else if (retval) {
			if (FD_ISSET(fd, &rdfs)) {
				int len = read(fd, buffer, 15 * packet_size);
				
				if (!len) {
					printf("Zero length read, quitting\n");
					exit(0);
				}
				memcpy(buffer_process, reserve, reserve_size);
				memcpy(buffer_process + reserve_size, buffer, len);
				len = len + reserve_size;
				reserve_size = 0;
				int processed = 0;
				while (processed < len) {
					int remain = len - processed;
					if (remain >= packet_size) {
						retval = write(fd, buffer_process + processed, packet_size);
						processed += packet_size;
						if (retval != packet_size) {
							printf("Write failure\n");
							exit(-1);
						}
					} else {
						memcpy(reserve, buffer_process + processed, remain);
						reserve_size = remain;
						processed += remain;
					}
				}
			}
		}	
		
	}

	
	return 0;
}
