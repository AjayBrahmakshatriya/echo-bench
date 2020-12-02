#include "memcached.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
//#include <asm/uaccess.h>
#include <linux/uaccess.h>



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

char process_buffer[MAX_BUFFER_SIZE];
char packet[MAX_BUFFER_SIZE];		

static size_t tokenize_command(char *command, token_t *tokens, const size_t max_tokens) {
    char *s, *e;
    size_t ntokens = 0;
    size_t len = strlen(command);
    unsigned int i = 0;

    //assert(command != NULL && tokens != NULL && max_tokens > 1);

    s = e = command;
    for (i = 0; i < len; i++) {
        if (*e == ' ') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
                *e = '\0';
                if (ntokens == max_tokens - 1) {
                    e++;
                    s = e; /* so we don't add an extra token */
                    break;
                }
            }
            s = e + 1;
        }
        e++;
    }

    if (s != e) {
        tokens[ntokens].value = s;
        tokens[ntokens].length = e - s;
        ntokens++;
    }

    /*
     * If we scanned the whole string, the terminal value pointer is null,
     * otherwise it is the first unprocessed character.
     */
    tokens[ntokens].value =  *e == '\0' ? NULL : e;
    tokens[ntokens].length = 0;
    ntokens++;

    return ntokens;
}
typedef uint32_t ub4;
#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n) - 1)

static bool read_bool_from_user(bool * addr) {
    bool res = false;
    copy_from_user(&res, addr, sizeof(bool));
    return res;
}
static unsigned int read_uint_from_user(unsigned int * addr) {
    unsigned int res;
    copy_from_user(&res, addr, sizeof(unsigned int));
    return res;
}

static item** read_item_pp_from_user(item*** addr) {
	item** res;
	copy_from_user(&res, addr, sizeof(res));
	return res;
}
static item* read_item_p_from_user(item** addr) {
	item* res;
	copy_from_user(&res, addr, sizeof(res));
	return res;
}
static int get_item_size(item* user) {
	item local;
	copy_from_user(&local, user, sizeof(local));
	return ITEM_ntotal(&local);
}
static void copy_item_from_user(item* dst, item* src) {
	copy_from_user(dst, src, sizeof(*dst));
	// Now also copy the data
	copy_from_user((char*)dst + sizeof(item), (char*)src + sizeof(item), ITEM_ntotal(dst) - sizeof(item));
}
static int memcmp_user(char* kern, char* user, int size) {
	// Can we get rid of this somehow?
	char local[size];
	copy_from_user(local, user, size);
	return memcmp(kern, local, size);
}

item *assoc_find(const char *key, const size_t nkey, const uint32_t hv, struct memcached_state* state) {
    bool expanding = read_bool_from_user(state->params->expanding_ptr);
    unsigned int hashpower = read_uint_from_user(state->params->hashpower_ptr);
    item** primary_hashtable = read_item_pp_from_user(state->params->primary_hashtable_ptr); 
    item *it;
    unsigned int oldbucket;

    if (expanding)
	return NULL;

    it = read_item_p_from_user(&primary_hashtable[hv & hashmask(hashpower)]);

    item *ret = NULL;
    int depth = 0;


    while (it) {
        char it_buff[get_item_size(it)];
        item *it_copy = (item*) it_buff;
	
	copy_item_from_user(it_copy, it);

        if ((nkey == it_copy->nkey) && (memcmp_user(key, ITEM_key(it_copy), nkey) == 0)) {
            ret = it;
            break;
        }
        it = it_copy->h_next;
        ++depth;
    }
    return ret;
}

void set_ip_checksum(char * ip_start) {
	unsigned int checksum = 0;
	unsigned short * packet = (unsigned short*) ip_start;
	int i;
	for (i = 0; i < 10; i++) {
#ifdef DEBUG_IP_CHECKSUM
		printk(KERN_INFO "Adding to checksum = %04x\n", (unsigned int) ntohs(packet[i]));
#endif
		checksum += ntohs(packet[i]);
	}
#ifdef DEBUG_IP_CHECKSUM
	printk(KERN_INFO "Check is %x\n", checksum);
#endif
	unsigned lower = checksum & 0xffffu;
	unsigned upper = (checksum >> 16) & 0xffffu;
	
	checksum = lower + upper;

	lower = checksum & 0xffffu;
	upper = (checksum >> 16) & 0xffffu;
	
	checksum = lower + upper;
	
	lower = checksum & 0xffffu;
	lower = lower ^ 0xffffu;
	lower = htons(lower);
	memcpy(ip_start + 10, &lower, 2);	
	
}

