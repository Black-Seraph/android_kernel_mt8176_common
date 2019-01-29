/* 
 * drivers/input/joystick/joystick.c
 * 
 * Joystick driver. 
 *
 * Copyright (c) 2013  SOFTWIN Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>
#include "soc.h"


#define JOY_NAME	            	"sf_joystick"
/* ddl@rock-chips.com : 100kHz */
#define CONFIG_SENSOR_I2C_SPEED     	100000
#define MAX				0xff
#define POWER_IO			0
#if POWER_IO
static int joy_power_io;
#endif
struct joystick_ts_data {
	struct i2c_client *client;
	int irq;
	struct work_struct pen_event_work;
	struct workqueue_struct *ts_workqueue;
};

static int sensor_read(struct i2c_client *client, u8 reg, u8 *read_buf, int len)
{
	struct i2c_msg msg[2];

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = &reg;
	msg[0].len = 1;
//	msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;

	msg[1].addr =client->addr;
	msg[1].flags = client->flags|I2C_M_RD;
	msg[1].buf = read_buf;
	msg[1].len = len;
//	msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;

	return i2c_transfer(client->adapter, msg, 2);
}

u8 read_value[4] = {0x7f, 0x7f, 0x7f, 0x7f};
//left_h, left_v, right_h, right_v
void soc_value(u8 *value)
{
	value[0] = 0xff - read_value[0];
	value[1] = 0xff - read_value[1];
	value[2] = read_value[3];
	value[3] = 0xff - read_value[2];
	//printk(" %s : %x %x %x %x\n", __func__, value[0], value[1], value[2], value[3]);
	return;
}

static void joystick_queue_work(struct work_struct *work)
{

	struct joystick_ts_data *data = container_of(work, struct joystick_ts_data, pen_event_work);
	int ret;
	u8 read_buf[4] = {0x7f, 0x7f, 0x7f, 0x7f};

	ret = sensor_read(data->client, 0x55, read_buf, 4);	
	if (ret < 0) {
		dev_err(&data->client->dev, "joystick_read_regs fail:%d!\n",ret);
	} else {
		read_value[0] = read_buf[1];
		read_value[1] = read_buf[0];
		read_value[2] = read_buf[2];
		read_value[3] = MAX - read_buf[3];
	//	printk("%s %x %x %x %x\n",__func__, read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
	//	printk("%x %x %x %x\n", read_value[0], read_value[1], read_value[2], read_value[3]);
	}

	//enable_irq(data->irq);
	return;
}

static irqreturn_t joystick_interrupt(int irq, void *dev_id)
{
	struct joystick_ts_data *joystick_ts = dev_id;

	//disable_irq_nosync(joystick_ts->irq);
	if (!work_pending(&joystick_ts->pen_event_work)) 
		queue_work(joystick_ts->ts_workqueue, &joystick_ts->pen_event_work);

	return IRQ_HANDLED;
}

static int joystick_suspend(struct device *dev)
{
	struct joystick_ts_data *joystick_ts;
	joystick_ts = dev_get_drvdata(dev);

	printk("### %s ###\n", __func__);
	//disable_irq_nosync(joystick_ts->irq);
#if POWER_IO
        gpio_set_value(joy_power_io, 0);
#endif
	return 0;
}

static int joystick_resume(struct device *dev)
{
	struct joystick_ts_data *joystick_ts;
	joystick_ts = dev_get_drvdata(dev);

	printk("### %s ###\n", __func__);
#if POWER_IO
        gpio_set_value(joy_power_io, 1);
#endif
	//msleep(10);
	//enable_irq(joystick_ts->irq);
	return 0;
}

