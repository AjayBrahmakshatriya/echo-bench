// Client side implementation of UDP client-server model 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <sys/time.h> 

#define PORT     11211
#define MAXLINE 1024 
#define TOTAL_REQUESTS (10000)

long long time_log[TOTAL_REQUESTS];

long long get_time_in_us(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * 1000000 + tv.tv_usec;
}

void sort_time_array(void) {
	int i, j;
	if (time_log == NULL)
		return;
	for (i = 0; i < TOTAL_REQUESTS; i++) {
		for (j = i+1; j < TOTAL_REQUESTS; j++) {
			if (time_log[i] > time_log[j]) {
				long long temp = time_log[i];
				time_log[i] = time_log[j];
				time_log[j] = temp;
			}
		}
	}
}

int main(int argc, char* argv[]) {
	int sockfd;
	char buffer[MAXLINE];
	char request[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x67, 0x65, 0x74, 0x20, 0x6d, 0x79, 0x6b, 0x65, 0x79, 0x0d, 0x0a};
	struct sockaddr_in     servaddr; 

	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
       		perror("socket creation failed"); 
        	exit(EXIT_FAILURE); 
    	} 
	memset(&servaddr, 0, sizeof(servaddr)); 
      
	// Filling server information 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_port = htons(PORT); 
	servaddr.sin_addr.s_addr = inet_addr("10.0.0.1");

	int n, len; 
    
	for (int i = 0; i < TOTAL_REQUESTS; i++) { 
		long long start = get_time_in_us(); 
		sendto(sockfd, (const char *)request, sizeof(request), 
				MSG_CONFIRM, (const struct sockaddr *) &servaddr,  
				sizeof(servaddr)); 

		n = recvfrom(sockfd, (char *)buffer, MAXLINE,  
			MSG_WAITALL, (struct sockaddr *) &servaddr, 
			&len);
		long long end = get_time_in_us();
		time_log[i] = end-start;
	}
	
	//printf("Received = %d\n", n);
	sort_time_array();
	printf("%lld\n", time_log[TOTAL_REQUESTS * 10/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 20/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 30/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 40/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 50/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 60/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 70/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 80/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 90/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 95/100]);
	printf("%lld\n", time_log[TOTAL_REQUESTS * 99/100]);
	close(sockfd);
	return 0;
}
