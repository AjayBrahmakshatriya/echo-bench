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

#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>


struct socket *conn_socket = NULL;
#define PORT 50050

long long *time_log = NULL;
int total_requests = 10000;
int parallel = 1;
int message_length = 16;


char dst_mac[] = {0x98, 0x03, 0x9b, 0x9b, 0x36, 0x33};
char src_mac[] = {0x098, 0x03, 0x9b, 0x9b, 0x2e, 0xeb};

#define INTERFACE ("enp101s0f1")

module_param(message_length, int, 0644);
module_param(parallel, int, 0644);

long long get_time_in_us(void) {
	struct timeval t;
	do_gettimeofday(&t);
	return t.tv_usec + 1000000 * t.tv_sec;
}


char packet[100];
int init_module(void)
{
	struct sk_buff  *skb = NULL;	
	printk(KERN_INFO "Hello world 1.\n");


	struct net *net = current->nsproxy->net_ns;	
	struct net_device *dev = dev_get_by_name_rcu(net, INTERFACE);
	if (dev == NULL)
		goto err;
			
	__be16 proto = htons((unsigned)0x80ab);
	__be16 resp_proto = htons((unsigned)0x80ac);

	
	int err = sock_create(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL), &conn_socket);
	if (err < 0)
		goto err;
	err = 0;
	
	struct sockaddr_ll sll;
	sll.sll_ifindex = dev->ifindex;
	sll.sll_protocol = proto;
	sll.sll_family = AF_PACKET;
	
	err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&sll, sizeof(sll));
	if (err < 0)
		goto err;


	int count = 0;
	for (count = 0; count < total_requests; count++) {
		int counter = 0;
		while(skb == NULL) {
			if (fatal_signal_pending(current))
				goto err;
			skb = skb_recv_datagram(conn_socket->sk, 0, 0, &err);
			if (skb == NULL) {
				if (counter == 100) {
					msleep(1);
					counter = 0;
				} else {
					//mdelay(1);
					counter ++;
				}
				continue;
			}
			counter = 0;	
			if (skb->protocol != proto) {
				kfree_skb(skb);
				skb = NULL;
			}	
		}
		//printk(KERN_INFO "proto is %04x\n", skb->protocol);	
		//printk(KERN_INFO "message is %s\n", skb->data);	
		char packet[100];
		int len = skb->len;
		memcpy(packet, skb->data, skb->len);
		kfree_skb(skb);
		
		skb = alloc_skb(len, GFP_ATOMIC);
		if (skb == NULL)
			goto err;
		skb_put(skb, len);
		memcpy(skb->data, packet, len);

		char dst[16];
		memcpy(dst, skb->data, 6);
		memcpy(skb->data, skb->data + 6, 6);
		memcpy(skb->data + 6,  dst, 6);

	
		memcpy(skb->data + 12, &resp_proto, 2);

		skb->dev = dev;
		skb->no_fcs = 1;
		skb->sk = NULL;
		skb->protocol = resp_proto;
		skb->priority = 1;
		skb->mark = 0;

		int ret = dev_queue_xmit(skb);
		skb = NULL;
		
		if (ret != 0)
			printk("Send received %d\n", ret);		
		
	}	
	

	if (skb != NULL)
		kfree_skb(skb);
	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (time_log != NULL)
		kfree(time_log);
	return 0;
		
err:
	printk(KERN_INFO "Error happened\n");
	if (skb != NULL)
		kfree_skb(skb);
	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (time_log != NULL)
		kfree(time_log);
	return -1;
}

void cleanup_module(void)
{
	printk(KERN_INFO "Goodbye world 1.\n");
}

