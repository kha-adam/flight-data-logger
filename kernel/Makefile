obj-m += telemetry.o

KDIR ?= /lib/modules/$(shell uname -r)/build
ccflags-y += -I$(src)/../include

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
