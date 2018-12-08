

/*
 * version V4.3
 * compatible PS3 and XBOX mode
 * change right stick range from -1 to 1
 * fixed self-test suspend release
 * add key and joystick self-test
 * add early suspend 
 * add hot key mirco
 * change to X-input mode
 * add exchange L1/L2 and R1/R2
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/slab.h>
#include "soc.h"


#define CENTER_TRY	5
#define VERTICAL	1
#define HOT		0
#define SELFTEST	1


#define TRACKING_ID	20
#define ADC_STEP	14
//#define ADC_STEP_FACTOR	ADC_STEP * 3 / 2;
#define ADC_STEP_FACTOR	2

#define ADC_MAX		0xFF
//#define HALF_ADC_MAX	(ADC_MAX / 2)
#define HALF_ADC_MAX	0x7F
#define MID_BLIND       16
#define JS_MAGNI_TIMES	68/10

#define	X1		1
#define	X2		2
#define	X3		3
#define	X4		4
#define	X5		5
#define	VX1		6
#define	VX2		4
#define	VX3		3
#define	VX4		2
#define	VX5		1


//#define AXIS_NUM sizeof(axis)/sizeof(axis[0])
//#if XBOX
#define AXIS_NUM 	8
//#else
//#define AXIS_NUM 	6
//#endif

//default mode, 0 is PS3 mode, 1 is XBOX mode
int ioctl_xbox = 0;
//default exchange, 0 is exchange, 1 is default 
int ioctl_exchange = 1;

int cf_mode = 0;

//1, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view，view_x, view_y, leftx, lefty, rightx, righty, upx, upy, downx, downy l3x, l3y, r3x, r3y, selectx, selecty, startx, starty
//2, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view_x，view_y, view_r, leftx, lefty, rightx, righty, upx, upy, downx, downy l3x, l3y, r3x, r3y, selectx, selecty, startx, starty
static int key_param[39];
//static int r_view_x,r_view_y;

struct joystick_axis {
	unsigned char *name;
	int code;       
	int code2;       
};

struct game_key{
	unsigned char *name;
	int ps3_code;       
	int xbox_code;       
	int value;
	int old_value;
	int flag;
	int num;   	//touch param num
	int id;		//touch point id
	int gpio;
};

struct kp {

	struct input_dev *ps3_input_joystick;
	struct input_dev *xbox_input_joystick;
	struct input_dev *cf_input_joystick;
	struct work_struct work_update;
	struct timer_list timer;
	struct work_struct work_input_mode;
	struct class *config_class;
	struct device *config_dev;
	int config_major;
	char config_name[20];
	int js_value[AXIS_NUM];
	int js_flag[AXIS_NUM];
	int key_value[AXIS_NUM];
	int key_valid[AXIS_NUM];
	u8 value[AXIS_NUM];
	int stick_flag[2];
	int view_oldx, view_oldy;
	int view_count;
	int view_restart;
	int left_center_count, right_center_count;
	int suspend_flag;
	struct joystick_axis *ps3_axis;
	struct joystick_axis *xbox_axis;
	struct game_key *gamekeys;
	int keynum;
	int xbox;
        int input_mode_old;
};
static struct kp *gp_kp=NULL;

// static int release = 1;
static int early_resume = 0;
int brake, gas;
int hatx, haty;

#if SELFTEST
/* selftest = 1 pass, can use key mapping function
 * selftest = 0 fail, cannot use key mapping function
 */
static int keytest = 0;
static int sticktest = 0;
#endif

static struct joystick_axis xbox_axis[] = {
	//name			code		code2
	{"LAS L2R",		ABS_X,		0},
	{"LAS U2D",		ABS_Y,		0},
	{"RAS L2R",		ABS_Z,		0},
	{"left trigger",	ABS_RX,		ABS_BRAKE},
	{"right trigger",	ABS_RY,		ABS_GAS},
	{"RAS U2D",		ABS_RZ,		0},
	{"DPAD L2R",		ABS_HAT0X,	0},
	{"DPAD U2D",		ABS_HAT0Y,	0},
};
/*
static struct joystick_axis xbox_axis[] = {
	//name			code		code2
	{"LAS L2R",		ABS_X,		0},
	{"LAS U2D",		ABS_Y,		0},
	{"RAS L2R",		ABS_RX,		0},
	{"left trigger",	ABS_Z,		0},
	{"right trigger",	ABS_RZ,		0},
	{"RAS U2D",		ABS_RY,		0},
	{"DPAD L2R",		ABS_HAT0X,	0},
	{"DPAD U2D",		ABS_HAT0Y,	0},
};
*/
static struct joystick_axis ps3_axis[] = {
	//name			code		code2
	{"LAS L2R",		ABS_X,		0},
	{"LAS U2D",		ABS_Y,		0},
	{"RAS L2R",		ABS_Z,		0},
	//{"left trigger",	ABS_BRAKE,	0},
	//{"right trigger",	ABS_GAS,	0},
	{"left trigger",	0x30,		0},
	{"right trigger",	0x31,		0},
	{"RAS U2D",		ABS_RZ,		0},
};

