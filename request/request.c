#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>


int total_requests;
long long *time_log;
long long get_time_in_us(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;	
}

void sort_time_array(void) {
	int i, j;
	if (time_log == NULL)
		return;
	for (i = 0; i < total_requests; i++) {
		for (j = i+1; j < total_requests; j++) {
			if (time_log[i] > time_log[j]) {
				long long temp = time_log[i];
				time_log[i] = time_log[j];
				time_log[j] = temp;
			}
		}
	}
}
int main(int argc, char* argv[]) {
	if (argc < 5)
		exit(-1);
	
	int fd = atoi(argv[1]);
	int parallel = atoi(argv[2]);	
	total_requests = atoi(argv[3]);
	int packet_size = atoi(argv[4]);


	char *reserve = malloc(packet_size * 2);
	int reserve_size = 0;
	char * buffer = malloc(packet_size * 16);
	char * buffer_process = malloc(packet_size * 18);
	for (int i = 0; i < parallel; i++) {	
		long long val = get_time_in_us();
		memcpy(buffer, &val, sizeof(long long));
		memset(buffer + sizeof(long long), 0, packet_size - sizeof(long long));
		if (write(fd, buffer, packet_size) != packet_size) {
			printf("Write error\n");
			exit(-1);
		}
	}
	int tot_sent = parallel;
	int tot_recv = 0;	
	time_log = malloc(total_requests * sizeof(*time_log));
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
				int len = read(fd, buffer, packet_size * 15);
				if (!len) {
					printf("Zero length read, quitting\n");
					exit(0);
				}
				memcpy(buffer_process, reserve, reserve_size);
				memcpy(buffer_process + reserve_size, buffer, len);
				len += reserve_size;
				reserve_size = 0;
				int processed = 0;
				while (processed < len) {
					int remain = len - processed;
					if (remain >= packet_size) {
						long long val = 0;
						memcpy(&val, buffer_process + processed, sizeof(long long));
						long long diff = get_time_in_us() - val;
						time_log[tot_recv] = diff;
						processed += packet_size;
						tot_recv++;
						if (tot_sent < total_requests) {
							val = get_time_in_us();
							memcpy(buffer, &val, sizeof(long long));
							memset(buffer + sizeof(long long), 0, packet_size - sizeof(long long));
							retval = write(fd, buffer, packet_size);
							if (retval != packet_size) {
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
	sort_time_array();
	printf("%lld\n", time_log[total_requests * 10/100]);
	printf("%lld\n", time_log[total_requests * 20/100]);
	printf("%lld\n", time_log[total_requests * 30/100]);
	printf("%lld\n", time_log[total_requests * 40/100]);
	printf("%lld\n", time_log[total_requests * 50/100]);
	printf("%lld\n", time_log[total_requests * 60/100]);
	printf("%lld\n", time_log[total_requests * 70/100]);
	printf("%lld\n", time_log[total_requests * 80/100]);
	printf("%lld\n", time_log[total_requests * 90/100]);
	printf("%lld\n", time_log[total_requests * 95/100]);
	printf("%lld\n", time_log[total_requests * 99/100]);
	return 0;
}
