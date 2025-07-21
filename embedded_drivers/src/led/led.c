#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include "../../../embedded_common/include/led.h"

static int __init led_init(void);
static void __exit led_exit(void);
static int led_probe(struct platform_device *p_pltdev);
static int led_remove(struct platform_device *p_pltdev);
static int led_open(struct inode *pnode, struct file *pfile);
static int led_release(struct inode *pnode, struct file *pfile);
static long led_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg);
static void led_ioremap(struct device_node *pnode);
static void led_iounmap(void);

static dev_t dev_num = MKDEV(0, 0);
static struct class* pcls;
static struct device* pdev;
static int dev_count = 1;
static struct cdev dev;
unsigned int led2gpio = 0;
unsigned int led3gpio = 0;
unsigned int led4gpio = 0;
unsigned int led5gpio = 0;

// 文件操作结构体定义为全局变量
static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
	.unlocked_ioctl = led_ioctl,
};

static int led_open(struct inode *pnode, struct file *pfile) {
    printk(KERN_INFO "led driver: device opened\n");
    return 0;
}

static int led_release(struct inode *pnode, struct file *pfile) {
    printk(KERN_INFO "led driver: device closed\n");
    return 0;
}

static long led_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg) {
    int ret = 0;
    if (arg < 2 || arg > 5) {
        return -1;
    }
    switch(cmd) {
        case LED_ON:
            if (arg == 2) {
                gpio_set_value(led2gpio,1);
            }
            if (arg == 3) {
                gpio_set_value(led3gpio,1);
            }
            if (arg == 4) {
                gpio_set_value(led4gpio,1);
            }
            if (arg == 5) {
                gpio_set_value(led5gpio,1);
            }
            break;
        case LED_OFF:
            if (arg == 2) {
                gpio_set_value(led2gpio,0);
            }
            if (arg == 3) {
                gpio_set_value(led3gpio,0);
            }
            if (arg == 4) {
                gpio_set_value(led4gpio,0);
            }
            if (arg == 5) {
                gpio_set_value(led5gpio,0);
            }
            break;
        case LED_GET_STATUS:
            break;
        default:
            return -ENOTTY;
    }
    return ret;
}

static void led_ioremap(struct device_node *pnode) {
    led2gpio = of_get_named_gpio(pnode, "led2-gpio", 0);
    gpio_request(led2gpio, "led2");
    led3gpio = of_get_named_gpio(pnode, "led3-gpio", 0);
    gpio_request(led3gpio, "led3");
    led4gpio = of_get_named_gpio(pnode, "led4-gpio", 0);
    gpio_request(led4gpio, "led4");
    led5gpio = of_get_named_gpio(pnode, "led5-gpio", 0);
    gpio_request(led5gpio, "led5");

    gpio_direction_output(led2gpio, 0);
	gpio_direction_output(led3gpio, 0);
	gpio_direction_output(led4gpio, 0);
	gpio_direction_output(led5gpio, 0);
}

static void led_iounmap() {
    gpio_free(led2gpio);
	gpio_free(led3gpio);
	gpio_free(led4gpio);
	gpio_free(led5gpio);
}

static struct class* match_class_create(const char *name) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
	// 6.4 及更高版本内核
	return class_create(name);
#else
	// 旧版本内核
	return class_create(THIS_MODULE, name);
#endif
}

static int led_probe(struct platform_device *p_pltdev) {
	int ret;
    printk(KERN_ALERT "=== LED PROBE FUNCTION CALLED ===\n");
    printk(KERN_INFO "Device name: %s\n", p_pltdev->name);
    printk(KERN_INFO "Device of_node: %p\n", p_pltdev->dev.of_node);

	// 申请设备号
	ret = alloc_chrdev_region(&dev_num, 0, dev_count, "led");
	if(ret) {
		printk(KERN_ERR "led driver: failed to allocate device number\n");
        return ret;
	}

	// 创建设备类
    pcls = match_class_create("led_class");
    if (IS_ERR(pcls)) {
        ret = PTR_ERR(pcls);
        printk(KERN_ERR "led driver: failed to create device class\n");
        unregister_chrdev_region(dev_num, dev_count);
        return ret;
    }

	// 创建设备文件
	pdev = device_create(pcls, NULL, dev_num, NULL, "led");
    if (IS_ERR(pdev)) {
		printk(KERN_ERR "led driver: failed to create device\n");
        class_destroy(pcls);
        unregister_chrdev_region(dev_num, dev_count);
        return PTR_ERR(pdev);
    }

	// 初始化 cdev 结构并添加到系统
    cdev_init(&dev, &led_fops); 
    dev.owner = THIS_MODULE;
    ret = cdev_add(&dev, dev_num, dev_count);
	if (ret) {
		printk(KERN_ERR "led driver: failed to add cdev\n");
        device_destroy(pcls, dev_num);
        class_destroy(pcls);
        unregister_chrdev_region(dev_num, dev_count);
        return ret;
	}
    
    led_ioremap(p_pltdev->dev.of_node);

	return 0;
}

static int led_remove(struct platform_device *p_pltdev) {
    // qing li
    led_iounmap();
	// 注意清理顺序：cdev -> device -> class -> dev_num
	cdev_del(&dev);
	device_destroy(pcls, dev_num);
    class_destroy(pcls);
	unregister_chrdev_region(dev_num, 1);
	printk(KERN_INFO "led driver: unloaded\n");
    return 0;
}

struct of_device_id myleddrv_of_ids[] = {
	[0] = {.compatible = "fs4412, led2-5"},
	[1] = {.compatible = "origen4412,led6-9"},
	[2] = {},
};

struct platform_driver led_driver = {
	.driver = {
		.name = "fs4412-leds",
		.of_match_table = myleddrv_of_ids,
	},
	.probe = led_probe,
	.remove = led_remove,
};

static int __init led_init(void) {
	platform_driver_register(&led_driver);
	return 0;
}

static void __exit led_exit(void) {
	platform_driver_unregister(&led_driver);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael_Hu");
MODULE_DESCRIPTION("led driver");