// ps3_code from Vendor_054c_Product_0268.kl
static struct game_key gamekeys[] = {
	//name		ps3_code	xbox_code	value	old_value	flag	num	id	gpio
	{"keya",	0x12e,	 	BTN_A,		0,	0,     		0, 	4,	2,	0},
	{"keyb",	0x12d,	 	BTN_B,		0,	0,     		0, 	6,	3,	0},
	{"keyx",	0x12f,	 	BTN_X,		0,	0,     		0, 	8,	4,	0},
	{"keyy",	0x12c,	 	BTN_Y,		0,	0,     		0, 	10,	5,	0},
	{"keyl",	0x12a,	 	BTN_TL,		0,	0,     		0, 	12,	6,	0},
	{"keyr",	0x12b,	 	BTN_TR,		0,	0,     		0, 	14,	7,	0},
	{"keyl2",	0x128,	 	BTN_TL2,	0,	0,     		0, 	16,	8,	0},
	{"keyr2",	0x129,	 	BTN_TR2,	0,	0,     		0, 	18,	9,	0},
	{"left",	0x127,	 	KEY_LEFT,	0,	0,     		0, 	23,	10,	0},
	{"right",	0x125,	 	KEY_RIGHT,	0,	0,     		0, 	25,	11,	0},
	{"up",		0x124,	 	KEY_UP,		0,	0,     		0, 	27,	12,	0},
	{"down",	0x126,	 	KEY_DOWN,	0,	0,     		0, 	29,	13,     0},
	{"keyl3",	0x121,	 	BTN_THUMBL,	0,	0,     		0, 	31,	14,	0},
	{"keyr3",	0x122,	 	BTN_THUMBR,	0,	0,     		0, 	33,	15,	0},
	{"start",	0x123,	 	BTN_START,	0,	0,     		0, 	35,	16,	0},
	{"select",	0x120,	 	BTN_SELECT,	0,	0,     		0, 	37,	17,	0},
	//{"hot_key",	KEY_F12,	KEY_F12	,	0,	0,     		0, 	0,	0,	0},
};

int ps3_input_device(struct kp *kp);
int xbox_input_device(struct kp *kp);
int cf_input_device(struct kp *kp);
void ps3_uninput_device(struct kp *kp);
void xbox_uninput_device(struct kp *kp);
void cf_uninput_device(struct kp *kp);

static void gpio_init(struct kp *kp, struct device_node *np)
{
	int i,ret;
	enum of_gpio_flags flags;
	
	for (i=0; i<kp->keynum; i++) {
		kp->gamekeys[i].gpio = of_get_named_gpio_flags(np, kp->gamekeys[i].name, 0, &flags);
		if (gpio_is_valid(kp->gamekeys[i].gpio)) {
			ret = gpio_request(kp->gamekeys[i].gpio, NULL);
			if (ret) {
				printk("failed to request GPIO%d for %s\n",kp->gamekeys[i].gpio, kp->gamekeys[i].name);
				continue;
			}
			gpio_direction_input(kp->gamekeys[i].gpio);
			printk("initial %s success \n", kp->gamekeys[i].name);
		} else {
			kp->gamekeys[i].gpio = 0;
			printk("initial %s is not use \n", kp->gamekeys[i].name);
		}
	}
}



//extern int select, start;
static void read_keys_value(struct kp *kp)
{
	int i;
#if SELFTEST
	int tmp = 0;
#endif

	if (kp->suspend_flag) {
		for (i=0; i<kp->keynum; i++)
			kp->gamekeys[i].value = 0;

	} else {
		for (i=0; i<kp->keynum; i++) {
			if (kp->gamekeys[i].gpio) {
				if (ioctl_exchange != 1) {
					if (i == 4) 
						kp->gamekeys[4].value = gpio_get_value(kp->gamekeys[6].gpio) == 1 ? 0 : 1;
					else if (i == 5)
						kp->gamekeys[5].value = gpio_get_value(kp->gamekeys[7].gpio) == 1 ? 0 : 1;
					else if (i == 6)
						kp->gamekeys[6].value = gpio_get_value(kp->gamekeys[4].gpio) == 1 ? 0 : 1;
					else if (i == 7)
						kp->gamekeys[7].value = gpio_get_value(kp->gamekeys[5].gpio) == 1 ? 0 : 1;
					else
						kp->gamekeys[i].value = gpio_get_value(kp->gamekeys[i].gpio) == 1 ? 0 : 1;
				} else {
					kp->gamekeys[i].value = gpio_get_value(kp->gamekeys[i].gpio) == 1 ? 0 : 1;
				}
			} else {
				kp->gamekeys[i].value = 0;
			}
		}
		//kp->gamekeys[14].value = start;
		//kp->gamekeys[15].value = select;
	}

#if SELFTEST
	for (i=0; i<kp->keynum; i++) {
		if (kp->gamekeys[i].gpio) {
			tmp += kp->gamekeys[i].value;
		}
	}
	//TODO only use extern select and start add
	//tmp += start;
	//tmp += select;
	//end
	if (!tmp) { 
		keytest = 1;
	} else {
		keytest = 0;
	}
#endif

}

