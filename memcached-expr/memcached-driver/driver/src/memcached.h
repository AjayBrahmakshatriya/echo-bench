#ifndef MEMCACHED_DRIVER_H
#define MEMCACHED_DRIVER_H
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>

typedef unsigned int rel_time_t;
typedef struct _stritem {
    /* Protected by LRU locks */
    struct _stritem *next;
    struct _stritem *prev;
    /* Rest are protected by an item lock */
    struct _stritem *h_next;    /* hash chain next */
    rel_time_t      time;       /* least recent access */
    rel_time_t      exptime;    /* expire time */
    int             nbytes;     /* size of data */
    unsigned short  refcount;
    uint16_t        it_flags;   /* ITEM_* above */
    uint8_t         slabs_clsid;/* which slab class we're in */
    uint8_t         nkey;       /* key length, w/terminating null and padding */
    /* this odd type prevents type-punning issues when we do
     * the little shuffle to save space when not using CAS. */
    union {
        uint64_t cas;
        char end;
    } data[];
    /* if it_flags & ITEM_CAS we have 8 bytes CAS */
    /* then null-terminated key */
    /* then " flags length\r\n" (no terminating null) */
    /* then data with terminating \r\n (no terminating null; it's binary!) */
} item;


#define MAX_TOKENS (24)
#define MAX_BUFFER_SIZE (1024)
typedef struct token_s {
    char *value;
    size_t length;
} token_t;

uint32_t MurmurHash3_x86_32(const void *key, size_t length);


#define ITEM_CAS (2)
#define ITEM_CFLAGS (256)
#define ITEM_key(item) (((char*)&((item)->data)) \
         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_data(item) ((char*) &((item)->data) + (item)->nkey + 1 \
         + (((item)->it_flags & ITEM_CFLAGS) ? sizeof(uint32_t) : 0) \
         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_ntotal(item) (sizeof(struct _stritem) + (item)->nkey + 1 \
         + (item)->nbytes \
         + (((item)->it_flags & ITEM_CFLAGS) ? sizeof(uint32_t) : 0) \
         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

struct memcached_params {
	char interface_name[16];
       	bool * expanding_ptr; 
	unsigned int * hashpower_ptr;	
      	item*** primary_hashtable_ptr; 
};

#define MAX_SKB_QUEUE (16)
struct memcached_state {
	unsigned long long ready;	
	struct packet_type *proto;
	unsigned required_port;
        struct memcached_params *params;


	// There are the members related to the handler thread(s)
        struct task_struct * thread_handle;
	volatile atomic_t thread_to_kill;
        volatile atomic_t thread_killed;	
	struct mm_struct *process_mm;

	// These are the members related to the skb process queue 
        // Circular queue for holding the skbs to process
        // Queue implementation borrowed from -
        // https://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circul
        struct sk_buff *skb_queue[MAX_SKB_QUEUE];
        volatile atomic_t skb_queue_start; // Points to where to read from next
        volatile atomic_t skb_queue_max_read; // Points to where to read from max
        volatile atomic_t skb_queue_end; // Points to where to write next
};

static void init_memcached_state(struct memcached_state *state) {
        atomic_set(&(state->skb_queue_start), 0);
        atomic_set(&(state->skb_queue_max_read), 0);
        atomic_set(&(state->skb_queue_end), 0);
	atomic_set(&(state->thread_to_kill), 0);
	atomic_set(&(state->thread_killed), 0);
}



static int count_to_index(int count) {
	return count % MAX_SKB_QUEUE;
}
static int skb_add_to_queue(struct memcached_state* state, struct sk_buff *elem) {
	int curr_start, curr_end, new_end; 
	do {
		curr_start = atomic_read(&(state->skb_queue_start));
		curr_end = atomic_read(&(state->skb_queue_end));
		if (count_to_index(curr_end + 1) == count_to_index(curr_start))
			return -1;
		new_end = curr_end + 1;
	} while(atomic_cmpxchg(&(state->skb_queue_end), curr_end, new_end) != curr_end);
	// Now write the skb
        state->skb_queue[count_to_index(curr_end)] = elem;
        // Bump the max read, this should happen in reserve order
	while(atomic_cmpxchg(&(state->skb_queue_max_read), curr_end, curr_end + 1) != curr_end); 
   return 0; 
}

static struct sk_buff* skb_take_from_queue(struct memcached_state* state) {
	int curr_max;
	int curr_start;
	do {
		curr_start = atomic_read(&(state->skb_queue_start));
		curr_max = atomic_read(&(state->skb_queue_max_read));
		if (count_to_index(curr_start) == count_to_index(curr_max))
			return NULL;
		struct sk_buff *to_ret = state->skb_queue[count_to_index(curr_start)];
		if (atomic_cmpxchg(&(state->skb_queue_start), curr_start, curr_start+1) == curr_start)
			return to_ret;
	} while (1);
	// Should never reach here
	// assert(false);
	return NULL;
}

int handle_memcached_request(unsigned char* buffer, unsigned len, struct memcached_state* state, struct sk_buff* skb);
item* assoc_find(const char * key, const size_t nkey, const uint32_t hv, struct memcached_state * state);

#endif

