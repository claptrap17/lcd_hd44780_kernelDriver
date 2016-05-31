

#include <linux/module.h>	//for all modules
#include <linux/moduleparam.h>
#include <linux/kernel.h>	//for prink priority macros

#include <linux/init.h>		//for entry/exit macros
#include <linux/fs.h>

#include <linux/cdev.h>
#include <linux/ioctl.h>

#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/interrupt.h>

#include "ioctl_header.h"

//static dev_t hd44780_dev_number = MKDEV(230, 0);

static int major = 0;
static int minor = 0;
static int count = 1;

dev_t dev = 0;

static struct cdev *driver_object;
static struct class *hd44780_class;
static struct device *hd44780_dev;
static char textbuffer[1024];

//function prototypes
static void write_nibble(int regist, int value);
static void write_lcd(int regist, int value);
static int gpio_request_output(int nr);
static int exit_display(void);
static int clear_display(void);

static long function_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);

static ssize_t driver_write(struct file *instance, const char __user *user, size_t cnt, loff_t *offset);

static void __exit mod_exit(void);
static int __init init_display(void);

//module parameters -> allow arguments to be passed to modules
module_param(major, int, S_IRUGO | S_IWUSR);
module_param(count, int, S_IRUGO | S_IWUSR);

static void write_nibble(int regist, int value){

	gpio_set_value(7, regist);
	
	gpio_set_value(25, value & 0x1); //DATABIT 4
	gpio_set_value(24, value & 0x2); //DATABIT 5
	gpio_set_value(23, value & 0x4); //DATABIT 6
	gpio_set_value(18, value & 0x8); //DATABIT 7

	gpio_set_value(8, 1); //enabled to write values
	
	udelay(40);

	gpio_set_value(8, 0); //disabled to write values
}


static void write_lcd(int regist, int value){
	write_nibble(regist, value >> 4); //HIGH-Nibble logic
	write_nibble(regist, value & 0xf); //LOW-Nibble logic
}


static int gpio_request_output(int nr){
	
	char gpio_name[12];
	int err;

	snprintf( gpio_name, sizeof(gpio_name), "rpi-gpio-%d", nr);
	err = gpio_request(nr, gpio_name);

	if(err){
		printk("gpio request for %s failed with %d\n", gpio_name, err);
		return -1;
	}
	err = gpio_direction_output(nr, 0);
	if(err){
		printk("gpio direction output failed %d\n", err);
		gpio_free(nr);
		return -1;
	}
	return 0;
}


static int __init init_display(void){

	printk("initialize display\n");

	if(gpio_request_output(7) == -1){
		 return -EIO;
	}
	if(gpio_request_output(8) == -1){
		goto free7;
	}
	if(gpio_request_output(18) == -1){
		goto free8;
	}
	if(gpio_request_output(23) == -1){
		goto free18;
	}
	if(gpio_request_output(24) == -1){
		goto free23;
	}
	if(gpio_request_output(25) == -1){
		goto free24;
	}

msleep(15);
write_nibble(0, 0x3);
msleep(5);
write_nibble(0, 0x3);
udelay(100);
write_nibble(0, 0x3);
msleep(5);
write_nibble(0, 0x2);
msleep(5);
write_lcd(0, 0x28);	//Command: 4-Bit Mode, 2 lines
msleep(2);
write_lcd(0, 0x01);
msleep(2);

write_lcd(0, 0x0c);	 //display on, cursor off, blink off
write_lcd(0, 0xc0);
write_lcd(1, 'H');
write_lcd(1, 'a');
write_lcd(1, 'l');
write_lcd(1, 'l');
write_lcd(1, 'o');

	return 0;

free24: gpio_free(24);
free23: gpio_free(23);
free18: gpio_free(18);
free8: gpio_free(8);
free7: gpio_free(7);
	return -EIO;
}


static int exit_display(void){
	printk("exit display called\n");
	gpio_free(25);
	gpio_free(24);
	gpio_free(23);
	gpio_free(18);
	gpio_free(8);
	gpio_free(7);
	return 0;
}


static ssize_t driver_write(struct file *instance, const char __user *user, size_t cnt, loff_t *offset){

	unsigned long not_copied; 
	unsigned long to_copy;
	int i;

	char msg_from_user[26] = { 0 };

	to_copy = min(cnt, sizeof(textbuffer));
	not_copied = copy_from_user(textbuffer, user, to_copy);

	write_lcd(0, 0x80);

	for(i = 0; i < to_copy && textbuffer[i]; i++){
		if(isprint(textbuffer[i])){
			write_lcd(1, textbuffer[i]);
		}
		if( i == 15){
			write_lcd(0, 0xc0);
		}
	}

	if(copy_from_user(msg_from_user, user, cnt)) {
		printk("failed copy from user");
	}
return to_copy-not_copied;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = init_display,
	//.release = exit_display,
	.write = driver_write,
	.unlocked_ioctl = function_ioctl,
};

static int __init mod_init(void){
int error = 0; 	

dev = MKDEV(major, minor);

if(register_chrdev_region(MKDEV(major, 0),count,"hd44780") < 0){
	printk("devicenumber(255, 0) in use!\n");
	return -EIO;
}
else{
	error = alloc_chrdev_region(&dev, 0, count, "hd44780");
	major = MAJOR(dev);
}

driver_object = cdev_alloc();	/* registered object reserved*/

if(driver_object == NULL){
	goto free_device_number;
}

driver_object->owner = THIS_MODULE;
driver_object->ops = &fops;
	
if(cdev_add(driver_object, dev, 1)){
	goto free_cdev;
}

hd44780_class = class_create(THIS_MODULE, "hd44780");

if(IS_ERR(hd44780_class)){
	pr_err("hd44780: no udev support!\n");
	goto free_cdev;
}

hd44780_dev = device_create(hd44780_class, NULL, dev, NULL, "%s", "hd44780");
dev_info(hd44780_dev, "mod_init called\n");

if(init_display() == 0){
	return 0;
}

free_cdev:
	kobject_put(&driver_object->kobj);
free_device_number:
	unregister_chrdev_region(dev, 1);
	printk("mod_init failed\n");
	return -EIO;
}

static void __exit mod_exit(void){
	dev_info(hd44780_dev, "mod_exit called\n");
	exit_display();
	device_destroy(hd44780_class, dev);
	class_destroy(hd44780_class);
	cdev_del(driver_object);
	unregister_chrdev_region(dev, 1);
	return;
}

static long function_ioctl(struct file *fp, unsigned int cmd, unsigned long arg){
int retval = 0;
int value = 5;

switch(cmd){
	case IOCTL_WRITE:
//		put_user(value, (int *)arg);
		printk("IOCTL_WRITE was selected\n");
		break;
//	case IOCTL_CLEAR:
//		put_user(value, (int *)arg);
//		printk("IOCTL_CLEAR was selected\n");
//		break;
	default:
		printk("no argument was selected\n");
		retval = -EFAULT;
		break;
	}
return retval;
}


static int clear_display(void){
	
	return 0;
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_AUTHOR("Daniel Obermaier <mailto:dan.obermaier@gmail.com>");
MODULE_DESCRIPTION("driver for LCD Display with HD44780 controller");
MODULE_LICENSE("GPL");
