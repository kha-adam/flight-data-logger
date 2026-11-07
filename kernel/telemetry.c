#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>

#include "telemetry_uapi.h"

static u32 telemetry_seq;

static int telemetry_open(struct inode *inode, struct file *filp)
{
	pr_info("Telemetry Open \n");
	return 0;
}

static int telemetry_release(struct inode *inode, struct file *filp)
{
	pr_info("Content processed, close\n");
	return 0;
}

static ssize_t telemetry_read(struct file *filp, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct telemetry_sample sample;
    unsigned long failed_bytes;
	if (count < sizeof(sample)){
		return -EINVAL;
	}
	sample.seq = telemetry_seq;
	sample.ts_ns = ktime_get_ns();
	sample.pressure_pa = 1000;
	sample.temp_mc=20000;
	sample.reserved = 0;
	failed_bytes = copy_to_user(buf, &sample, sizeof(sample));
	if (failed_bytes != 0){
		return -EFAULT;
	}
	telemetry_seq ++;
	*ppos+= sizeof(sample);
	return sizeof(sample);
}

static const struct file_operations telemetry_fops = {
	.owner = THIS_MODULE,
	.open = telemetry_open,
	.release = telemetry_release,
	.read = telemetry_read,
};

static struct miscdevice telemetry_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "telemetry",
	.fops = &telemetry_fops,
	.mode = 0644,
};

static int __init telemetry_init(void)
{
	int ret;
	ret = misc_register(&telemetry_misc);
	return ret;
}

static void __exit telemetry_exit(void)
{
	misc_deregister(&telemetry_misc);
}
module_init(telemetry_init);
module_exit(telemetry_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam");
MODULE_DESCRIPTION("Telemetry character device");
