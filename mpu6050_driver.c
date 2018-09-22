#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/delay.h>


#include<linux/init.h>
#include<linux/device.h>
#include <linux/of.h>		/* for struct device_node */
#include <linux/swab.h>
 
#include <asm/uaccess.h>
 
#include "mpu6050.h"
 
MODULE_LICENSE("GPL");

#define MPU6050_MAJOR 250
#define MPU6050_MINOR 0
 
struct mpu6050_device {
	struct cdev cdev;  //设备信息结构体
	struct i2c_client *client;  //适配器和从机匹配后记录从机设备的信息
};
struct mpu6050_device *mpu6050; 

/*读mpu6050数据寄存器的值*/
static int mpu6050_read_byte(struct i2c_client *client, unsigned char reg)
{
	int ret;
 
	char txbuf[1] = { reg };  //要读取的寄存器地址
	char rxbuf[1];
    /*构建适配器和从设备间传输的数据包*/
	struct i2c_msg msg[2] = {
		{client->addr, 0, 1, txbuf},          //源，写，长度，目的
		{client->addr, I2C_M_RD, 1, rxbuf}    //源，读，长度，目的
	};
 
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)); 
	if (ret < 0) {
		printk("ret = %d\n", ret);
		return ret;
	}
 
	return rxbuf[0];
}

/*向mpu6050控制器写入*/
static int mpu6050_write_byte(struct i2c_client *client, unsigned char reg, unsigned char val)
{
	char txbuf[2] = {reg, val};  //寄存器地址，要写入的值
    
	struct i2c_msg msg[2] = {
		{client->addr, 0, 2, txbuf},  
	};
 
	i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
 
	return 0;
}

static int mpu6050_open(struct inode *inode, struct file *file) 
{
	return 0;
}
 
static int mpu6050_release(struct inode *inode, struct file *file) 
{
	return 0;
}
 
static long mpu6050_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	union mpu6050_data data;
	struct i2c_client *client = mpu6050->client;
 
	switch(cmd) {
	case GET_ACCEL:
		data.accel.x = mpu6050_read_byte(client, ACCEL_XOUT_L);
		data.accel.x |= mpu6050_read_byte(client, ACCEL_XOUT_H) << 8;
 
		data.accel.y = mpu6050_read_byte(client, ACCEL_YOUT_L);
		data.accel.y |= mpu6050_read_byte(client, ACCEL_YOUT_H) << 8;
 
		data.accel.z = mpu6050_read_byte(client, ACCEL_ZOUT_L);
		data.accel.z |= mpu6050_read_byte(client, ACCEL_ZOUT_H) << 8;
		break;
 
	case GET_GYRO:
 
		data.gyro.x = mpu6050_read_byte(client, GYRO_XOUT_L);
		data.gyro.x |= mpu6050_read_byte(client, GYRO_XOUT_H) << 8;
 
		data.gyro.y = mpu6050_read_byte(client, GYRO_YOUT_L);
		data.gyro.y |= mpu6050_read_byte(client, GYRO_YOUT_H) << 8;
 
		data.gyro.z = mpu6050_read_byte(client, GYRO_ZOUT_L);
		data.gyro.z |= mpu6050_read_byte(client, GYRO_ZOUT_H) << 8;
		break;
 
	case GET_TEMP:	
		data.temp = mpu6050_read_byte(client, TEMP_OUT_L);
		data.temp |= mpu6050_read_byte(client, TEMP_OUT_H) << 8;
		break;
 
	default:
		printk("invalid argument\n");
		return -EINVAL;
	}
 
	if (copy_to_user((void *)arg, &data, sizeof(data)))
		return -EFAULT;
 
	return sizeof(data);
}
 
struct file_operations mpu6050_fops = {
	.owner 		= THIS_MODULE,
	.open		= mpu6050_open,
	.release 	= mpu6050_release,
	.unlocked_ioctl = mpu6050_ioctl,
};
 /*主设备对从设备的检测*/
static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	dev_t devno = MKDEV(MPU6050_MAJOR, MPU6050_MINOR);//生成唯一的设备号
	printk("match OK!\n");
 
	mpu6050 = kzalloc(sizeof(*mpu6050), GFP_KERNEL);  //向内核申请mpu6050设备的内存
	if (mpu6050 == NULL) {
		return -ENOMEM;
	}
    /*获取i2c设备结构体*/
	mpu6050->client = client;        
    /*给用户提供接口*/
	ret = register_chrdev_region(devno, 1, "mpu6050"); //设备号注册
	if (ret < 0) {
		printk("failed to register char device region!\n");
		goto err1;
	}
 
	cdev_init(&mpu6050->cdev, &mpu6050_fops);  //设备与设备的操作函数绑定
	mpu6050->cdev.owner = THIS_MODULE;
	ret = cdev_add(&mpu6050->cdev, devno, 1);  //设备注册到内核
	if (ret < 0) {
		printk("failed to add device\n");
		goto err2;
	}
	
	/*mpu6050初始化*/
	mpu6050_write_byte(client, PWR_MGMT_1, 0x00);  //电源管理，正常启动
	mpu6050_write_byte(client, SMPLRT_DIV, 0x07);  //采样频率寄存器  125HZ
	mpu6050_write_byte(client, CONFIG, 0x06);      //配置寄存器  
	mpu6050_write_byte(client, GYRO_CONFIG, 0x1B);  //陀螺仪配置，不自检，2000deg/s
	mpu6050_write_byte(client, ACCEL_CONFIG, 0x19);  //加速度配置
 
	return 0;
err2:
	unregister_chrdev_region(devno, 1);      
err1:
	kfree(mpu6050);
	return ret;
}
 
static int mpu6050_remove(struct i2c_client *client)
{
	dev_t devno = MKDEV(MPU6050_MAJOR, MPU6050_MINOR);
	cdev_del(&mpu6050->cdev);
	unregister_chrdev_region(devno, 1);
	kfree(mpu6050);
 
	return 0;
}
 /*构建i2c平台设备结构体，name,id*/
static const struct i2c_device_id mpu6050_id[] = {
	{ "mpu6050", 0},
	{}
}; 
 
static struct of_device_id mpu6050_of_match[] = {
	{.compatible = "invense,mpu6050" },
	{/*northing to be done*/},
};
 /*构建i2c平台驱动结构体*/
struct i2c_driver mpu6050_driver = {
	.driver = {
		.name 			= "mpu6050",
		.owner 			= THIS_MODULE,
		.of_match_table = of_match_ptr(mpu6050_of_match),
	},
	.probe 		= mpu6050_probe,
	.remove 	= mpu6050_remove,
	.id_table 	= mpu6050_id_table,
};
 
static init _init mpu6050_init(void)
{
	/*1将i2c驱动加入i2c总线的驱动链表
	  2.搜索i2c总线的设备链表，调用i2c总线的match
	    实现client->name与id->name进行匹配
		匹配成功就调用probe函数*/
	return i2c_add_driver(&mpu6050_driver); //注册驱动
}
 
static void _exit mpu6050_exit(void)
{
	return i2c_del_driver(&mpu6050_driver); //注销驱动
}
 
module_init(&mpu6050_init);
module_exit(&mpu6050_exit);