static void js_report(struct kp *kp, int value, int id)
{
	struct input_dev *input_joystick;
	struct joystick_axis *axis;

	if (cf_mode)
		return;
	
	/**********************************************************/
	//PS3 and XBOX switch
	if (kp->xbox) {
		input_joystick = kp->xbox_input_joystick;
		axis = kp->xbox_axis;
	} else {
		input_joystick = kp->ps3_input_joystick;
		axis = kp->ps3_axis;
	}
	/**********************************************************/

	if (id >= AXIS_NUM) {
		printk("-------- axis is overflow --------\n");
		return ;
	}

	if (value == 0) {
		if (kp->js_flag[id]) {
			kp->js_flag[id] = 0;
			input_report_abs(input_joystick, axis[id].code, 0);
			if (axis[id].code2)
				input_report_abs(input_joystick, axis[id].code2, 0);
		}
	} else {
		kp->js_flag[id] = 1;
		input_report_abs(input_joystick, axis[id].code, value);
		if (axis[id].code2)
			input_report_abs(input_joystick, axis[id].code2, value);
	}
}


static void scan_joystick(struct kp *kp, int channel)
{
	int js_value;

	js_value = kp->value[channel];
	if (js_value >= 0) {
		if ((js_value >= HALF_ADC_MAX - MID_BLIND) && (js_value <= HALF_ADC_MAX + MID_BLIND)) {
			kp->js_value[channel] = 0;
		} else {
			if (js_value >= HALF_ADC_MAX)
				js_value = (js_value - HALF_ADC_MAX - MID_BLIND) * JS_MAGNI_TIMES;
			else
				js_value = (js_value - HALF_ADC_MAX + MID_BLIND) * JS_MAGNI_TIMES;
			if (js_value > ADC_MAX)
				js_value = ADC_MAX;
			if(js_value < -ADC_MAX)
				js_value = -ADC_MAX;
			kp->js_value[channel] = js_value;
			//printk("---------------------- %i js_value = %d -------------------\n", channel, js_value);
		}
	}
}


static void scan_left_joystick(struct kp *kp)
{
	if (kp->suspend_flag) {
		kp->value[0] = 0x7f;
		kp->value[1] = 0x7f;
	} else {
		//left_h, left_v, right_h, right_v
		soc_value(kp->value);
	}
	scan_joystick(kp, 0);
	scan_joystick(kp, 1);
}

static void scan_right_joystick(struct kp *kp)
{
	if (kp->suspend_flag) {
		kp->value[2] = 0x7f;
		kp->value[3] = 0x7f;
	} else {
		//left_h, left_v, right_h, right_v
		soc_value(kp->value);
	}
	scan_joystick(kp, 2);
	scan_joystick(kp, 3);
}
/*
static void scan_joystick_touchmapping(struct kp *kp, int channel)
{
	int js_value;

	js_value = kp->value[channel];
	if (js_value >= 0) {
		if ((js_value >= HALF_ADC_MAX - MID_BLIND) && (js_value <= HALF_ADC_MAX + MID_BLIND)) {
			kp->key_valid[channel] = 0;
		} else {
			kp->key_valid[channel] = 1;

			if (js_value >= HALF_ADC_MAX)
				js_value = (js_value - HALF_ADC_MAX - MID_BLIND) * JS_MAGNI_TIMES / 2 + HALF_ADC_MAX;
			else
				js_value = (js_value - HALF_ADC_MAX + MID_BLIND) * JS_MAGNI_TIMES / 2 + HALF_ADC_MAX;

			if (js_value > ADC_MAX)
				js_value = ADC_MAX;
			if (js_value < 0)
				js_value = 0;
		}
		kp->key_value[channel] = js_value;
	}
}

static void scan_left_joystick_touchmapping(struct kp *kp)
{

	if (kp->suspend_flag) {
		kp->value[1] = 0x7f;
		kp->value[0] = 0x7f;
	} else {
		//left_h, left_v, right_h, right_v
		soc_value(kp->value);
	}
	scan_joystick_touchmapping(kp, 1);
	scan_joystick_touchmapping(kp, 0);
#if !VERTICAL
	int tmp;
        tmp=kp->key_value[1];
        kp->key_value[1]=kp->key_value[0];
        kp->key_value[0]=tmp; 
#endif

}

static void scan_right_joystick_touchmapping(struct kp *kp)
{
	if (kp->suspend_flag) {
		kp->value[2] = 0x7f;
		kp->value[3] = 0x7f;
	} else {
		//left_h, left_v, right_h, right_v
		soc_value(kp->value);
	}
	scan_joystick_touchmapping(kp, 2);
	scan_joystick_touchmapping(kp, 3);
}
*/
static void ps3_report_joystick_key(struct kp *kp)
{
	int i;
printk("%s  %d \n",__func__,__LINE__);
	read_keys_value(kp);
	for (i = 0; i < kp->keynum; i++) {
		if(kp->gamekeys[i].value == kp->gamekeys[i].old_value) {
			if (kp->gamekeys[i].value == kp->gamekeys[i].flag) {
				if(kp->gamekeys[i].value) {
					printk("%s press\n", kp->gamekeys[i].name);
					input_report_key(kp->ps3_input_joystick, kp->gamekeys[i].ps3_code, 1);
					input_mt_sync(kp->ps3_input_joystick);
					if(i == 6)
						brake = 0xff;
					if(i == 7)
						gas = 0xff;
					kp->gamekeys[i].flag = 0;
				} else {
					printk("%s release\n", kp->gamekeys[i].name);
					input_report_key(kp->ps3_input_joystick, kp->gamekeys[i].ps3_code, 0);
					input_mt_sync(kp->ps3_input_joystick);
					if(i == 6)
						brake = 0;
					if(i == 7)
						gas = 0;
					kp->gamekeys[i].flag = 1;
				}
			}
		}
		kp->gamekeys[i].old_value = kp->gamekeys[i].value;
	}
}

