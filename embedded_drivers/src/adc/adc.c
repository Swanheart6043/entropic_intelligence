#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <asm/uaccess.h>
#include <asm/ioctl.h>

unsigned int major = 0;
const char * modulename = "adc";
dev_t devnum = 0;
struct class  *cls = NULL;
struct device *dev = NULL;

#define ADCCON 0x126C0000  
#define ADCMUX 0x126C001C             
#define ADCDAT 0x126C000C

static unsigned int* adccon;
static unsigned int* adcmux;
static unsigned int* adcdat;

unsigned int adc_value;

int demo_open(struct inode *inode, struct file *filp) {
	printk("--->%s--->%d.\n",__FUNCTION__,__LINE__);
	return 0;
}

int demo_release(struct inode *inode, struct file *filp) {
	printk("--->%s--->%d.\n",__FUNCTION__,__LINE__);
	return 0;
}

ssize_t demo_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset) {
	int bytes = 0;
	printk("---->%s--->%d\n", __func__, __LINE__);
    // 开始转换
    writel(readl(adccon) | 1, adccon);
    // 等待转换完成
    while(!(readl(adccon) & (1 << 15)));
    /*读取转换结果*/
    writel(readl(adcdat) & 0xFFF, adccon);
    adc_value = adccon;
	bytes =	copy_to_user(usrbuf, adc_value, 4);
	if(bytes > 0){
		printk("copy_to_user failed!\n");
	}
	return 0;
}

const struct file_operations fops = {
	.open = demo_open,
	.release = demo_release,
    .read = demo_read,
};

static int __init  adc_init(void) {
	major = register_chrdev(0, modulename, &fops);
	if(major <= 0){
		printk("register_chrdev failed.\n");
		return -1;
	}
	devnum = MKDEV(major, 0);
	printk(KERN_INFO "major :%d.\n",major);
	cls = class_create(THIS_MODULE, "fsadc");
	if (cls == NULL) {
		printk("class_create failed");
		return -1;
	}
	dev = device_create(cls, NULL, devnum,NULL, "adc%d",0);
	if (dev == NULL) {
		printk("device_create failed.\n");
		return -1;
	}
    // start
    adccon = ioremap(ADCCON, 4);
    adcmux = ioremap(ADCMUX, 4);
    adcdat = ioremap(ADCDAT, 4);
    // 设置ADC精度为12bit
    writel(readl(adccon) | (1 << 16), adccon);
    // 使能ADC分频器
    writel(readl(adccon) | (1 << 14), adccon);
    // 设置ADC分频值 ADC时钟频率 = PLCK / (19+1) = 5MHZ ADC转换频率 =5MHZ / 5 = 1MHZ
    writel(readl(adccon) & (~(0xFF << 6)) | (19 << 6), adccon);
    // 关闭待机模式，使能正常模式
    writel(readl(adccon) & (~(1 << 2)), adccon);
    // 关闭通过读使能AD转换
    writel(readl(adccon) & (~(1 << 1)), adccon);
    // 选择转换通道，3通道
    writel(3, adcmux);
	return 0;
}

static void __exit adc_exit(void) {
	iounmap(adccon);
	iounmap(adcmux);
    iounmap(adcdat);
	device_destroy(cls,devnum);
	class_destroy(cls);
	unregister_chrdev(major, modulename);
}

module_init(adc_init);
module_exit(adc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael_Hu");
MODULE_DESCRIPTION("adc driver");
