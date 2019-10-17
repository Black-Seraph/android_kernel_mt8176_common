#include <linux/reboot.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mtd/mtd.h>
#include <asm/uaccess.h>

static int at88_open(struct inode *inode, struct file *filp)
{
	return 0; 
}
static int at88_release (struct inode *inode, struct file *filp)
{
	return 0; 
}
static ssize_t at88_read(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
	return 0;
}

struct file_operations at88_ops = {
	.open = at88_open,
	.read =  at88_read,
	.release = at88_release,
};
//static DEVICE_ATTR(test, S_IRUGO | S_IWUSR, NULL, NULL);
/*************************************************************************************************/

static int encrypt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *class_dev = NULL;
	struct class *at_class = NULL;
	/**********************************************/
	struct cdev *at_cdev;
	int alloc_ret = 0;
	int cdev_err = 0;
	dev_t dev;
printk("%s %d\n",__func__,__LINE__);
	at_cdev = kzalloc(sizeof(struct cdev), GFP_KERNEL);
	alloc_ret = alloc_chrdev_region(&dev, 0, 1, "at88");
	if (alloc_ret) {
		printk("at88 cdev can't alloc\n");
	}
	// allocate the character device
	cdev_init(at_cdev, &at88_ops);
	at_cdev->owner = THIS_MODULE;
	cdev_err = cdev_add(at_cdev, dev, 1);
	if(cdev_err) {
		printk("cdev_add for at88 fail!\n");
	}


	at_class = class_create(THIS_MODULE, "at88");
	class_dev = device_create(at_class, NULL, dev, NULL, "at88");
	/**********************************************/
printk("%s %d\n",__func__,__LINE__);
	return ret;
}

static int encrypt_remove(struct platform_device *pdev)
{
	return 0;
}
static struct platform_driver at88scxx_driver = {
	.driver         = {
	     .name   = "at88scxx",
	     .owner  = THIS_MODULE,
	},
	.probe          = encrypt_probe,
	.remove         = encrypt_remove,
	.suspend        = NULL,
	.resume         = NULL,
};

static struct platform_device at88scxx_device = {
	.name           = "at88scxx",
	.id             = -1,
};


static int __init at88scxx_init(void)
{
	int ret=0;

	platform_device_register(&at88scxx_device);
	ret = platform_driver_register(&at88scxx_driver);
	if (ret)
	     printk(KERN_ERR "%s: failed to at88scxx driver\n", __func__);

	return ret;
}

static void __exit at88scxx_exit(void)
{
	platform_driver_unregister(&at88scxx_driver);
	platform_device_unregister(&at88scxx_device);
}

module_init(at88scxx_init);
module_exit(at88scxx_exit);

MODULE_AUTHOR("Semtech Corp.");
MODULE_DESCRIPTION("AT88SCXX Driver");
MODULE_LICENSE("GPL");