static void xbox_report_joystick_key(struct kp *kp)
{
	int i;
//printk("%s  %d \n",__func__,__LINE__);
	read_keys_value(kp);
	for (i = 0; i < kp->keynum; i++) {
		if(kp->gamekeys[i].value == kp->gamekeys[i].old_value) {
			if (kp->gamekeys[i].value == kp->gamekeys[i].flag) {
				if(kp->gamekeys[i].value) {
					printk("%s press\n", kp->gamekeys[i].name);
					if(i >= 8 && i <= 11) {
						if(i == 8)
							hatx = -0xff;
						else if(i == 9)
							hatx = 0xff;
						if(i == 10)
							haty = -0xff;
						else if(i == 11)
							haty = 0xff;
					} else {
						input_report_key(kp->xbox_input_joystick, kp->gamekeys[i].xbox_code, 1);
						input_mt_sync(kp->xbox_input_joystick);
						if(i == 6)
							brake = 0xff;
						if(i == 7)
							gas = 0xff;
					}
					kp->gamekeys[i].flag = 0;
				} else {
					printk("%s release\n", kp->gamekeys[i].name);
					if(i >= 8 && i <= 11) {
						if((i == 8) || (i == 9))
							hatx = 0;
						if((i == 10) || (i == 11))
							haty = 0;
					} else {
						input_report_key(kp->xbox_input_joystick, kp->gamekeys[i].xbox_code, 0);
						input_mt_sync(kp->xbox_input_joystick);
						if(i == 6)
							brake = 0;
						if(i == 7)
							gas = 0;
					}
					kp->gamekeys[i].flag = 1;
				}
			}
		}
		kp->gamekeys[i].old_value = kp->gamekeys[i].value;
	}
}

#if HOT
/*
static void report_hot_key(struct kp *kp)
{
	int i;

	kp->gamekeys[kp->keynum].value = gpio_get_value(RK30_PIN0_PC7) == 1 ? 0 : 1;
	i = kp->keynum;
	if(kp->gamekeys[i].value == kp->gamekeys[i].old_value) {
		if (kp->gamekeys[i].value == kp->gamekeys[i].flag) {
			if(kp->gamekeys[i].value) {
				input_report_key(kp->input_joystick, kp->gamekeys[i].code, 1);
				input_sync(kp->input_joystick);
				kp->gamekeys[i].flag = 0;
				//printk("%s press\n", kp->gamekeys[i].name);
			} else {
				input_report_key(kp->input_joystick, kp->gamekeys[i].code, 0);
				input_sync(kp->input_joystick);
				kp->gamekeys[i].flag = 1;
				//printk("%s release\n", kp->gamekeys[i].name);
			}
		}
	}
	kp->gamekeys[i].old_value = kp->gamekeys[i].value;
}
*/
#endif


#if SELFTEST
static void test_stick(void)
{
	int i;
	int tmp = 0;

	//left_h, left_v, right_h, right_v
	soc_value(gp_kp->value);
	for (i=0; i<4; i++) {
		if ((gp_kp->value[i] < HALF_ADC_MAX - MID_BLIND * 2) || (gp_kp->value[i] > HALF_ADC_MAX + MID_BLIND * 2))
			tmp++;
	}

	if (!tmp)
		sticktest = 1;
	else
		sticktest = 0;
}
/*
static ssize_t selftest_sys_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;

	if (gp_kp->suspend_flag) {
		count += sprintf(buf, "%d\n", 0);
	} else {
		test_stick();
		count += sprintf(buf, "%d\n", keytest * sticktest);
		//printk("keytst = %d sticktest = %d \n", keytest, sticktest);
	}

	return count;
}
static DEVICE_ATTR(selftest, S_IRWXUGO, selftest_sys_read, NULL);
*/
#endif
/*
static ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	count += sprintf(buf, "%d\n", ioctl_xbox);
	return count;
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d", &ioctl_xbox);
	printk("--- ioctl_xbox = %d ---\n", ioctl_xbox);
	return count;
}
static DEVICE_ATTR(mode, S_IRWXUGO, mode_show, mode_store);
*/
static struct attribute *key_attr[] = {
//	&dev_attr_mode.attr,
#if SELFTEST
//	&dev_attr_selftest.attr,
#endif
	NULL
};

