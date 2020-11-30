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

char header[] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef};
char rheader[] = {0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
//#define IFNAME ("ib0")
#define IFNAME ("enp101s0f1")
unsigned char src_addr[6];
#define BIND (1)

#define MESSAGE_SIZE (16)


#define ETH_SIZE (14)

int main(int argc, char* argv[]) {
	int sockfd;
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
	
	struct sockaddr_ll addr;
	addr.sll_family = AF_PACKET;
	unsigned short pt = 0x08ab;
	addr.sll_protocol = htons(pt);
	addr.sll_ifindex = index;
#if BIND
	if (bind(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
		printf("error in bind\n");
		return -1;
	}	
#endif
	char *message = malloc (2048);
	char *send_message = malloc (2048);
	while (1) {
		socklen_t addr_size = sizeof(addr);
		int retval = recvfrom(sockfd, message, 2048, 0, (struct sockaddr*) &addr, &addr_size);
		if (retval > (sizeof(header) + ETH_SIZE) && memcmp(message + ETH_SIZE, header, sizeof(header)) == 0) {
			long long val;
			memcpy(&val, message + ETH_SIZE + sizeof(header), sizeof(long long));
			//printf("Val = %lld on interface = %d\n", val, addr.sll_ifindex);
			memcpy(send_message, message + 6, 6);
			memcpy(send_message + 6, message, 6);
			send_message[12] = 0x08;
			send_message[13] = 0xab;
			memcpy(send_message + ETH_SIZE, rheader, sizeof(rheader));
			memcpy(send_message + ETH_SIZE + sizeof(rheader), &val, sizeof(val));
			memcpy(send_message + ETH_SIZE + sizeof(rheader) + sizeof(val), message + ETH_SIZE + sizeof(header) + sizeof(val), MESSAGE_SIZE-sizeof(header)-sizeof(val));
			memset(send_message + ETH_SIZE + MESSAGE_SIZE, 0, 4);
			retval = sendto(sockfd, send_message, ETH_SIZE + MESSAGE_SIZE + 4, 0, (struct sockaddr*)&addr, sizeof(addr));	
			if (retval < 0) {
				printf("Write failure\n");
				exit(-1);
			}
		}
		#if BIND
		else
			printf("Other");
		#endif
	}
	return 0;
}
