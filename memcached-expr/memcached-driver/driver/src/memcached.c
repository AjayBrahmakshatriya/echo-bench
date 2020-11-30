#include "memcached.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>

char process_buffer[MAX_BUFFER_SIZE];

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

item *assoc_find(const char *key, const size_t nkey, const uint32_t hv, struct memcached_state* state) {
    bool expanding = *(state->params->expanding_ptr);
    unsigned int hashpower = *(state->params->hashpower_ptr);
    item** primary_hashtable = *(state->params->primary_hashtable_ptr); 
    item *it;
    unsigned int oldbucket;

    if (expanding)
	return NULL;

    it = primary_hashtable[hv & hashmask(hashpower)];

    item *ret = NULL;
    int depth = 0;
    while (it) {
        if ((nkey == it->nkey) && (memcmp(key, ITEM_key(it), nkey) == 0)) {
            ret = it;
            break;
        }
        it = it->h_next;
        ++depth;
    }
    return ret;
}

//#define DEBUG_REQUEST
void handle_memcached_request(unsigned char* buffer, unsigned len, struct memcached_state *state) {
	//buffer[8] = 0;
#ifdef DEBUG_REQUEST
	printk(KERN_INFO "Request(%d) %.*s", len, len, buffer);
#endif
	if (len > MAX_BUFFER_SIZE)
		return;

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
		return;
	if (strcmp(tokens[0].value, "get"))
		return;
#ifdef DEBUG_REQUEST	
	printk(KERN_INFO "Key is %s\n", tokens[1].value);
#endif
	// Since we know we are using murmur3_hash, we will bypass the whole 
	// hash_func hash hoop and directly call the murmur3_hash function
	
	uint32_t hash_val = MurmurHash3_x86_32(tokens[1].value, tokens[1].length);
#ifdef DEBUG_REQUEST
	printk(KERN_INFO "Hash val is %u\n", hash_val);
#endif

	item* it = assoc_find(tokens[1].value, tokens[1].length, hash_val, state);
	printk(KERN_INFO "Item is %p\n", it);

}
