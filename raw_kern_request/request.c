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

#include "internal.h"
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched/signal.h>
#include <linux/file.h>


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


char packet[100];
int init_module(void)
{



	unsigned long v = __fdget(1);
	struct fd f = (struct fd){(struct file*)(v & ~3), v & 3};
	if (f.file) {
		loff_t pos = f.file->f_pos;
		ssize_t ret = kernel_write(f.file, "Hello", sizeof("Hello"), &pos);	
		if (ret >= 0)
			f.file->f_pos = pos;
		//fdput_pos(f);
			
	}


	struct sk_buff  *skb = NULL;	
	printk(KERN_INFO "Hello world 1.\n");


	time_log = kmalloc(sizeof(long long) * total_requests, GFP_KERNEL);
	if (time_log == NULL)
		goto err;

	struct net *net = current->nsproxy->net_ns;	
	struct net_device *dev = dev_get_by_name_rcu(net, INTERFACE);
	if (dev == NULL)
		goto err;
			
	__be16 proto = htons((unsigned)0x80ab);
	__be16 recv_proto = htons((unsigned)0x80ac);
	//__be16 proto = htons((unsigned)0x08ab);
	//__be16 proto = htons((unsigned)0xab08);


	int err = sock_create(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL), &conn_socket);
	if (err < 0)
		goto err;
	err = 0;
	
	struct sockaddr_ll sll;
	sll.sll_ifindex = dev->ifindex;
	sll.sll_protocol = recv_proto;
	sll.sll_family = AF_PACKET;
	
	err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&sll, sizeof(sll));
	if (err < 0)
		goto err;
	

	int len = 0;
	memcpy(packet + len, dst_mac, sizeof(dst_mac));
	len += sizeof(dst_mac);
	
	memcpy(packet + len, src_mac, sizeof(src_mac));
	len += sizeof(src_mac);
	
	memcpy(packet + len, &proto, sizeof(proto));
	len += sizeof(proto);



	long long tstamp = get_time_in_us();
	//memcpy(packet + len, &tstamp, sizeof(tstamp));
	len += sizeof(tstamp);
		
	int err_val;	

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		goto err;

	skb_put(skb, len);
	memcpy(skb->data, packet, len);
	skb->no_fcs = 1;
	skb->sk = NULL;

	//printk(KERN_INFO "skb->data is %p\n", skb->data);

	skb->dev = dev;
	skb->protocol = proto;
	skb->priority = 1;
	skb->mark = 0;
	skb->network_header = 0;
	int i;
	printk(KERN_INFO "Num tx queues = %d\n", dev->num_tx_queues);


	int count = 0;
	for (count = 0; count < total_requests; count++) {
		tstamp = get_time_in_us();
		memcpy(skb->data + 14, &tstamp, sizeof(tstamp));	
		skb_get(skb);
		//printk("Sending time stamp = %lld\n", tstamp);
		int ret = dev_queue_xmit(skb);
		
		if (ret != 0)
			printk(KERN_INFO "dropped\n");


		struct sk_buff *recv_skb = NULL;
		int counter = 0;	
		while (recv_skb == NULL) {
			if (fatal_signal_pending(current)) 
				goto err;
			recv_skb = skb_recv_datagram(conn_socket->sk, 0, 0, &err);
			if (recv_skb == NULL) {
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
			if (recv_skb->protocol != recv_proto) {
				kfree_skb(recv_skb);	
				recv_skb = NULL;
			}	
		}


		//skb->data[skb->len + 2] = 0;
		//printk(KERN_INFO "data is %s\n", skb->data);
		long long new_tstamp = get_time_in_us();
		long long *old_tstamp = (long long*)(recv_skb->data+14);
		time_log[count] = new_tstamp - *old_tstamp;
		//printk(KERN_INFO "Difference = %lld\n", new_tstamp - *old_tstamp);
		
		
		if (recv_skb != NULL)
			kfree_skb(recv_skb);
		recv_skb = NULL;

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

