#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
int main(int argc, char* argv[]) {
	if (argc < 2) 
		exit(-1);

	int fd = atoi(argv[1]);

	if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
                fprintf(stderr, "Bad file descriptor passed\n");
		while(1);
                return -1;
        }
	
	printf("Starting receive\n");

	char reserve[16];
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
				char buffer[256];
				int len = read(fd, buffer, 80);
				
				if (!len) {
					printf("Zero length read, quitting\n");
					exit(0);
				}
				char buffer_process[256 + 16];
				memcpy(buffer_process, reserve, reserve_size);
				memcpy(buffer_process + reserve_size, buffer, len);
				len = len + reserve_size;
				reserve_size = 0;
				int processed = 0;
				while (processed < len) {
					int remain = len - processed;
					if (remain >= sizeof(long long)) {
						long long val = 0;
						memcpy(&val, buffer_process + processed, sizeof(long long));
						//printf("%lld\n", val);
						processed += sizeof(long long);
						retval = write(fd, &val, sizeof(long long));
						if (retval != sizeof (long long )) {
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