static void send_udp_response(char * input, char* key, int nkey, char* data, int data_len, struct sk_buff * skb, struct memcached_state* state) {
	int len = 0;
	char * skb_pre = skb->data - 14;
	char * skb_data = skb->data;

	// Create the ethernet frame (14 bytes)
	memcpy(packet + len, skb_pre + 6, 6);
	len += 6;
	memcpy(packet + len, skb_pre, 6);
	len += 6;
	memcpy(packet + len, skb_pre + 12, 2);
	len += 2;

	char base[] = {0x45, 0x00, 0x00, 0x00, 
		       0x4a, 0x72, 0x40, 0x00,
		       0x40, 0x11, 0x00, 0x00};

	char* ip_start = packet + len;		  
	// Create the IP packet (20 bytes)
	memcpy(packet + len, base, sizeof(base));
        len += sizeof(base);
			
	memcpy(packet + len, skb_data + 16, 4);
	len += 4;
	memcpy(packet + len, skb_data + 12, 4);
	len += 4;

	// UDP segment
	char * udp_start = packet + len;	
	memcpy(packet + len, skb_data + 20 + 2, 2);
        len += 2;
	memcpy(packet + len, skb_data + 20, 2);
	len += 2;
	
	// UDP Length to be filled here	
	len += 2;
	
	char checksum[] = {0x00, 0x00};
	// Ignore checksum for now
	memcpy(packet + len, checksum, 2);
	len += 2;
	
	// Memcached header, can copy the exact from the request
	memcpy(packet+len, input-8, 8);
	len += 8;
/*
	char template[] = "VALUE mykey 0 4\r\nDATA\r\nEND\r\n";
	memcpy(packet + len, template, sizeof(template)-1);
	len += sizeof(template) - 1;
*/
	memcpy(packet + len, "VALUE ", sizeof("VALUE ") -1);
	len += sizeof("VALUE ") -1;
	
	memcpy(packet + len, key, nkey);
	len += nkey;
	
	memcpy(packet + len, " 0 4\r\n", sizeof(" 0 4\r\n")-1);
	len += sizeof(" 0 4\r\n")-1;
	
	memcpy(packet + len, data, data_len);
	len += data_len;
	
	memcpy(packet + len, "END\r\n", sizeof("END\r\n")-1);
	len += sizeof("END\r\n")-1;
	

	
	// Patch in the ip length and udp length
	unsigned short ip_length = htons(packet + len - ip_start);
	unsigned short udp_length = htons(packet + len - udp_start);
	
	memcpy(ip_start + 2, &ip_length, sizeof(ip_length));
	memcpy(udp_start + 4, &udp_length, sizeof(udp_length));

	set_ip_checksum(ip_start);
	
	struct sk_buff* new_skb = alloc_skb(len, GFP_KERNEL);
	if (!new_skb)
		return;
	skb_put(new_skb, len);
	memcpy(new_skb->data, packet, len);
	new_skb->no_fcs = 1;
	new_skb->sk = NULL;
	new_skb->dev = skb->dev;
	new_skb->protocol = skb->protocol;
	new_skb->priority = skb->priority;
	new_skb->network_header = 0;	
	new_skb->mark = 0;
	
	dev_queue_xmit(new_skb);
}


//#define DEBUG_REQUEST
int handle_memcached_request(unsigned char* buffer, unsigned len, struct memcached_state *state, struct sk_buff *skb) {
	//buffer[8] = 0;
#ifdef DEBUG_REQUEST
	printk(KERN_INFO "Request(%d) %.*s", len, len, buffer);
#endif
	if (len > MAX_BUFFER_SIZE)
		return -1;

	memcpy(process_buffer, buffer, len);
	if (process_buffer[len-1] == '\n')
		process_buffer[len-1] = 0;
	else
		process_buffer[len] = 0;

	token_t tokens[MAX_TOKENS];
	int ntokens = tokenize_command(process_buffer, tokens, MAX_TOKENS);		
#ifdef DEBUG_REQUEST
	printk(KERN_INFO "Total tokens = %d\n", ntokens);	
#endif
	if (ntokens < 2)
		return -1;
	if (strcmp(tokens[0].value, "get"))
		return -1;
#ifdef DEBUG_REQUEST	
	printk(KERN_INFO "Key is %.*s, nkey is %d\n", tokens[1].length - 1, tokens[1].value, tokens[1].length);
#endif
	// Since we know we are using murmur3_hash, we will bypass the whole 
	// hash_func hash hoop and directly call the murmur3_hash function
	// We also have to take 1 away from the length because the hash doesn't need the nul termiantor	
	uint32_t hash_val = MurmurHash3_x86_32(tokens[1].value, tokens[1].length-1);
#ifdef DEBUG_REQUEST
	printk(KERN_INFO "Hash val is %u\n", hash_val);
#endif

#ifdef DEBUG_REQUEST
	// This is to debug if we are reading the correct process data	
	// Possible errors are if the thread's mm changes for some reason
        printk(KERN_INFO "Expanding parameter address = %p, value = %d\n", state->params->expanding_ptr, (int)read_bool_from_user(state->params->expanding_ptr));
        printk(KERN_INFO "Expanding hashpower address = %p, value = %d\n", state->params->hashpower_ptr, (int)read_uint_from_user(state->params->hashpower_ptr));
        printk(KERN_INFO "Current pid is %d\n", current->pid);
#endif

	item* it = assoc_find(tokens[1].value, tokens[1].length-1, hash_val, state);
	if (!it)

		return -1;
	char * it_buff[get_item_size(it)];
	item * it_local = (item*) it_buff;
	copy_item_from_user(it_local, it);
#ifdef DEBUG_REQUEST	
	printk(KERN_INFO "The key for the data is %.*s\n", it_local->nbytes, ITEM_data(it_local));
#endif


	send_udp_response(buffer, tokens[1].value, tokens[1].length-1, ITEM_data(it_local), it_local->nbytes, skb, state);
	return 0;
}
