#include <linux/cdev.h>
#include <linux/cred.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <trace/events/kmem.h> 


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
#include <linux/mmu_context.h>


#include "src/memcached.h"
#define FIRST_MINOR 0
#define MINOR_CNT 1

static dev_t dev;
static struct cdev c_dev;
static struct class *cl;

#define CMD_START_MEMCACHED (0)
#define CMD_STOP_MEMCACHED (1)

#define TCP_PROTOCOL_NUMBER (0x6)
#define UDP_PROTOCOL_NUMBER (0x11)






static int clean_up_session(struct file * f) {

	if (f->private_data == NULL)
		return 0;
	// Clean up individual states
	struct memcached_state* state = (struct memcached_state*) f->private_data;

	// Now kill the thread that we had spawned
	// Wait for it to be killed
	smp_mb();
	atomic_set(&(state->thread_to_kill), 1);
	smp_mb();
	while(!atomic_read(&(state->thread_killed))) {
		msleep(100);
		smp_mb();
	}
	
	// This HAS to happen AFTER the thread is killed
	// Otherwise dev_remove_pack gets stuck, because the thread blocks for ever
	// Not a perfect solution either, since other kernel processes might call synchronize_rcu
	dev_remove_pack(state->proto);
	kfree(state->proto);
	state->proto = NULL;
	

        kfree(state->params);
        state->params = NULL;
	// Clean up the state structure;
	kfree(f->private_data);
	f->private_data = NULL;		
	return 0;
}

static int mem_open(struct inode *i, struct file *f) {
	f->private_data = NULL;

	return 0;
}
static int mem_close(struct inode *i, struct file *f) {
	return clean_up_session(f);
}
//#define DEBUG_PACKET
static int memcached_process(struct sk_buff *skb, struct net_device *dev, struct packet_type * pt, struct net_device *orig_dev) {
	// We are not going to process the skb here, we will just add it to the queue
	struct memcached_state *state = (struct memcached_state *) pt->af_packet_priv;

	// Do bump the reference counter before we actually add the skb to the queue
	// This will be decremented when we dequeue it
        skb_get(skb);
	skb_add_to_queue(state, skb);
	return 0;
}

static int process_dequed_skb(struct sk_buff * skb, struct memcached_state *state) {
	// Quickly weed out packets that are not TCP/IP
#ifdef DEBUG_PACKET
        if (skb->len > 40) {
		printk(KERN_INFO "New request of size = %d\n", skb->len);
		// Let us print the first 40 bytes to see what's what
		int i, j;
		char buffer[40 * 3 + 5] = {0};
                for (j = 0; j < skb->len/4; j++) {
			for (i = 0; i < 4; i++) {
				sprintf(buffer + (i) * 3, "%02x ", (int) skb->data[i + j * 4]);	
			}
			printk(KERN_INFO "Packet is %s\n", buffer);
		}
	}
#endif
	// We are going to assume for now that the IP packet doesn't have Options
	// TODO: Add an handler to calculate the size of the options to skip over
	if (skb->len < 20)
		return 0;
        
	if (skb->data[9] != TCP_PROTOCOL_NUMBER && skb->data[9] != UDP_PROTOCOL_NUMBER) {
#ifdef DEBUG_PACKET
		printk("IP protocol mismatch, actually is %d\n", (int) skb->data[9]);
#endif
		return 0;
	}

	unsigned short required_port = state->required_port;

	if (skb->data[9] == TCP_PROTOCOL_NUMBER) {
		char * tcp_packet_start = skb->data + 20;
		int tcp_packet_len = skb->len - 20;
		
		// Quickly weed out packets that are not for the specified port
		if (tcp_packet_len < 4)
			return 0;
		
		if (ntohs(((unsigned short*)tcp_packet_start)[1]) != required_port) {
#ifdef DEBUG_PACKEET
			printk("TCP port mismatch, actually is %d\n", (int) ntohs(((unsigned short*)tcp_packet_start)[1]));
#endif
			return 0;
		}
		// Now to calculate the TCP header size
		unsigned char size_byte = ((unsigned char*)tcp_packet_start)[12] >> 4;
		unsigned char * data = tcp_packet_start + size_byte * 4;
		unsigned int data_len = tcp_packet_len - size_byte * 4;
		if (data_len <= 0)
			return 0;
			
		handle_memcached_request(data, data_len, state, skb);
	} else if (skb->data[9] == UDP_PROTOCOL_NUMBER) {
		char * udp_start = skb->data + 20;
		int udp_len = skb->len - 20;
		if (udp_len < 4)
			return 0;
		if (ntohs(((unsigned short*)udp_start)[1]) != required_port) {
#ifdef DEBUG_PACKET
			printk("UDP port mismatch, actually is %d\n", (int) ntohs(((unsigned short*)udp_start)[1]));
#endif
			return 0;
		}
		unsigned char * data = udp_start + 8;
		unsigned int data_len = udp_len - 8;
		if (data_len <= 0)
			return 0;

		// UDP protocol for memcached also has 8 byte header
		// We will ignore that for now
		data += 8;
		data_len -= 8;	
		handle_memcached_request(data, data_len, state, skb);		
			
	}
}

