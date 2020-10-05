#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <unistd.h>
#include <sys/time.h>

// mac address 54:80:28:4b:3f:e6
struct sockaddr_ll addr;
unsigned char src_addr[6];
unsigned char dst_addr[] = {0x54, 0x80, 0x28, 0x4b, 0x3f, 0xe6};
//unsigned char dst_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
char buffer[2048];
int buf_len = 0;
int payloadoffset = -1;
#define MESSAGE_SIZE (1024)
int sockfd;
//#define IFNAME ("ib0")
#define IFNAME ("eno2")
#define TOTAL_REQUESTS (10000)
#define PARALLEL (5)
#define ETH_SIZE (14)

long long time_log[TOTAL_REQUESTS];


char header[] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef};
char rheader[] = {0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};

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
void send_packet(void) {
	memcpy(buffer+payloadoffset, header, sizeof(header));	
	long long val = get_time_in_us();
	memcpy(buffer+payloadoffset + sizeof(header), &val, sizeof(val));
	int retval = sendto(sockfd, buffer, buf_len, 0, (struct sockaddr*) &addr, sizeof(addr));
	if (retval < 0) {
		printf("Error sending\n");
		exit(-1);
	}
	if (retval == 0) {
		printf("Zero write\n");
		exit(-1);
	}
	//printf("Write size = %d\n", retval);
}

int main(int argc, char* argv[]) {
	
	sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (sockfd < 0) {
		printf("Error creating socket\n");
		return -1;
	}


	struct ifreq ifr_index;
	memset(&ifr_index, 0, sizeof(struct ifreq));
	strncpy((char*)&ifr_index.ifr_name, IFNAME, IFNAMSIZ-1);
	if(ioctl(sockfd, SIOCGIFINDEX, &ifr_index) < 0) {
		printf("Error in IOCTL\n");
		return -1;
	}
	int index = ifr_index.ifr_ifindex;

	printf("Index obtained as %d\n", index);
	memset(&ifr_index, 0, sizeof(struct ifreq));
	strncpy((char*)&ifr_index.ifr_name, IFNAME, IFNAMSIZ-1);

	if(ioctl(sockfd, SIOCGIFHWADDR, &ifr_index) < 0) {
		printf("Error in IOCTL\n");
		return -1;
	}
	memcpy(src_addr, &ifr_index.ifr_hwaddr.sa_data, ETH_ALEN);
	printf("Mac address read as %02x:%02x:%02x:%02x:%02x:%02x\n", src_addr[0], src_addr[1], src_addr[2], src_addr[3], src_addr[4], src_addr[5]);
	

	unsigned pt = 0x08ab;
	addr.sll_family = AF_PACKET;
	addr.sll_protocol = htons(pt);
	addr.sll_ifindex = index;
	addr.sll_hatype = 0;
	addr.sll_pkttype = 0;
	addr.sll_halen = 6;
	
	if (bind(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		printf("error in bind\n");
		return -1;
	}

	
	//unsigned char dst_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	memcpy(&addr.sll_addr[0], dst_addr, sizeof(dst_addr));

	//unsigned char src_addr[] = {0xac, 0x1f, 0x6b, 0x87, 0x00, 0x99};
	

	memcpy(buffer+buf_len, dst_addr, 6);
	buf_len += 6;

	
	memcpy(buffer+buf_len, src_addr, 6);
	buf_len += 6;

	buffer[buf_len+0] = 0x08;
	buffer[buf_len+1] = 0xab;
	buf_len+=2;

	payloadoffset = buf_len;
	buf_len+= MESSAGE_SIZE;	
	
	buf_len += 4;
	for (int i = 0; i < PARALLEL; i++)
		send_packet();
	
	int tot_sent = PARALLEL;
	int tot_recv = 0;	

	struct sockaddr_ll r_addr;
	char *message = malloc(2048);
	while(1) {
		if (tot_recv == TOTAL_REQUESTS) {
			break;
		}		
		socklen_t addr_size = sizeof(r_addr);		
		int retval = recvfrom(sockfd, message, 2048, 0, (struct sockaddr*) &addr, &addr_size);
		if (retval > (sizeof(rheader) + ETH_SIZE) && memcmp(message + ETH_SIZE, rheader, sizeof(rheader)) == 0) {
			tot_recv++;
			long long val;
			memcpy(&val, message + ETH_SIZE + sizeof(rheader), sizeof(long long));
			//printf("%lld\n", get_time_in_us() - val);
			time_log[tot_recv-1] = get_time_in_us() - val;
			if (tot_sent < TOTAL_REQUESTS) {
				send_packet();
				tot_sent++;
			}
		} else 
			printf("Other\n");
	}

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
