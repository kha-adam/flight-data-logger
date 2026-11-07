obj-m += telemetry.o

KDIR ?= /lib/modules/$(shell uname -r)/build
ccflags-y += -I$(src)/../include

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
