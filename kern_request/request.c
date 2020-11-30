#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/net.h>
#include <net/sock.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <asm/uaccess.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>




struct socket *conn_socket = NULL;
#define PORT 50050

long long *time_log = NULL;
int total_requests = 10000;
int parallel = 1;
//u8 destip[5] = {128, 30, 64, 26, 0};
u8 destip[5] = {192, 168, 1, 2, 0};
int message_length = 16;

module_param(message_length, int, 0644);
module_param(parallel, int, 0644);

char *reserve = NULL;
char* buffer = NULL;
char* buffer_process = NULL;

u32 create_address(u8 *ip)
{
        u32 addr = 0;
        int i;

        for(i=0; i<4; i++)
        {
                addr += ip[i];
                if(i==3)
                        break;
                addr <<= 8;
        }
        return addr;
}
int tcp_send(struct socket *sock, const char *buf, const size_t length, unsigned long flags) {
	struct msghdr msg;
	struct kvec vec;
	
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = flags;

	mm_segment_t oldmm;
	oldmm = get_fs(); set_fs(KERNEL_DS);
	
	int left = length;
	int len;
	int written = 0;
	while (left) {
		vec.iov_len = left;
		vec.iov_base = (char*) buf + written;
		len = kernel_sendmsg(sock, &msg, &vec, left, left);
		if (len == -ERESTARTSYS)
			continue;
		if (!(flags & MSG_DONTWAIT) && len == -EAGAIN)
			continue;
		if (len <= 0)
			break;
		left -= len;	
		written += len;
	}	
	set_fs(oldmm);
	return written?written:len;
}

int tcp_recv(struct socket *sock, char * buf, size_t max, unsigned long flags) {
	struct msghdr msg;
	struct kvec vec;
	int len;
	int max_size = max;
	
	msg.msg_name = 0;
	msg.msg_namelen = 0;

	msg.msg_control = 0;
	msg.msg_controllen = 0;
	
	msg.msg_flags = flags;
	
	vec.iov_len = max_size;
	vec.iov_base = buf;
	
	while (1) {
		len = kernel_recvmsg(sock, &msg, &vec, max_size, max_size, flags);
		if (len == -EAGAIN || len == -ERESTARTSYS)
			continue;
		break;	
	}
	return len;
}
long long get_time_in_us(void) {
	struct timeval t;
	do_gettimeofday(&t);
	return t.tv_usec + 1000000 * t.tv_sec;
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
int init_module(void)
{
	printk(KERN_INFO "Hello world 1.\n");
	
	struct sockaddr_in saddr;
	int ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &conn_socket);
	if (ret < 0) 
		goto err;

	memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(PORT);
        saddr.sin_addr.s_addr = htonl(create_address(destip));

	ret = conn_socket->ops->connect(conn_socket, (struct sockaddr *)&saddr, sizeof(saddr), O_RDWR);
	if (ret < 0)
		goto err;


	time_log = kmalloc(sizeof(long long) * total_requests, GFP_KERNEL);
	if (time_log == NULL)
		goto err;

	int i;
	long long val;	
	reserve = kmalloc(2 * message_length, GFP_KERNEL);
	buffer = kmalloc(16 * message_length, GFP_KERNEL);
	buffer_process = kmalloc(18 * message_length, GFP_KERNEL);
	if (reserve == NULL || buffer == NULL || buffer_process == NULL)
		goto err;
	for (i = 0; i < parallel; i++) {
		val = get_time_in_us();	
		memcpy(buffer, &val, sizeof(long long));
		memset(buffer + sizeof(long long), 0, message_length - sizeof(long long));
		tcp_send(conn_socket, buffer, message_length, MSG_DONTWAIT);
	}
	
	//char reserve[16];
	int reserve_size = 0;
	//char buffer[128];
	//char buffer_process[128+16];
	int tot_sent = parallel;
	int tot_recv = 0;
	while(1) {
		if (tot_recv == total_requests)
			break;
		int len = tcp_recv(conn_socket, buffer, 15 * message_length, MSG_DONTWAIT);
		if (len == 0) {
			printk(KERN_INFO "Zero read, Quitting\n");
			break;
		}
		memcpy(buffer_process, reserve, reserve_size);
		memcpy(buffer_process+reserve_size, buffer, len);
		len += reserve_size;
		reserve_size = 0;
		int processed = 0;
		while (processed < len) {
			int remain = len - processed;
			if (remain >= message_length) {		
				memcpy(&val, buffer_process + processed, sizeof(long long));	
				long long diff = get_time_in_us() - val;
				time_log[tot_recv] = diff;
				processed += message_length;
				tot_recv++;
				if (tot_sent < total_requests) {
					val = get_time_in_us();	
					memcpy(buffer, &val, sizeof(long long));
					memset(buffer + sizeof(long long), 0, message_length - sizeof(long long));
					tcp_send(conn_socket, buffer, message_length, MSG_DONTWAIT);
					tot_sent++;
				}

			} else {
				memcpy(reserve, buffer_process + processed, remain);
				reserve_size = remain;
				processed += remain;
			}
		}
	}
	sort_time_array();
	printk(KERN_INFO "%lld\n", time_log[total_requests * 10/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 20/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 30/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 40/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 50/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 60/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 70/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 80/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 90/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 95/100]);
	printk(KERN_INFO "%lld\n", time_log[total_requests * 99/100]);

	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (time_log != NULL)
		kfree(time_log);
	if (reserve != NULL)
		kfree(reserve);
	if (buffer != NULL)
		kfree(buffer);
	if (buffer_process != NULL)
		kfree(buffer_process);
	return 0;
		
err:
	printk(KERN_INFO "Error happened\n");
	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (time_log != NULL)
		kfree(time_log);
	if (reserve != NULL)
		kfree(reserve);
	if (buffer != NULL)
		kfree(buffer);
	if (buffer_process != NULL)
		kfree(buffer_process);
	return -1;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Goodbye world 1.\n");
}

