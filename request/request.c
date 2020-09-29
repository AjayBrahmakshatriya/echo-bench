#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>



long long get_time_in_us(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;	
}
int main(int argc, char* argv[]) {
	if (argc < 4)
		exit(-1);
	
	int fd = atoi(argv[1]);
	int parallel = atoi(argv[2]);	
	int total_requests = atoi(argv[3]);


	char reserve[16];
	int reserve_size = 0;
	for (int i = 0; i < parallel; i++) {	
		long long val = get_time_in_us();
		if (write(fd, &val, sizeof(long long)) != sizeof(long long)) {
			printf("Write error\n");
			exit(-1);
		}
	}
	int tot_sent = parallel;
	int tot_recv = 0;	
	long long * time_log = malloc(total_requests * sizeof(*time_log));
	while (1) {
		if (tot_recv == total_requests)
			break;

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
				len += reserve_size;
				reserve_size = 0;
				int processed = 0;
				while (processed < len) {
					int remain = len - processed;
					if (remain >= sizeof(long long)) {
						long long val = 0;
						memcpy(&val, buffer_process + processed, sizeof(long long));
						long long diff = get_time_in_us() - val;
						time_log[tot_recv] = diff;
						processed += sizeof(long long);
						tot_recv++;
						if (tot_sent < total_requests) {
							val = get_time_in_us();
							retval = write(fd, &val, sizeof(long long));
							if (retval != sizeof (long long)) {
								printf("Write failure\n");
								exit(-1);	
							}
							tot_sent++;
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
	close(fd);
	for (int i = 0; i < total_requests; i++) {
		printf("%lld\n", time_log[i]);
	}
	return 0;
}