static struct attribute_group key_attr_group = {
	
	.attrs = key_attr,
};

static void kp_timer_sr(unsigned long data)
{
	struct kp *kp_data=(struct kp *)data;
	
	schedule_work(&(kp_data->work_update));
	mod_timer(&kp_data->timer,jiffies+msecs_to_jiffies(10));
}

static void update_work_func(struct work_struct *work)
{
	struct kp *kp = container_of(work, struct kp, work_update);
	// int i;
	//int tmp = 1;
	// long int tmpx, tmpy;

	struct input_dev *input_joystick = NULL;

	if (ioctl_xbox != kp->input_mode_old){
		if (ioctl_xbox == 1) {
			xbox_input_device(kp);

			input_joystick = kp->xbox_input_joystick;
			kp->xbox = 1;
			if (kp->input_mode_old == 0)
				ps3_uninput_device(kp);
			else if (kp->input_mode_old == 2)
				cf_uninput_device(kp);
			cf_mode = 0;
		} else if (ioctl_xbox == 0) {

			input_joystick = kp->ps3_input_joystick;
			kp->xbox = 0;
                        if (kp->input_mode_old == 1)
                                xbox_uninput_device(kp);
                        else if (kp->input_mode_old == 2)
                                cf_uninput_device(kp);
			cf_mode = 0;
		} else if (ioctl_xbox == 2) {
			cf_mode = 1;
			cf_input_device(kp);
	
			input_joystick = kp->cf_input_joystick;
			if (kp->input_mode_old == 1)
				xbox_uninput_device(kp);
			else if (kp->input_mode_old == 0)
				ps3_uninput_device(kp);
			kp->xbox = ioctl_xbox;
		}
		kp->input_mode_old = kp->xbox;
	} else {
		if (ioctl_xbox == 1) {

			input_joystick = kp->xbox_input_joystick;
			kp->xbox = ioctl_xbox;
		} else if (ioctl_xbox == 0){
		input_joystick = kp->ps3_input_joystick;
			kp->xbox = ioctl_xbox;
		}else if (ioctl_xbox == 2){
			input_joystick = kp->cf_input_joystick;
			kp->xbox = ioctl_xbox;
		}
	}


#if HOT
	//report_hot_key(kp);
#endif

	/****************************** report joystick ********************************/
	//report left stick
	if (key_param[0] == 0 || key_param[1] < 0 || key_param[2] < 0) {
		scan_left_joystick(kp);
#if VERTICAL
		js_report(kp, kp->js_value[0], 0); //left
		js_report(kp, kp->js_value[1], 1); //left
#else
		js_report(kp, kp->js_value[1], 0); //left
		js_report(kp, kp->js_value[0], 1); //left
#endif
		input_sync(input_joystick);
	}
	//report right stick
	if (key_param[0] == 0) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[3], 5); //right
		js_report(kp, kp->js_value[2], 2); //right
		input_sync(input_joystick);
	} else if (key_param[0] == 1 && (key_param[21] < 0 || key_param[22] < 0)) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[3], 5); //right
		js_report(kp, kp->js_value[2], 2); //right
		input_sync(input_joystick);
	} else if (key_param[0] == 2 && (key_param[20] < 0 || key_param[21] < 0)) {
		scan_right_joystick(kp);
		js_report(kp, kp->js_value[3], 5); //right
		js_report(kp, kp->js_value[2], 2); //right
		input_sync(input_joystick);
	}
	//report keys
	if (key_param[0] == 0) {
		if (kp->xbox == 1)
			xbox_report_joystick_key(kp);
		else if (kp->xbox == 0)
		{
			printk("ps3_report_joystick_key\n");
			ps3_report_joystick_key(kp);
		}
		input_sync(input_joystick);

		js_report(kp, brake, 3); //brake
		js_report(kp, gas, 4); //gas
		if (kp->xbox) {
			js_report(kp, hatx, 6); //hatx, only for xbox mode
			js_report(kp, haty, 7); //haty, only for xbox mode
		}
		input_sync(input_joystick);
	}
	//end
	/*******************************************************************************/

}

static int selftest_open(struct inode *inode, struct file *file)
{
	return 0;
}
#if SELFTEST
static ssize_t selftest_read(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
	char *rbuf;

	test_stick();
	rbuf = kzalloc(count, GFP_KERNEL);
	rbuf[0] = keytest * sticktest;
	if (copy_to_user(buf, rbuf, count)) {
		return -EFAULT;
	}

	return 0;
}
#endif
static int selftest_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations keypad_fops = {
	.owner = THIS_MODULE,
	.open = selftest_open,
#if SELFTEST
	.read = selftest_read,
#endif
	.release = selftest_release,
};

static int register_keypad_dev(struct kp  *kp)
{
	int ret=0;
	strcpy(kp->config_name,"selftest");
	ret=register_chrdev(0, kp->config_name, &keypad_fops);
	if (ret<=0) {
		printk("register char device error\r\n");
		return  ret ;
	}
	kp->config_major=ret;
	//printk("adc keypad major:%d\r\n",ret);
	kp->config_class=class_create(THIS_MODULE,kp->config_name);
	kp->config_dev=device_create(kp->config_class, NULL, MKDEV(kp->config_major,0), NULL, kp->config_name);

	return ret;
}

