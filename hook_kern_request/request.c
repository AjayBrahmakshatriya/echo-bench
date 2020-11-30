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
#include <linux/file.h>


struct socket *conn_socket = NULL;
#define PORT 50050

long long *time_log = NULL;
int total_requests = 10000;
int parallel = 100;
int *parallel_count;
atomic64_t *parallel_tracker;

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


__be16 req_proto = htons((unsigned)0x80ab);
__be16 resp_proto = htons((unsigned)0x80ac);

char packet[100];

//static int count = 0;
atomic_t count;


static int packet_recv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev) {
	skb = skb_copy(skb, GFP_KERNEL);
	int index;
	memcpy(&index, skb->data + sizeof(long long), sizeof(int));
	//printk(KERN_INFO "index = %d\n", index);
	long long * val = NULL;
	val = (long long*) skb->data;	
	// This is an old request that has come back, ignore this completely
	if (*val != atomic64_read(&parallel_tracker[index])) {
		printk(KERN_INFO "Val mismatch for %d, %lld vs %lld\n", index, *val, atomic64_read(&parallel_tracker[index]));
		return 0;
	}

	int local_count = atomic_inc_return(&count);
	time_log[local_count] = get_time_in_us() - (*val);
	parallel_count[index]++;
	if (parallel_count[index] < total_requests/parallel) {
		char add[6];
		skb->data -= 14;
		memcpy(add, skb->data, 6);
		memcpy(skb->data, skb->data + 6, 6);
		memcpy(skb->data + 6, add, 6);
		memcpy(skb->data + 12, &req_proto, 2);
		long long t = get_time_in_us();
		skb->protocol = req_proto;
		memcpy(skb->data + 14, &t, sizeof(t));
		skb_get(skb);
		//parallel_tracker[index] = t;
		atomic64_set(&parallel_tracker[index], t);
		dev_queue_xmit(skb);
	}

	return 0;	
}

struct packet_type proto;
int inserted = 0;
int init_module(void)
{



	struct sk_buff  *skb = NULL;	
	printk(KERN_INFO "Hello world 1.\n");


	time_log = kmalloc(sizeof(long long) * total_requests, GFP_KERNEL);
	if (time_log == NULL)
		goto err;
	
	parallel_count = kmalloc(sizeof(int) * parallel, GFP_KERNEL);
	if (parallel_count == NULL)
		goto err;
	parallel_tracker = kmalloc(sizeof(atomic64_t) * parallel, GFP_KERNEL);
	if (parallel_tracker == NULL)
		goto err;

	struct net *net = current->nsproxy->net_ns;	
	struct net_device *dev = dev_get_by_name_rcu(net, INTERFACE);
	if (dev == NULL)
		goto err;
			
	//__be16 proto = htons((unsigned)0x08ab);
	//__be16 proto = htons((unsigned)0xab08);

	
	int len = 0;
	memcpy(packet + len, dst_mac, sizeof(dst_mac));
	len += sizeof(dst_mac);
	
	memcpy(packet + len, src_mac, sizeof(src_mac));
	len += sizeof(src_mac);
	
	memcpy(packet + len, &req_proto, sizeof(req_proto));
	len += sizeof(req_proto);



	long long tstamp = get_time_in_us();
	len += sizeof(tstamp);
	
	len += sizeof(int);


	len += message_length - sizeof(tstamp) - sizeof(int);
		
	int err_val;	

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL)
		goto err;

	skb_put(skb, len);
	memcpy(skb->data, packet, len);
	skb->no_fcs = 1;
	skb->sk = NULL;


	skb->dev = dev;
	skb->protocol = req_proto;
	skb->priority = 1;
	skb->mark = 0;
	skb->network_header = 0;
	int i;
	printk(KERN_INFO "Num tx queues = %d\n", dev->num_tx_queues);

	//skb_get(skb);

	proto.func = packet_recv;
	proto.type = resp_proto;
	proto.dev = dev;
	dev_add_pack(&proto);

	inserted = 1;

	atomic_set(&count, 0);

		
	for (i = 0; i < parallel; i++) {
		struct sk_buff* to_send = skb_copy(skb, GFP_KERNEL);
		parallel_count[i] = 0;
		atomic_inc_return(&count);		
		tstamp = get_time_in_us();
		memcpy(to_send->data + 14, &tstamp, sizeof(tstamp));	
		memcpy(to_send->data + 14 + sizeof(tstamp), &i, sizeof(i));
		//parallel_tracker[i] = tstamp;
		atomic64_set(&parallel_tracker[i], tstamp);
		//printk(KERN_INFO "SEnding %d with tstamp %lld\n", i, atomic64_read(&parallel_tracker[i]));
		
		int ret = dev_queue_xmit(to_send);
	}
/*
	while(atomic_read(&count) < total_requests - 2) {
		long long time_now = get_time_in_us();
		for (i = 0; i < parallel; i++) {
			if (time_now - parallel_tracker[i] > 2500) {
				// Packet is most probably dropped, retransmit it
				memcpy(skb->data + 14, &time_now, sizeof(time_now));	
				memcpy(skb->data + 14 + sizeof(time_now), &i, sizeof(i));
				skb_get(skb);
				int ret = dev_queue_xmit(skb);
				parallel_tracker[i] = tstamp;	
			}
		} 
		usleep_range(10, 11);
	}

*/
	if (skb != NULL)
		kfree_skb(skb);

	if (conn_socket != NULL)
		sock_release(conn_socket);
	return 0;
		
err:
	printk(KERN_INFO "Error happened\n");
	if (skb != NULL)
		kfree_skb(skb);
	if (conn_socket != NULL)
		sock_release(conn_socket);
	if (time_log != NULL)
		kfree(time_log);
	if (parallel_count != NULL)
		kfree(parallel_count);
	if (parallel_tracker != NULL)
		kfree(parallel_tracker);
	return -1;
}

void cleanup_module(void)
{
	if (inserted)
		__dev_remove_pack(&proto);

	if (time_log == NULL)
		goto end;
	if (parallel_count == NULL)
		goto end;

	int i;
	for (i = 0; i < parallel; i++) {
		printk(KERN_INFO "p %d: %d\n", i, parallel_count[i]);
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
	kfree(time_log);
end:
	printk(KERN_INFO "Goodbye world 1.\n");
	if (parallel_tracker != NULL)
		kfree(parallel_tracker);
}