static int joystick_probe(struct i2c_client *client ,const struct i2c_device_id *id)
{
	struct joystick_ts_data *joystick_ts;
	struct device_node *np = client->dev.of_node;

	unsigned long irq_flags = 0;
	int err = 0;
	int ret = 0;
	int irq_pin = 0;

printk("%s %d\n",__func__,__LINE__);
//irq_pin = of_get_named_gpio(np, "irq-gpio", 0);

	irq_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
#if POWER_IO
	enum of_gpio_flags pwr_flags;
	joy_power_io = of_get_named_gpio_flags(np, "power-gpio", 0, &pwr_flags);

	ret = gpio_request(joy_power_io, NULL);
	if (ret != 0) {
		printk(" request joystick power pin fail\n");
		gpio_free(joy_power_io);
		return -ENOMEM;
	}
printk("%s %d\n",__func__,__LINE__);
	gpio_direction_output(joy_power_io, 1);
#endif
	dev_info(&client->dev, "joystick_ts_probe!\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)){
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}
printk("%s %d\n",__func__,__LINE__);
	joystick_ts = kzalloc(sizeof(*joystick_ts), GFP_KERNEL);	
	if (!joystick_ts)	{
		return -ENOMEM;
	}
printk("%s %d\n",__func__,__LINE__);
	joystick_ts->client = client;
	joystick_ts->irq = irq_pin;


	if (!joystick_ts->irq) {
		err = -ENODEV;
		dev_err(&joystick_ts->client->dev, "no IRQ?\n");
		goto exit_no_irq_fail;
	}else{
		joystick_ts->irq = gpio_to_irq(irq_pin);
	}
printk("%s %d\n",__func__,__LINE__);
	INIT_WORK(&joystick_ts->pen_event_work, joystick_queue_work);
	joystick_ts->ts_workqueue = create_singlethread_workqueue("joystick_ts");
	if (!joystick_ts->ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}
printk("%s %d\n",__func__,__LINE__);
	ret = devm_request_threaded_irq(&client->dev, joystick_ts->irq, NULL, joystick_interrupt, irq_flags | IRQF_ONESHOT, client->name, joystick_ts);
	if (ret < 0) {
		dev_err(&client->dev, "irq %d busy?\n", joystick_ts->irq);
		goto exit_irq_request_fail;
	}
printk("%s %d\n",__func__,__LINE__);
	i2c_set_clientdata(client, joystick_ts);
printk("%s %d\n",__func__,__LINE__);
	return 0;

exit_irq_request_fail:
	cancel_work_sync(&joystick_ts->pen_event_work);
	destroy_workqueue(joystick_ts->ts_workqueue);
exit_create_singlethread:
exit_no_irq_fail:
	kfree(joystick_ts);
	return err;
}


static int joystick_remove(struct i2c_client *client)
{
	struct joystick_ts_data *joystick_ts = i2c_get_clientdata(client);

	free_irq(joystick_ts->irq, joystick_ts);
	kfree(joystick_ts);
	cancel_work_sync(&joystick_ts->pen_event_work);
	destroy_workqueue(joystick_ts->ts_workqueue);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static struct i2c_device_id joystick_idtable[] = {
	{JOY_NAME, 0},
	{ }
};

static struct of_device_id sf_joystick_ids[] = {
        { .compatible = "sf,joystick" },
};

MODULE_DEVICE_TABLE(i2c, joystick_idtable);

#ifdef CONFIG_PM
static const struct dev_pm_ops joystick_pm_ops = {
        .suspend        = joystick_suspend,
        .resume         = joystick_resume,
};
#endif

static struct i2c_driver joystick_driver  = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= JOY_NAME,
		.of_match_table = of_match_ptr(sf_joystick_ids),
#ifdef CONFIG_PM
                .pm     = &joystick_pm_ops,
#endif

	},
	.id_table	= joystick_idtable,
	.probe      = joystick_probe,
	.remove		= joystick_remove,

};

static int __init joystick_ts_init(void)
{
	return i2c_add_driver(&joystick_driver);
}

static void __exit joystick_ts_exit(void)
{
	i2c_del_driver(&joystick_driver);
}

module_init(joystick_ts_init);
module_exit(joystick_ts_exit);

MODULE_DESCRIPTION("softwin joystick driver");
MODULE_AUTHOR("softwin");
MODULE_LICENSE("GPL");