static int adc_early_suspend(struct platform_device *pdev, pm_message_t state)
{
	printk("%s\n",__func__);
	if(gp_kp==NULL)
	return 0;
	del_timer_sync(&gp_kp->timer);
	//keytouch_release(kp);
	//input_sync(kp->input_joystick);
	gp_kp->suspend_flag = 1;
	return 0;
}

static int adc_early_resume(struct platform_device *pdev)
{
	printk("%s\n",__func__);

	//if (early_resume) {
	if(gp_kp==NULL)
	{

	return 0;
	}
		setup_timer(&gp_kp->timer, kp_timer_sr, (unsigned long)gp_kp) ;
		mod_timer(&gp_kp->timer, jiffies+msecs_to_jiffies(100));
		gp_kp->suspend_flag = 0;
	early_resume=1;

	return 0;
}
#if 0
static int joystick_fb_event_notify(struct notifier_block *self, unsigned long action, void *data)
{
        struct fb_event *event = data;
        int blank_mode = *((int *)event->data);

        if (action == FB_EARLY_EVENT_BLANK) {
                switch (blank_mode) {
                        case FB_BLANK_UNBLANK:
                                break;
                        default:
                                adc_early_suspend(gp_kp);
                                break;
                }
        }
        else if (action == FB_EVENT_BLANK) {
                switch (blank_mode) {
                        case FB_BLANK_UNBLANK:
                                adc_early_resume(gp_kp);
                                break;
                        default:
                                break;
                }
        }
        return NOTIFY_OK;
}

static struct notifier_block joystick_fb_notifier = {
        .notifier_call = joystick_fb_event_notify,
};
#endif
int ps3_input_device(struct kp *kp)
{
	int i, ret;

	//register joystick
	kp->ps3_input_joystick = input_allocate_device();
	if (!kp->ps3_input_joystick) {
		printk("---------- allocate ps3_input_joystick fail ------------\n");
		kfree(kp);
		input_free_device(kp->ps3_input_joystick);
		return -ENOMEM;
	}

	for (i = 0; i < kp->keynum; i++)
		set_bit(kp->gamekeys[i].ps3_code, kp->ps3_input_joystick->keybit);
	//for hot key
	set_bit(kp->gamekeys[kp->keynum].ps3_code, kp->ps3_input_joystick->keybit);

	set_bit(EV_REP, kp->ps3_input_joystick->evbit);
	set_bit(EV_KEY, kp->ps3_input_joystick->evbit);
	set_bit(EV_ABS, kp->ps3_input_joystick->evbit);
	set_bit(EV_SYN, kp->ps3_input_joystick->evbit);

	//PS3 joystick mode
	//kp->input_joystick->name = "Sony PLAYSTATION(R)3 Controller";
	kp->ps3_input_joystick->name = "PLAYSTATION(R)3";
	kp->ps3_input_joystick->id.vendor = 0x054C;
	kp->ps3_input_joystick->id.product = 0x0268;
	input_set_abs_params(kp->ps3_input_joystick, ABS_X, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->ps3_input_joystick, ABS_Y, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->ps3_input_joystick, ABS_Z, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->ps3_input_joystick, ABS_RZ, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->ps3_input_joystick, 0x30, 0, 0xFF, 0, 0);
	input_set_abs_params(kp->ps3_input_joystick, 0x31, 0, 0xFF, 0, 0);
	kp->ps3_input_joystick->id.bustype = BUS_USB;

	kp->ps3_input_joystick->id.version = 0x001;
	kp->ps3_input_joystick->rep[REP_DELAY]=0xffffffff;
	kp->ps3_input_joystick->rep[REP_PERIOD]=0xffffffff;
	kp->ps3_input_joystick->keycodesize = sizeof(unsigned short);
	kp->ps3_input_joystick->keycodemax = 0x1ff;
	ret = input_register_device(kp->ps3_input_joystick);
	if (ret < 0) {
		printk(KERN_ERR "register ps3_input_joystick device fail\n");
		kfree(kp);
		input_free_device(kp->ps3_input_joystick);
		return -EINVAL;
	}

	return 0;
}

int cf_input_device(struct kp *kp)
{
        int  ret;   //i,

        //register joystick
        kp->cf_input_joystick = input_allocate_device();
        if (!kp->cf_input_joystick) {
                printk("---------- allocate cf_input_joystick fail ------------\n");
                kfree(kp);
                input_free_device(kp->cf_input_joystick);
                return -ENOMEM;
        }

        kp->cf_input_joystick->name = "cf_touch";
        kp->cf_input_joystick->id.vendor = 0x0001;
        kp->cf_input_joystick->id.product = 0x0001;
        kp->cf_input_joystick->id.bustype = BUS_USB;

        kp->cf_input_joystick->id.version = 0x0100;

        ret = input_register_device(kp->cf_input_joystick);
        if (ret < 0) {
                printk(KERN_ERR "register cf_input_joystick device fail\n");
                kfree(kp);
                input_free_device(kp->cf_input_joystick);
                return -EINVAL;
        }

        return 0;
}


