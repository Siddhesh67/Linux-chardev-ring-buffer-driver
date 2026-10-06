#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include "mychardev_ioctl.h"

#define DEVICE_NAME "mychardev"
#define RING_BUF_SIZE 256

static int major_number;
static struct class *mychardev_class = NULL;
static struct device *mychardev_device = NULL;

static char ring_buf[RING_BUF_SIZE];
static size_t head = 0;
static size_t tail = 0;
static size_t count = 0;

static DEFINE_MUTEX(ring_mutex);
static DECLARE_WAIT_QUEUE_HEAD(read_queue);

static int mychardev_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static int dev_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "mychardev: device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "mychardev: device closed\n");
    return 0;
}

static long dev_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    int current_count;

    switch (cmd) {
    case MYCHARDEV_GET_COUNT:
        mutex_lock(&ring_mutex);
        current_count = (int)count;
        mutex_unlock(&ring_mutex);

        if (copy_to_user((int __user *)arg, &current_count, sizeof(current_count)))
            return -EFAULT;

        printk(KERN_INFO "mychardev: ioctl GET_COUNT returned %d\n", current_count);
        return 0;

    case MYCHARDEV_CLEAR:
        mutex_lock(&ring_mutex);
        head = 0;
        tail = 0;
        count = 0;
        mutex_unlock(&ring_mutex);

        printk(KERN_INFO "mychardev: ioctl CLEAR executed\n");
        return 0;

    default:
        return -ENOTTY;
    }
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    size_t to_copy;
    size_t i;
    char tmp[RING_BUF_SIZE];

    if (mutex_lock_interruptible(&ring_mutex))
        return -ERESTARTSYS;

    while (count == 0) {
        mutex_unlock(&ring_mutex);

        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN;

        printk(KERN_INFO "mychardev: reader blocking, buffer empty\n");

        if (wait_event_interruptible(read_queue, count != 0))
            return -ERESTARTSYS;

        if (mutex_lock_interruptible(&ring_mutex))
            return -ERESTARTSYS;
    }

    to_copy = (len < count) ? len : count;

    for (i = 0; i < to_copy; i++) {
        tmp[i] = ring_buf[tail];
        tail = (tail + 1) % RING_BUF_SIZE;
    }
    count -= to_copy;

    mutex_unlock(&ring_mutex);

    if (copy_to_user(buffer, tmp, to_copy) != 0)
        return -EFAULT;

    printk(KERN_INFO "mychardev: sent %zu bytes to user\n", to_copy);
    return to_copy;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    size_t space;
    size_t to_copy;
    size_t i;
    char tmp[RING_BUF_SIZE];

    if (len > RING_BUF_SIZE)
        len = RING_BUF_SIZE;

    if (copy_from_user(tmp, buffer, len) != 0)
        return -EFAULT;

    if (mutex_lock_interruptible(&ring_mutex))
        return -ERESTARTSYS;

    space = RING_BUF_SIZE - count;
    to_copy = (len < space) ? len : space;

    for (i = 0; i < to_copy; i++) {
        ring_buf[head] = tmp[i];
        head = (head + 1) % RING_BUF_SIZE;
    }
    count += to_copy;

    mutex_unlock(&ring_mutex);

    wake_up_interruptible(&read_queue);


    printk(KERN_INFO "mychardev: received %zu bytes from user (buffer now holds %zu)\n", to_copy, count);
    return to_copy;
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
    .unlocked_ioctl = dev_ioctl,
};

static int __init chardev_init(void)
{
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "mychardev: failed to register a major number\n");
        return major_number;
    }
    printk(KERN_INFO "mychardev: registered with major number %d\n", major_number);

    mychardev_class = class_create(DEVICE_NAME);
    if (IS_ERR(mychardev_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "mychardev: failed to register device class\n");
        return PTR_ERR(mychardev_class);
    }
    mychardev_class->dev_uevent = mychardev_uevent;
    printk(KERN_INFO "mychardev: device class registered\n");

    mychardev_device = device_create(mychardev_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(mychardev_device)) {
        class_destroy(mychardev_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "mychardev: failed to create the device\n");
        return PTR_ERR(mychardev_device);
    }
    printk(KERN_INFO "mychardev: device created at /dev/%s\n", DEVICE_NAME);

    return 0;
}

static void __exit chardev_exit(void)
{
    device_destroy(mychardev_class, MKDEV(major_number, 0));
    class_destroy(mychardev_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "mychardev: unregistered\n");
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sid");
MODULE_DESCRIPTION("Character device driver with ring buffer");
