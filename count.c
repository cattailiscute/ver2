#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/ktime.h>

#define CLASS_NAME "sysprog_gpio"
#define MAX_GPIO 10
#define GPIOCHIP_BASE 512

#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)
#define GPIO_IOCTL_DISABLE_IRQ _IOW(GPIO_IOCTL_MAGIC, 2, int)
#define GPIO_IOCTL_GET_COUNT   _IOR(GPIO_IOCTL_MAGIC, 3, int)

static dev_t dev_num_base;
static struct cdev gpio_cdev;
static int major_num;

struct gpio_entry {
    int bcm_num;
    struct gpio_desc *desc;
    struct device *dev;
    int irq_num;
    bool irq_enabled;
    struct fasync_struct *async_queue;
    ktime_t last_time;
};

static struct class *gpiod_class;
static struct gpio_entry *gpio_table[MAX_GPIO];
static atomic_t people_count = ATOMIC_INIT(0);

// ---- SYSFS ATTRIBUTES ----

static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    int val = gpiod_get_value(entry->desc);
    return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t value_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    if (gpiod_get_direction(entry->desc)) return -EPERM;
    if (sysfs_streq(buf, "1")) gpiod_set_value(entry->desc, 1);
    else if (sysfs_streq(buf, "0")) gpiod_set_value(entry->desc, 0);
    else return -EINVAL;
    return count;
}

static ssize_t direction_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    int dir = gpiod_get_direction(entry->desc);
    return scnprintf(buf, PAGE_SIZE, "%s\n", dir ? "in" : "out");
}

static ssize_t direction_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    struct gpio_entry *entry = dev_get_drvdata(dev);
    if (sysfs_streq(buf, "in")) gpiod_direction_input(entry->desc);
    else if (sysfs_streq(buf, "out")) gpiod_direction_output(entry->desc, 0);
    else return -EINVAL;
    return count;
}

static DEVICE_ATTR_RW(value);
static DEVICE_ATTR_RW(direction);

// ---- IRQ HANDLER ----

static irqreturn_t gpio_irq_handler(int irq, void *dev_id) {
    struct gpio_entry *entry = dev_id;
    ktime_t now = ktime_get();
    s64 delta_us = ktime_to_us(ktime_sub(now, entry->last_time));
    entry->last_time = now;

    int val = gpiod_get_value(entry->desc);
    if (val == 0) {
        if (delta_us > 180000 && delta_us < 220000) {
            atomic_dec(&people_count);
            pr_info("[PeopleCounter] Detected EXIT (delta: %lld us), count: %d\n", delta_us, atomic_read(&people_count));
        } else if (delta_us > 80000 && delta_us < 120000) {
            atomic_inc(&people_count);
            pr_info("[PeopleCounter] Detected ENTRY (delta: %lld us), count: %d\n", delta_us, atomic_read(&people_count));
        } else {
            pr_info("[PeopleCounter] Ignored pulse (delta: %lld us)\n", delta_us);
        }
    }

    if (entry->async_queue)
        kill_fasync(&entry->async_queue, SIGIO, POLL_IN);

    return IRQ_HANDLED;
}

// ---- FILE OPERATIONS ----

static int gpio_fops_open(struct inode *inode, struct file *filp) {
    int minor = iminor(inode);
    if (minor >= MAX_GPIO || !gpio_table[minor])
        return -ENODEV;
    filp->private_data = gpio_table[minor];
    return 0;
}

static int gpio_fops_release(struct inode *inode, struct file *filp) {
    struct gpio_entry *entry = filp->private_data;
    if (entry && entry->irq_enabled) {
        free_irq(entry->irq_num, entry);
        entry->irq_enabled = false;
    }
    fasync_helper(-1, filp, 0, &entry->async_queue);
    return 0;
}

static int gpio_fops_fasync(int fd, struct file *filp, int mode) {
    struct gpio_entry *entry = filp->private_data;
    return fasync_helper(fd, filp, mode, &entry->async_queue);
}

