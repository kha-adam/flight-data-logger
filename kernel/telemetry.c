#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "telemetry_uapi.h"

#define TELEM_BUF_SIZE         1024u
#define TELEM_BUF_MASK         (TELEM_BUF_SIZE - 1u)
#define TELEM_DEFAULT_RATE_HZ  100u


struct telemetry_dev {
	struct telemetry_sample *buf;
	unsigned int head;
	unsigned int tail;
	unsigned int dropped;
    unsigned int consumed;
    unsigned int high_watermark;
	u32 seq;
	u32 rate_hz;
	bool in_use;
	spinlock_t lock;
	struct hrtimer timer;
    wait_queue_head_t wq;
};

static unsigned int start_rate_hz = 100;
module_param_named(rate_hz, start_rate_hz, uint, 0644);
MODULE_PARM_DESC(rate_hz, "timer rate in Hz, 1 to 10000");

static unsigned int buf_samples = 1024;
module_param(buf_samples, uint, 0444);

static struct telemetry_dev telem;

static int telemetry_proc_show(struct seq_file *m, void *v)
{
    struct telemetry_dev *dev = &telem;
	struct telemetry_stats s;
	unsigned long flags;

	/* lock, fill s from telem, unlock */
    spin_lock_irqsave(&dev->lock, flags);
    s.produced = dev->seq;
    s.consumed = dev->consumed;
    s.dropped = dev->dropped;
    s.high_watermark = dev->high_watermark;
    s.rate_hz = dev->rate_hz;
    s.buf_samples = TELEM_BUF_SIZE;
    s.reserved = 0;
    spin_unlock_irqrestore(&dev->lock, flags);
	seq_printf(m, "produced: %llu\n", s.produced);
	seq_printf(m, "consumed: %llu\n", s.consumed);
	seq_printf(m, "dropped: %llu\n", s.dropped);
	seq_printf(m, "high_watermark: %u\n", s.high_watermark);
	seq_printf(m, "rate_hz: %u\n", s.rate_hz);
	seq_printf(m, "buf_samples: %u\n", s.buf_samples);
	/* consumed, dropped, high_watermark, rate_hz, buf_samples */
	return 0;
}
static int telemetry_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, telemetry_proc_show, NULL);
}

static const struct proc_ops telemetry_proc_ops = {
	.proc_open = telemetry_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* Called with dev->lock held. Copies one sample into the ring. */
static bool ring_push(struct telemetry_dev *dev, const struct telemetry_sample *sample)
{
	if (dev->head - dev->tail == TELEM_BUF_SIZE) {
		dev->dropped++;
		return false;
	}

	dev->buf[dev->head & TELEM_BUF_MASK] = *sample;
	dev->head++;
    if (dev->head - dev->tail > dev->high_watermark){
        dev->high_watermark = dev->head-dev->tail;
    }
	return true;
}

/* Called with dev->lock held. Copies one sample out of the ring. */
static bool ring_pop(struct telemetry_dev *dev, struct telemetry_sample *sample)
{
	if (dev->head == dev->tail)
		return false;

	*sample = dev->buf[dev->tail & TELEM_BUF_MASK];
	dev->tail++;
	return true;
}

static enum hrtimer_restart telemetry_timer(struct hrtimer *timer)
{
	struct telemetry_dev *dev = container_of(timer, struct telemetry_dev, timer);
	struct telemetry_sample sample;
	unsigned long flags;
	u32 seq;
	u32 phase;
    u32 rate;
    bool pushed;

	spin_lock_irqsave(&dev->lock, flags);
	seq = dev->seq++;
	phase = seq % 20;
	if (phase > 10)
		phase = 20 - phase;
	sample.seq = seq;
	sample.ts_ns = ktime_get_ns();
	sample.temp_mc = 20000 + (s32)phase * 500;
	sample.pressure_pa = 100000 + (seq % 50) * 10;
	sample.reserved = 0;
    pushed = ring_push(dev, &sample);
    rate = dev->rate_hz;
	spin_unlock_irqrestore(&dev->lock, flags);
    if (pushed){
        wake_up_interruptible(&dev->wq);
    }
	hrtimer_forward_now(timer, ns_to_ktime(1000000000ULL / rate));
	return HRTIMER_RESTART;
}

static int telemetry_open(struct inode *inode, struct file *filp)
{
	struct telemetry_dev *dev = &telem;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->in_use) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -EBUSY;
	}
	dev->in_use = true;
	dev->head = 0;
	dev->tail = 0;
	dev->seq = 0;
	dev->dropped = 0;
    dev->consumed = 0;
    dev->high_watermark = 0;
	filp->private_data = dev;
	spin_unlock_irqrestore(&dev->lock, flags);

	hrtimer_start(&dev->timer,
		      ns_to_ktime(1000000000ULL / dev->rate_hz),
		      HRTIMER_MODE_REL);
	pr_info("telemetry open\n");
	return 0;
}

static int telemetry_release(struct inode *inode, struct file *filp)
{
	struct telemetry_dev *dev = filp->private_data;
	unsigned long flags;

	hrtimer_cancel(&dev->timer);

	spin_lock_irqsave(&dev->lock, flags);
	dev->in_use = false;
	spin_unlock_irqrestore(&dev->lock, flags);
	pr_info("telemetry closed\n");
	return 0;
}

