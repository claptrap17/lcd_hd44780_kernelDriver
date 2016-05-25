obj-m := hd44780.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	
install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) install