static long gpio_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    struct gpio_entry *entry = filp->private_data;
    int irq;

    switch (cmd) {
    case GPIO_IOCTL_ENABLE_IRQ:
        if (entry->irq_enabled)
            return -EBUSY;
        irq = gpiod_to_irq(entry->desc);
        if (irq < 0) return -EINVAL;
        if (request_irq(irq, gpio_irq_handler,
                        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                        "gpio_irq", entry)) {
            pr_err("[sysprog_gpio] IRQ request failed\n");
            return -EIO;
        }
        entry->irq_num = irq;
        entry->irq_enabled = true;
        entry->last_time = ktime_get();
        return 0;
    case GPIO_IOCTL_DISABLE_IRQ:
        if (!entry->irq_enabled)
            return -EINVAL;
        free_irq(entry->irq_num, entry);
        entry->irq_enabled = false;
        return 0;
    case GPIO_IOCTL_GET_COUNT:
        {
            int val = atomic_read(&people_count);
            if (copy_to_user((int __user *)arg, &val, sizeof(int)))
                return -EFAULT;
            return 0;
        }
    default:
        return -ENOTTY;
    }
}

static ssize_t gpio_fops_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    char out[16];
    int count = atomic_read(&people_count);
    int written = snprintf(out, sizeof(out), "%d\n", count);

    if (*off > 0 || len < written)
        return 0;

    if (copy_to_user(buf, out, written))
        return -EFAULT;

    *off += written;
    return written;
}

static ssize_t gpio_fops_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    struct gpio_entry *entry = filp->private_data;
    char kbuf[8] = {0};
    if (len >= sizeof(kbuf)) return -EINVAL;
    if (copy_from_user(kbuf, buf, len)) return -EFAULT;
    kbuf[len] = '\0';
    if (sysfs_streq(kbuf, "1")) {
        if (gpiod_get_direction(entry->desc)) return -EPERM;
        gpiod_set_value(entry->desc, 1);
    } else if (sysfs_streq(kbuf, "0")) {
        if (gpiod_get_direction(entry->desc)) return -EPERM;
        gpiod_set_value(entry->desc, 0);
    } else if (sysfs_streq(kbuf, "in")) {
        gpiod_direction_input(entry->desc);
    } else if (sysfs_streq(kbuf, "out")) {
        gpiod_direction_output(entry->desc, 0);
    } else {
        return -EINVAL;
    }
    return len;
}

static const struct file_operations gpio_fops = {
    .owner = THIS_MODULE,
    .open = gpio_fops_open,
    .read = gpio_fops_read,
    .write = gpio_fops_write,
    .release = gpio_fops_release,
    .fasync = gpio_fops_fasync,
    .unlocked_ioctl = gpio_fops_ioctl,
};

// ---- SYSFS EXPORT / UNEXPORT ----

static ssize_t export_store(const struct class *class, const struct class_attribute *attr, const char *buf, size_t count) {
    int bcm, minor;
    struct gpio_entry *entry;
    struct device *dev;

    if (kstrtoint(buf, 10, &bcm))
        return -EINVAL;

    for (minor = 0; minor < MAX_GPIO; minor++) {
        if (!gpio_table[minor])
            break;
    }
    if (minor == MAX_GPIO)
        return -ENOMEM;

    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry)
        return -ENOMEM;

    entry->bcm_num = bcm;
    entry->desc = gpio_to_desc(GPIOCHIP_BASE + bcm);
    if (!entry->desc) {
        kfree(entry);
        return -ENODEV;
    }

    gpiod_direction_input(entry->desc);
    dev = device_create(gpiod_class, NULL, MKDEV(major_num, minor), NULL, "gpio%d", bcm);
    if (IS_ERR(dev)) {
        kfree(entry);
        return PTR_ERR(dev);
    }

    entry->dev = dev;
    dev_set_drvdata(dev, entry);
    device_create_file(dev, &dev_attr_value);
    device_create_file(dev, &dev_attr_direction);

    gpio_table[minor] = entry;

    pr_info("[sysprog_gpio] Exported GPIO %d at minor %d\n", bcm, minor);
    return count;
}

