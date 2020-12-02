import memcached_udp
import time
import sys
client = memcached_udp.Client([('10.0.0.1', 11211)])
client.set("mykey", "A" * int(sys.argv[1]))
print(client.get("mykey"))
