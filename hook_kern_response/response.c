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



static struct net_device *dev;
__be16 req_proto = htons((unsigned)0x80ab);
__be16 resp_proto = htons((unsigned)0x80ac);

static int packet_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev) {
	//printk("Received packet of size = %d\n", skb->len);
	skb = skb_copy(skb, GFP_KERNEL);
	long long * val;
	//val = (long long*)((char*)(skb->data));
	//packetprintk("The time stamp was %lld\n", *val);

	// Mangle the packet, it is okay	
	// Reverse the packet address
	char add[6];
	
	skb->data -= 14;
	
	memcpy(add, skb->data, 6);
	memcpy(skb->data, skb->data + 6, 6);
	memcpy(skb->data + 6, add, 6);

	memcpy(skb->data + 12, &resp_proto, 2);

	//skb->data += 14;	

	skb->protocol = resp_proto;		
	skb_get(skb);
	dev_queue_xmit(skb);
		
	
	return 0;	
}

struct packet_type proto;
int init_module(void)
{
	struct sk_buff  *skb = NULL;	
	printk(KERN_INFO "Hello world 1.\n");


	struct net *net = current->nsproxy->net_ns;	
	dev = dev_get_by_name_rcu(net, INTERFACE);
	if (dev == NULL)
		goto err;
			



	proto.func = packet_recv;
	proto.type = req_proto;
	
	proto.dev = dev;
	
	dev_add_pack(&proto);
	
	

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
	__dev_remove_pack(&proto);
	printk(KERN_INFO "Goodbye world 1.\n");
}