static ssize_t telemetry_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	struct telemetry_dev *dev = filp->private_data;
	struct telemetry_sample batch[16];
	size_t max_n, n = 0, copied, missed;
	unsigned long flags;
	unsigned long failed;
	int wait_ret;
	if (count < sizeof(batch[0]))
		return -EINVAL;
	max_n = min(ARRAY_SIZE(batch), count / sizeof(batch[0]));
	if (!(filp->f_flags & O_NONBLOCK)) {
		wait_ret = wait_event_interruptible(dev->wq,
				READ_ONCE(dev->head) != READ_ONCE(dev->tail));
		if (wait_ret)
			return wait_ret;
	}
	spin_lock_irqsave(&dev->lock, flags);
	while (n < max_n && ring_pop(dev, &batch[n]))
		n++;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (n == 0)
		return -EAGAIN;
	failed = copy_to_user(ubuf, batch, n * sizeof(batch[0]));
	copied = n * sizeof(batch[0]) - failed;
    copied -= copied % sizeof(batch[0]);
	missed = n - copied / sizeof(batch[0]);
	if (missed || copied) {
		spin_lock_irqsave(&dev->lock, flags);
		dev->dropped += missed;
        dev->consumed += copied / sizeof(batch[0]);
		spin_unlock_irqrestore(&dev->lock, flags);
	}
	if (copied == 0)
		return -EFAULT;
	*ppos += copied;
	return copied;
}
static __poll_t telemetry_poll(struct file *filp, poll_table *wait)
{
	struct telemetry_dev *dev = filp->private_data;
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(filp, &dev->wq, wait);

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->head != dev->tail)
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&dev->lock, flags);
	return mask;
}

static long telemetry_ioctl (struct file *filp, unsigned int cmd, unsigned long arg){
    struct telemetry_dev *dev = filp->private_data;
    unsigned long failed;
    unsigned long flags;
    __u32 rate;
    switch(cmd){
        case TELEM_IOC_SET_RATE:
            failed = copy_from_user(&rate, (void __user *) arg, sizeof(rate));
            if (failed != 0){
                return -EFAULT;
            }
            if (rate < 1 || rate > 10000){
                return -EINVAL;
            }
            spin_lock_irqsave(&dev->lock, flags);
            dev->rate_hz = rate;
            spin_unlock_irqrestore(&dev->lock, flags);
            return 0;
        case TELEM_IOC_GET_STATS:{
            struct telemetry_stats out;
            spin_lock_irqsave(&dev->lock, flags);
            out.produced = dev->seq;
            out.consumed = dev->consumed;
            out.dropped = dev->dropped;
            out.high_watermark = dev->high_watermark;
            out.rate_hz = dev->rate_hz;
            out.buf_samples = TELEM_BUF_SIZE;
            out.reserved = 0;
            spin_unlock_irqrestore(&dev->lock, flags);
            if (copy_to_user((void __user * )arg, &out, sizeof(out))){
                return -EFAULT;
            }
            return 0;
        }
        case TELEM_IOC_RESET:
            spin_lock_irqsave(&dev->lock, flags);
            dev->dropped = 0;
            dev->consumed = 0;
            dev->high_watermark = 0;
            spin_unlock_irqrestore(&dev->lock, flags);
            return 0;
        default:
            return -ENOTTY;
    }
}

static const struct file_operations telemetry_fops = {
	.owner = THIS_MODULE,
	.open = telemetry_open,
	.release = telemetry_release,
	.read = telemetry_read,
    .poll = telemetry_poll,
    .unlocked_ioctl = telemetry_ioctl,
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
    if (start_rate_hz < 1 || start_rate_hz > 10000){
        return -EINVAL;
    }
    telem.rate_hz = start_rate_hz;
    if(buf_samples != 1024){
        return -EINVAL;
    }
	telem.buf = kcalloc(TELEM_BUF_SIZE, sizeof(*telem.buf), GFP_KERNEL);
	if (!telem.buf)
		return -ENOMEM;

	spin_lock_init(&telem.lock);
    init_waitqueue_head(&telem.wq);
	hrtimer_setup(&telem.timer, telemetry_timer, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);

	ret = misc_register(&telemetry_misc);
	if (ret) {
		kfree(telem.buf);
		telem.buf = NULL;
		return ret;
	}
    if (!proc_create("telemetry_stats", 0444, NULL, &telemetry_proc_ops)) {
	    misc_deregister(&telemetry_misc);
	    kfree(telem.buf);
	    telem.buf = NULL;
	    return -ENOMEM;
    }
	return 0;
}

static void __exit telemetry_exit(void)
{
    remove_proc_entry("telemetry_stats", NULL);
	misc_deregister(&telemetry_misc);
	hrtimer_cancel(&telem.timer);
	kfree(telem.buf);
	telem.buf = NULL;

}

module_init(telemetry_init);
module_exit(telemetry_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adam");
MODULE_DESCRIPTION("Telemetry character device");
