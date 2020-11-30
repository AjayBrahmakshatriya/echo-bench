TARGET=mem_driver
obj-m += $(TARGET).o
$(TARGET)-objs := driver.o ./src/memcached.o src/murmur3_hash.o

all: build

build:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


install: build
	- sudo rmmod mem_driver.ko
	sudo insmod mem_driver.ko
	sudo chmod 666 /dev/memcached_ctrl	


uninstall:
	sudo rmmod mem_driver.ko