int xbox_input_device(struct kp *kp)
{
	int i, ret;

	//register joystick
	kp->xbox_input_joystick = input_allocate_device();
	if (!kp->xbox_input_joystick) {
		printk("---------- allocate xbox_input_joystick fail ------------\n");
		kfree(kp);
		input_free_device(kp->xbox_input_joystick);
		return -ENOMEM;
	}

	for (i = 0; i < kp->keynum; i++)
		set_bit(kp->gamekeys[i].xbox_code, kp->xbox_input_joystick->keybit);
	//for hot key
	set_bit(kp->gamekeys[kp->keynum].xbox_code, kp->xbox_input_joystick->keybit);

	set_bit(EV_REP, kp->xbox_input_joystick->evbit);
	set_bit(EV_KEY, kp->xbox_input_joystick->evbit);
	set_bit(EV_ABS, kp->xbox_input_joystick->evbit);
	set_bit(EV_SYN, kp->xbox_input_joystick->evbit);

	//360 joystick mode
	kp->xbox_input_joystick->name = "XBOX";
	kp->xbox_input_joystick->id.vendor = 0x02D6;
	kp->xbox_input_joystick->id.product = 0x89E5;
	input_set_abs_params(kp->xbox_input_joystick, ABS_X, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_Y, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_Z, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RZ, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RX, 0, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RY, 0, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_BRAKE, 0, ADC_MAX, 0, 0);
        input_set_abs_params(kp->xbox_input_joystick, ABS_GAS, 0, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_HAT0X, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_HAT0Y, -ADC_MAX, ADC_MAX, 0, 0);
	kp->xbox_input_joystick->id.bustype = BUS_USB;

	kp->xbox_input_joystick->id.version = 0x001;
	kp->xbox_input_joystick->rep[REP_DELAY]=0xffffffff;
	kp->xbox_input_joystick->rep[REP_PERIOD]=0xffffffff;
	kp->xbox_input_joystick->keycodesize = sizeof(unsigned short);
	kp->xbox_input_joystick->keycodemax = 0x1ff;
	ret = input_register_device(kp->xbox_input_joystick);
	if (ret < 0) {
		printk(KERN_ERR "register xbox_input_joystick device fail\n");
		kfree(kp);
		input_free_device(kp->xbox_input_joystick);
		return -EINVAL;
	}

	return 0;
}

/*
int xbox_input_device(struct kp *kp)
{
	int i, ret;

	//register joystick
	kp->xbox_input_joystick = input_allocate_device();
	if (!kp->xbox_input_joystick) {
		printk("---------- allocate xbox_input_joystick fail ------------\n");
		kfree(kp);
		input_free_device(kp->xbox_input_joystick);
		return -ENOMEM;
	}

	for (i = 0; i < kp->keynum; i++)
		set_bit(kp->gamekeys[i].xbox_code, kp->xbox_input_joystick->keybit);
	//for hot key
	set_bit(kp->gamekeys[kp->keynum].xbox_code, kp->xbox_input_joystick->keybit);

	set_bit(EV_REP, kp->xbox_input_joystick->evbit);
	set_bit(EV_KEY, kp->xbox_input_joystick->evbit);
	set_bit(EV_ABS, kp->xbox_input_joystick->evbit);
	set_bit(EV_SYN, kp->xbox_input_joystick->evbit);

	//360 joystick mode
	kp->xbox_input_joystick->name = "Microsoft X-Box 360";
	kp->xbox_input_joystick->id.vendor = 0x045E;
	kp->xbox_input_joystick->id.product = 0x028E;
	input_set_abs_params(kp->xbox_input_joystick, ABS_X, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_Y, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_Z, 0, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RZ, 0, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RX, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_RY, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_HAT0X, -ADC_MAX, ADC_MAX, 0, 0);
	input_set_abs_params(kp->xbox_input_joystick, ABS_HAT0Y, -ADC_MAX, ADC_MAX, 0, 0);
	kp->xbox_input_joystick->id.bustype = BUS_USB;

	kp->xbox_input_joystick->id.version = 0x001;
	kp->xbox_input_joystick->rep[REP_DELAY]=0xffffffff;
	kp->xbox_input_joystick->rep[REP_PERIOD]=0xffffffff;
	kp->xbox_input_joystick->keycodesize = sizeof(unsigned short);
	kp->xbox_input_joystick->keycodemax = 0x1ff;
	ret = input_register_device(kp->xbox_input_joystick);
	if (ret < 0) {
		printk(KERN_ERR "register xbox_input_joystick device fail\n");
		kfree(kp);
		input_free_device(kp->xbox_input_joystick);
		return -EINVAL;
	}

	return 0;
}
*/

void ps3_uninput_device(struct kp *kp)
{
	input_unregister_device(kp->ps3_input_joystick);
	input_free_device(kp->ps3_input_joystick);
}

