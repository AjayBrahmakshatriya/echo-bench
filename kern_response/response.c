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
struct socket *listen_socket = NULL;

#define PORT 50050

int message_length = 16;
module_param(message_length, int, 0644);


char* reserve = NULL;
char* buffer = NULL;
char* buffer_process = NULL;
int reserve_size = 0;

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

int init_module(void)
{
	printk(KERN_INFO "Hello world 1.\n");
	
	struct sockaddr_in saddr;
	int ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &listen_socket);
	if (ret < 0) 
		goto err;

	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(PORT);
	
	ret = listen_socket->ops->bind(listen_socket, (struct sockaddr*)&saddr, sizeof(saddr));

	if (ret < 0) 
		goto err;
	
	ret = listen_socket->ops->listen(listen_socket, 16);

		
	//ret = sock_create(listen_socket->sk->sk_family, listen_socket->type, listen_socket->sk->sk_protocol, &conn_socket);
	
	conn_socket = sock_alloc();
	if (!conn_socket)
		goto err;

	conn_socket->type = listen_socket->type;
	conn_socket->ops = listen_socket->ops;

	struct inet_connection_sock *isock; 
	isock = inet_csk(listen_socket->sk);


	DECLARE_WAITQUEUE(accept_wait, current);
	add_wait_queue(&listen_socket->sk->sk_wq->wait, &accept_wait);
	while(reqsk_queue_empty(&isock->icsk_accept_queue)) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&listen_socket->sk->sk_wq->wait, &accept_wait);
	
	

	ret = listen_socket->ops->accept(listen_socket, conn_socket, O_NONBLOCK,  true);
	
	if (ret < 0)
		goto err;
	
	reserve_size = 0;
	reserve = kmalloc(2* message_length, GFP_KERNEL);
	buffer = kmalloc(16 * message_length, GFP_KERNEL);
	buffer_process = kmalloc(18 * message_length, GFP_KERNEL);

	if (reserve == NULL || buffer == NULL || buffer_process == NULL)
		goto err;
	

	while(1) {
		int len;
		len = tcp_recv(conn_socket, buffer, 15 * message_length, MSG_DONTWAIT);
		
		if (len <= 0) {
			break;
		}
		memcpy(buffer_process, reserve, reserve_size);
		memcpy(buffer_process + reserve_size, buffer, len);
	
		len = len + reserve_size;
		reserve_size = 0;
		int processed = 0;
		while (processed < len) {
			int remain = len - processed;
			if (remain >= message_length) {
				int retval = tcp_send(conn_socket, buffer_process + processed, message_length, MSG_DONTWAIT);
				processed += message_length;
				if (retval != message_length) {
					printk(KERN_INFO "Write failure\n");
					goto err;	
				}
			} else {
				memcpy(reserve, buffer_process + processed, remain);
				reserve_size = remain;
				processed += remain;
			}
		}
		
		//tcp_send(conn_socket, buffer, 8, MSG_DONTWAIT);
	}

	if (listen_socket != NULL)
		sock_release(listen_socket);
	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (reserve != NULL)
		kfree(reserve);
	if (buffer != NULL)
		kfree(buffer);
	if (buffer_process != NULL)
		kfree(buffer_process);
	return 0;

err:
	printk(KERN_INFO "Error occured\n");
	if (listen_socket != NULL)
		sock_release(listen_socket);
	if (conn_socket != NULL)
		sock_release(conn_socket);
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