int dedicated_thread_handler(void* state_ptr) {
	printk(KERN_INFO "New memcached thread created\n");
	struct memcached_state *state = (struct memcached_state *) state_ptr;
	// Before we do anything, let us start using the process's mm
	use_mm(state->process_mm);
	// thread main loop
	struct sk_buff * skb;
	long long counter = 0;
	while (1) {
                smp_mb();
		if (atomic_read(&(state->thread_to_kill)))
			break;
		skb = skb_take_from_queue(state);
		if (skb != NULL) {
			counter = 0;
#ifdef DEBUG_PACKET
			printk(KERN_INFO "Dequed skb from queue = %p\n", skb);
#endif
			// Release this skb
			process_dequed_skb(skb, state);
			kfree_skb(skb);
		}
		// This is required for now, otherwise the CPU gets stuck
		// Figure out how we can remove this later
		// Otherwise the latency is going to tank
		// Maybe we can signal this thread from the interrupt handler?
		// msleep(10);	
		counter ++;
		// This is so that the thread sleep once in a while
		// There was to be a better workaround
		if (counter % 100000000 == 0) {
			msleep(1);
		}
	}
	// Release the process_mm
	unuse_mm(state->process_mm);
	smp_mb();
	atomic_set(&(state->thread_killed), 1);	
	smp_mb();
	printk(KERN_INFO "New memcached thread stopped\n");
	do_exit(0);
}



static bool read_bool_from_user(bool * addr) {
    bool res = false;
    copy_from_user(&res, addr, sizeof(bool));
    return res;
}



static long mem_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
	//printk(KERN_INFO "IOCTL received: %d with arg %lu\n", cmd, arg);	

	if (cmd != CMD_START_MEMCACHED && cmd != CMD_STOP_MEMCACHED)
		return -EINVAL;
	
	if (cmd == CMD_STOP_MEMCACHED) 
		return clean_up_session(f);

	if (!access_ok(VERIFY_READ, (void*) arg, sizeof(struct memcached_params))) {
		return -EINVAL;
	}	
	
	struct memcached_params *params = kmalloc(sizeof(struct memcached_params), GFP_KERNEL);
        if (params == NULL)
		goto err;	
	copy_from_user(params, (char*) arg, sizeof(struct memcached_params));

        printk(KERN_INFO "Expanding parameter address = %p, value = %d\n", params->expanding_ptr, (int)read_bool_from_user(params->expanding_ptr));
        
	
	// If a state is already allocated for this file handle, free it first
	clean_up_session(f);
		
	// This is a start request. We always install a new handler per process
	// TODO: Figure out if we can manage all this in a single handler and multiple hooks into the handler

	// We start by allocating a state object for this f			
	struct memcached_state * state = kmalloc(sizeof(struct memcached_state), GFP_KERNEL);
		
	if (state == NULL)
		goto err;

	init_memcached_state(state);

        state->params = params;

	f->private_data = state;
	
	// Find the interface relevant to this request
	struct net * net = current->nsproxy->net_ns;
	struct net_device * dev = dev_get_by_name_rcu(net, params->interface_name);
	if (dev == NULL)
		goto err;

	__be16 proto_ip = htons(0x0800);
	
	struct packet_type * proto = kmalloc(sizeof(struct packet_type), GFP_KERNEL);
	if (proto == NULL)
		goto err;

	state->proto = proto;
	proto->func = memcached_process;
	proto->type = proto_ip;
	proto->dev = dev;
	proto->af_packet_priv = f->private_data;

	state->required_port = 11211;
	
	printk(KERN_INFO "Starting a hook on %s\n", params->interface_name);
	dev_add_pack(proto);
	
	// We will also save this process's mm in the state for the thread to use
	state->process_mm = current->mm;	

	// Everything is good, start a kernel thread
	state->thread_handle = kthread_run(dedicated_thread_handler, (void*)state, "Memcached Handler thread");
	
	if (state->thread_handle == NULL)
		goto err;
	 			
	state->ready = 1;	
	return 0;


err:
	if (params != NULL)
		kfree(params);
        if (state != NULL)
		kfree(state);
        if (proto != NULL)
		kfree(proto);
	return -EINVAL;
}


static struct file_operations query_fops = {
	.owner = THIS_MODULE,
	.open = mem_open,
	.release = mem_close,
	.unlocked_ioctl = mem_ioctl
};


static int __init install_driver(void) {
	int ret;
	struct device * dev_ret;

	if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "memcached_ctrl_region")) < 0) {
		return ret;
	}

	cdev_init(&c_dev, &query_fops);

	if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
		return ret;
	}
	
	if (IS_ERR(cl = class_create(THIS_MODULE, "char"))) {
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(cl);
	}

	if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, "memcached_ctrl"))) {
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(dev_ret);
	}
	
	return 0;
}


static void __exit uninstall_driver(void) {
	device_destroy(cl, dev);
	class_destroy(cl);
	cdev_del(&c_dev);
	unregister_chrdev_region(dev, MINOR_CNT);	
}


module_init(install_driver);
module_exit(uninstall_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ajay Brahmakshatriya <ajaybr@mit.edu>");
MODULE_DESCRIPTION("Memcached in kernel space driver");