void cf_uninput_device(struct kp *kp)
{
        input_unregister_device(kp->cf_input_joystick);
        input_free_device(kp->cf_input_joystick);
}


void xbox_uninput_device(struct kp *kp)
{
	input_unregister_device(kp->xbox_input_joystick);
	input_free_device(kp->xbox_input_joystick);
}

static int adc_probe(struct platform_device *pdev)
{
	struct kp *kp;
	struct pinctrl *key_pinctrl;
	struct device_node *np = pdev->dev.of_node;
	int i, ret;
	int what;
	// s8 phys[32];
	struct device *dev = &pdev->dev;
printk("%s %d\n",__func__,__LINE__);
	kp = kzalloc(sizeof(struct kp), GFP_KERNEL);
	if (!kp) {
		kfree(kp);
		return -ENOMEM;
	}
	gp_kp=kp;

	kp->stick_flag[0] = 0;
	kp->stick_flag[1] = 0;
	kp->view_oldx = 0;
	kp->view_oldy = 0;
	kp->left_center_count = 0;
	kp->right_center_count = 0;
	kp->view_restart = 0;
	for (i=0; i<AXIS_NUM; i++) {
		kp->js_value[i] = 0;
		kp->js_flag[i] = 0;
		kp->key_value[i] = 0;
		kp->key_valid[i] = 0;
	}

printk("%s %d\n",__func__,__LINE__);
	what = sysfs_create_group(&dev->kobj, &key_attr_group);
	platform_set_drvdata(pdev, kp);
	register_keypad_dev(kp);
//	fb_register_client(&joystick_fb_notifier);
printk("%s %d\n",__func__,__LINE__);
	kp->xbox = ioctl_xbox;
        kp->input_mode_old = kp->xbox;
	kp->keynum = sizeof(gamekeys)/sizeof(gamekeys[0]);
	kp->gamekeys = gamekeys;

	kp->xbox_axis = xbox_axis;
	kp->ps3_axis = ps3_axis;
	if (ioctl_xbox) {
		ret = xbox_input_device(kp);
		if (ret != 0) {
			printk("register XBOX input device error!");
			return -EINVAL;
		}
	} else {
		ret = ps3_input_device(kp);
		if (ret != 0) {
			printk("register PS3 input device error!");
			return -EINVAL;
		}
	}

	key_pinctrl = devm_pinctrl_get(&pdev->dev);//devm_pinctrl_get(dev);
	if (IS_ERR(key_pinctrl)) {
		ret = PTR_ERR(key_pinctrl);
		printk("Cannot find kp key_pinctrl  pinctrl!\n");
		return ret;
	}
	for (i=0; i<kp->keynum; i++) {
	struct pinctrl_state *key_pinctrl_state;
	key_pinctrl_state=pinctrl_lookup_state(key_pinctrl, kp->gamekeys[i].name);
	if (IS_ERR(key_pinctrl_state)) {
		ret = PTR_ERR(key_pinctrl_state);
		dev_err(&pdev->dev, "fwq Cannot find key pinctrl key_pinctrl_state!\n");
		return ret;
	}
		pinctrl_select_state(key_pinctrl, key_pinctrl_state);
	}
	gpio_init(kp, np);
printk("%s %d\n",__func__,__LINE__);
	INIT_WORK(&(kp->work_update), update_work_func);
	setup_timer(&kp->timer, kp_timer_sr, (unsigned long)kp) ;
	mod_timer(&kp->timer, jiffies+msecs_to_jiffies(20));
printk("%s %d\n",__func__,__LINE__);
	return 0;
}

static int adc_remove(struct platform_device *pdev)
{
	struct kp *kp = platform_get_drvdata(pdev);

	input_unregister_device(kp->ps3_input_joystick);
	input_unregister_device(kp->xbox_input_joystick);
	input_unregister_device(kp->cf_input_joystick);
	input_free_device(kp->ps3_input_joystick);
	input_free_device(kp->xbox_input_joystick);
	input_free_device(kp->cf_input_joystick);

	unregister_chrdev(kp->config_major,kp->config_name);
	if(kp->config_class)
	{
		if(kp->config_dev)
			device_destroy(kp->config_class,MKDEV(kp->config_major,0));
		class_destroy(kp->config_class);
	}

	gp_kp = NULL;
	kfree(kp);

	return 0;
}


static struct of_device_id adcjs_joystick_ids[] = {
        { .compatible = "adcjs" },
};



static struct platform_driver adc_driver = {
	.probe      = adc_probe,
	.remove     = adc_remove,
	.suspend = adc_early_suspend,
	.resume = adc_early_resume,
	.driver     = {
		.name   = "mx-adcjs",
		.of_match_table = of_match_ptr(adcjs_joystick_ids),
	},
};

static int __init adc_init(void)
{
	return platform_driver_register(&adc_driver);
}

static void __exit adc_exit(void)
{
	platform_driver_unregister(&adc_driver);
}

//module_init(adc_init);
late_initcall(adc_init);
module_exit(adc_exit);

MODULE_AUTHOR("Samty");
MODULE_DESCRIPTION("ADC Joystick Driver");
MODULE_LICENSE("GPL");

