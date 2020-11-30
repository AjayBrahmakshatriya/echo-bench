import memcached_udp
client = memcached_udp.Client([('10.0.0.1', 11211)])
print(client.get("mykey"))
