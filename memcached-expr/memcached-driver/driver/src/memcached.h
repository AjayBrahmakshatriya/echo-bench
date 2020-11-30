#ifndef MEMCACHED_DRIVER_H
#define MEMCACHED_DRIVER_H
#include <linux/types.h>

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


#define MAX_TOKENS 24
#define MAX_BUFFER_SIZE 1024
typedef struct token_s {
    char *value;
    size_t length;
} token_t;

uint32_t MurmurHash3_x86_32(const void *key, size_t length);


#define ITEM_CAS (2)
#define ITEM_key(item) (((char*)&((item)->data)) \
         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

struct memcached_params {
	char interface_name[16];
       	bool * expanding_ptr; 
	unsigned int * hashpower_ptr;	
      	item*** primary_hashtable_ptr; 
};

struct memcached_state {
	unsigned long long ready;	
	struct packet_type *proto;
	unsigned required_port;
        struct memcached_params *params;
};

void handle_memcached_request(unsigned char* buffer, unsigned len, struct memcached_state* state);
item* assoc_find(const char * key, const size_t nkey, const uint32_t hv, struct memcached_state * state);

#endif