static ssize_t unexport_store(const struct class *class, const struct class_attribute *attr, const char *buf, size_t count) {
    int bcm, idx;

    if (kstrtoint(buf, 10, &bcm))
        return -EINVAL;

    for (idx = 0; idx < MAX_GPIO; idx++) {
        if (gpio_table[idx] && gpio_table[idx]->bcm_num == bcm)
            break;
    }

    if (idx == MAX_GPIO)
        return -ENOENT;

    struct gpio_entry *entry = gpio_table[idx];
    device_remove_file(entry->dev, &dev_attr_value);
    device_remove_file(entry->dev, &dev_attr_direction);
    device_destroy(gpiod_class, MKDEV(major_num, idx));
    kfree(entry);
    gpio_table[idx] = NULL;

    pr_info("[sysprog_gpio] Unexported GPIO %d\n", bcm);
    return count;
}

static CLASS_ATTR_WO(export);
static CLASS_ATTR_WO(unexport);

// ---- MODULE INIT / EXIT ----

static int __init gpio_driver_init(void) {
    int ret;

    pr_info("[sysprog_gpio] module loading\n");

    gpiod_class = class_create(CLASS_NAME);
    if (IS_ERR(gpiod_class)) {
        pr_err("[sysprog_gpio] Failed to create class\n");
        return PTR_ERR(gpiod_class);
    }

    ret = class_create_file(gpiod_class, &class_attr_export);
    if (ret) {
        pr_err("[sysprog_gpio] Failed to create export attribute\n");
        class_destroy(gpiod_class);
        return ret;
    }

    ret = class_create_file(gpiod_class, &class_attr_unexport);
    if (ret) {
        pr_err("[sysprog_gpio] Failed to create unexport attribute\n");
        class_remove_file(gpiod_class, &class_attr_export);
        class_destroy(gpiod_class);
        return ret;
    }

    ret = alloc_chrdev_region(&dev_num_base, 0, MAX_GPIO, "gpio");
    if (ret) {
        pr_err("[sysprog_gpio] alloc_chrdev_region failed\n");
        class_remove_file(gpiod_class, &class_attr_export);
        class_remove_file(gpiod_class, &class_attr_unexport);
        class_destroy(gpiod_class);
        return ret;
    }

    major_num = MAJOR(dev_num_base);
    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_cdev, dev_num_base, MAX_GPIO);
    if (ret) {
        pr_err("[sysprog_gpio] cdev_add failed\n");
        unregister_chrdev_region(dev_num_base, MAX_GPIO);
        class_remove_file(gpiod_class, &class_attr_export);
        class_remove_file(gpiod_class, &class_attr_unexport);
        class_destroy(gpiod_class);
        return ret;
    }

    pr_info("[sysprog_gpio] Module initialized successfully\n");
    return 0;
}

static void __exit gpio_driver_exit(void) {
    for (int i = 0; i < MAX_GPIO; i++) {
        if (gpio_table[i]) {
            device_remove_file(gpio_table[i]->dev, &dev_attr_value);
            device_remove_file(gpio_table[i]->dev, &dev_attr_direction);
            device_destroy(gpiod_class, MKDEV(major_num, i));
            kfree(gpio_table[i]);
            gpio_table[i] = NULL;
        }
    }

    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev_num_base, MAX_GPIO);
    class_destroy(gpiod_class);

    pr_info("[sysprog_gpio] module unloaded\n");
}

module_init(gpio_driver_init);
module_exit(gpio_driver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiwon Shin");
MODULE_DESCRIPTION("GPIO driver for people counter with sysfs and IRQ support");
