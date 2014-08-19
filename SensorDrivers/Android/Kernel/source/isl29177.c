/**
 *	File 		: isl29177.c
 *	Desc 		: Sample driver to illustrate sensor features of ISL29177 
 *			  proximity sensor
 *	Ver  		: 1.0.1
 * 	Copyright 	: Intersil Inc. (2014)
 * 	License 	: GPLv2
 */

/**
 * Comment below macro to eliminate debug prints
 */
//#define ISL29177_DEBUG

//#define ISL29177_INTERRUPT_MODE

#ifdef ISL29177_DEBUG
#define DEBUG(x...)	printk(KERN_ALERT x)
#else
#define DEBUG(x...)	
#endif

#define ERR(x...)	printk(KERN_ERR x)


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/input/isl29177.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/kobject.h>


/**
 *  Data structure to hold driver runtime resources
 *  @ pdata 			- Platform data passed to the driver 
 *  @ input_dev 		- Reference to the input device registered 
 *		  	  			during the driver probe
 *  @ timer				- Reference to the high resolution timer registered
 *		  	  			in the driver probe
 *  @ prox_poll_delay 	- Timer interval of high resolution timer 
 *  @ client			- Reference to I2C slave (sensor device)  
 *  @ isl29177_kobj 	- Kernel object used as parent node for sysfs entry
 *  @ mutex				- Provides mutex based synchronization for userspace
 *			  			access to driver sysfs files
 *  @ work				- Holds the task / thread reference to be submitted to
 * 			  			the work queue 
 *  @ irq				- irq number associated with interrupt pin to CPU
 *  @ power_state		- Indicates whether sensor is enabled / disabled
 *  @ reg_cache 		- Copy of complete register set of sensor
 *  			  		device 
 */
struct isl29177_drv_data { 
	struct isl29177_pdata *pdata;	
	struct input_dev  *input_dev;
	struct hrtimer *timer;
	ktime_t prox_poll_delay;
	struct i2c_client *client;
	struct kobject *isl29177_kobj;
	struct mutex mutex;
	struct work_struct work;
	unsigned int irq;
	int16_t power_state;
	unsigned char reg_cache[0x10];
};

/* 
 * Data structure for holding runtime state machine parameters
 * @baseline		- Non-zero prox count when no object is present
 *			  		in front of the sensor device 
 * @rel_prox		- Raw prox count (directly read from sensor) - baseline
 * @raw_prox		- Proximity count read from the prox_data register of
 *			  		sensor device
 * @offset			- Offset adjust count applied to the sensor
 * @wash			- Ambient IR count 
 * @high_th			- High interrupt threshold that decides the NEAR boundary
 *			  		of the object 
 * @low_th			- Low interrupt threshold that decides the FAR boundary of
 *			  		the object
 * @obj_pos			- Indicates if the object is NEAR / FAR to the sensor
 * @light			- Indicates the ambient lighting condition as BRIGHT / DIM
 * @np				- Pulse state (High or Low) 
 * @offsetpersist   - Persistance value of high offset adjust value
 * @baselinepersist - Persistance value of a new lower proximity baseline
 *
 **/
struct isl29177_sm {
	unsigned char baseline;
	int	      rel_prox;
	int	      raw_prox;
	unsigned char offset;
	unsigned char wash;
	unsigned char high_th;
	unsigned char low_th;
	unsigned char obj_pos;
	unsigned char light;
	unsigned char np;
	unsigned char offsetpersist;
	unsigned char baselinepersist;
};

#define LUT_RANGE0_START_INDEX 0
#define LUT_RANGE1_START_INDEX 32 
#define LUT_RANGE2_START_INDEX 50
#define LUT_RANGE3_START_INDEX 73

/* Lookup table for offset adjust value */
struct lut lut_off[] = {
       /* Prox offset, range, offset adj */
	{0	,0	,0 },
	{5	,0	,1 },
	{9	,0	,2 },
	{14	,0	,3 },
	{19	,0	,4 },
	{23	,0	,5 },
	{28	,0	,6 },
	{33	,0	,7 },
	{37	,0	,8 },
	{42	,0	,9 },
	{47	,0	,10},
	{51	,0	,11},
	{56	,0	,12},
	{61	,0	,13},
	{65	,0	,14},
	{70	,0	,15},
	{75	,0	,16},
	{79	,0	,17},
	{84	,0	,18},
	{89	,0	,19},
	{93	,0	,20},
	{98	,0	,21},
	{103	,0	,22},
	{107	,0	,23},
	{112	,0	,24},
	{117	,0	,25},
	{121	,0	,26},
	{126	,0	,27},
	{131	,0	,28},
	{135	,0	,29},
	{140	,0	,30},
	{145	,0	,31},
	{151	,1	,14},
	{162	,1	,15},
	{173	,1	,16},
	{184	,1	,17},
	{195	,1	,18},
	{205	,1	,19},
	{216	,1	,20},
	{227	,1	,21},
	{238	,1	,22},
	{249	,1	,23},
	{259	,1	,24},
	{270	,1	,25},
	{281	,1	,26},
	{292	,1	,27},
	{303	,1	,28},
	{313	,1	,29},
	{324	,1	,30},
	{335	,1	,31},
	{349	,2	,9 },
	{388	,2	,10},
	{426	,2	,11},
	{465	,2	,12},
	{504	,2	,13},
	{543	,2	,14},
	{581	,2	,15},
	{620	,2	,16},
	{659	,2	,17},
	{698	,2	,18},
	{736	,2	,19},
	{775	,2	,20},
	{814	,2	,21},
	{853	,2	,22},
	{891	,2	,23},
	{930	,2	,24},
	{969	,2	,25},
	{1008	,2	,26},
	{1047	,2	,27},
	{1085	,2	,28},
	{1124	,2	,29},
	{1163	,2	,30},
	{1202	,2	,31},
	{1207	,3	,13},
	{1300	,3	,14},
	{1393	,3	,15},
	{1486	,3	,16},
	{1579	,3	,17},
	{1672	,3	,18},
	{1765	,3	,19},
	{1857	,3	,20},
	{1950	,3	,21},
	{2043	,3	,22},
	{2136	,3	,23},
	{2229	,3	,24},
	{2322	,3	,25},
	{2415	,3	,26},
	{2508	,3	,27},
	{2600	,3	,28},
	{2693	,3	,29},
	{2786	,3	,30},
	{2879	,3	,31}
};

/**
 * Data structure that represents register map of ISL29177
 */
struct isl29177_regmap
{
	/* DEVICE_ID_REG */
	unsigned char device_id;
	/* CONFIG0_REG */
	unsigned char irdr_curr:3;
	unsigned char shrt_det:1;
	unsigned char prox_slp:3;
	unsigned char prox_en:1;
	/* CONFIG1_REG */
	unsigned char off_adj:5;
	unsigned char high_offset:1;
	unsigned char prox_pulse:1;
	unsigned char :1;
	/* INT_CONFIG_REG */
	unsigned char intr_wash_en:1;
	unsigned char intr_shrt:1;
	unsigned char conv_done_en:1;
	unsigned char prox_flag:1;
	unsigned char prox_persist:2;
	unsigned char irdr_trim:1;
	unsigned char :1;
	/* THRESHOLD REG */
	unsigned char lt;
	unsigned char ht;
	/* STATUS_REG */
	unsigned char wash_flag:1;
	unsigned char irdr_shrt:1;
	unsigned char conv_done:1;
	unsigned char prox_intr:1;
	unsigned char pwr_fail:1;
	unsigned char :3;
	/* PROX_AMBIR_DATA */
	unsigned char prox;
	unsigned char ambir;
};

static int isl_read_field(unsigned char reg, unsigned char mask, unsigned char *val);
static int isl_write_field(unsigned char reg, unsigned char mask, unsigned char val);

static int refresh_reg_cache(void);
static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count);
static ssize_t reg_dump_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
static ssize_t reg_write(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count);
void interpret_value(const struct isl29177_regmap *regbase, unsigned int *arr);
static ssize_t show_log(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t cmd_hndlr(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);

void report_prox_count(unsigned int IR_count);
static void sensor_irq_thread(struct work_struct *work);
static enum hrtimer_restart isl29177_hrtimer_handler(struct hrtimer *timer);

static int setup_input_device(void);
static int setup_hrtimer(void);
static int setup_debugfs(void);
static void isl29177_initialize(void);

int XtalkAdj(void);
static void measBase(void);
static int getproxoffset(void);
static void setproxoffset(int);
static void runtime_sequence(void);

static int isl29177_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int isl29177_remove(struct i2c_client *client);
static int isl29177_get_mode(void);
static int isl29177_set_power_state(unsigned char reg, unsigned char mask, unsigned char mode);
static int isl29177_suspend(struct i2c_client *client, pm_message_t mesg);
static int isl29177_set_mode(int16_t power_state);
static int isl29177_resume(struct i2c_client *client);

/**
 * Supported I2C Devices
 */
static struct i2c_device_id isl_device_ids[] = {
	{ 
		"isl29177" ,		/* Device name */
		ISL29177_I2C_ADDR      /* Device address */
	},
	{ },
};

MODULE_DEVICE_TABLE(i2c, isl_device_ids);

struct isl29177_drv_data drv_data;
struct isl29177_sm rt;


/** @function: isl_read_field
 *  @desc    : read function for reading a particular bit field in a particular register
 *   	       from isl29177 sensor registers over I2C interface
 *  @args    
 *  reg	     : register to read from (0x00h to 0x0Fh)
 *  mask     : specific bit or group of continuous bits to read from
 *  val	     : out pointer to store the required value to be read
 *
 *  @return	 : 0 on success, -1 on failure
 *
 *  Example :
 *   To read bits [6:4] from register 0x00h call fn with reg = 0x00 mask = 0x70 (01110000b)
 *   To read the entire register pass the mas value as 0xFFh
 */
static int isl_read_field(unsigned char reg, unsigned char mask, unsigned char *val)
{
	unsigned char byte;
	unsigned char i;

	byte = i2c_smbus_read_byte_data(drv_data.client, reg);
	if(mask == ISL_FULL_MASK) {
		*val = byte;
	} else {
		byte &= mask;

		for(i = 0; i < 8; i++)
			if(mask & (1 << i)) break;	

		if( i > 7 ) return -1;

		*val = (byte >> i);
	}
	return 0;
}


/** @function: isl_write_field
 *  @desc    : write function for writing a particular bit field in a particular register
 *   	       to isl29177 sensor registers over I2C interface
 *  @args    
 *  reg	     : register to write to (0x00h to 0x0Fh)
 *  mask     : specific bit or group of continuous bits to write to
 *  val	     : value to be written to the register bits defined by mask
 *
 *  @return  : 0 on success, -1 on failure
 *
 *  Example :
 *   To write bits [6:4] to register 0x00h call fn with reg = 0x00 mask = 0x70 (01110000b)
 *   To write entire register use mask = 0xFFh
 */
static int isl_write_field(unsigned char reg, unsigned char mask, unsigned char val)
{
	unsigned char byte;
	unsigned char i = 0;

	if(mask == ISL_FULL_MASK) {
		i2c_smbus_write_byte_data(drv_data.client, reg, val);
	} else {
		byte = i2c_smbus_read_byte_data(drv_data.client, reg);
		byte &= (~mask);

		for(i = 0; i < 8; i++)
			if(mask & (1 << i)) break;	

		if(i > 7) return -1;

		byte |= (val << i);
		i2c_smbus_write_byte_data(drv_data.client, reg, byte);
	}
	return 0;
}

#ifdef ISL29177_INTERRUPT_MODE
/** @function: isl29177_irq_handler
 *  @desc    : Sensor interrupt handler that handles interrupt events and schedules the work queue
 *  @args     
 *  irq	     : irq number allocated for device
 *  dev_id   : handle for interrupt
 *  work     : Holds data about the thread that would be submitted to the work queue 
 *             that holds this thread
 *
 *  @return  : Status of irq handling 
 */
static irqreturn_t isl29177_irq_handler(int irq, void *dev_id)
{
	/* Schedule a thread that will handle interrupt and clear the interrupt flag*/
	schedule_work(&drv_data.work);
	return IRQ_HANDLED;
}
#endif 


/** @function: refresh_reg_cache
 *  @desc    : Function that fills the register cahce array with
 *             value read from sensor device registers  
 *  @args    : void 
 *
 *  @return  : 0 on success -EIO on failure 
 */
static int refresh_reg_cache(void)
{
	unsigned char i;
	for(i = 0; i < 0x10; i++){
		if(isl_read_field(i, ISL_FULL_MASK, &drv_data.reg_cache[i])) return -EIO;
	}
	return 0;
}

/** @function: enable_show
 *  @desc    : Function that shows the state of driver (enabled /disabled ) 
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data shown on reading the sysfs attribute file
 *
 *  @return  : length of data read on success, negative value on failure
 */
static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",drv_data.power_state);
}


/** @function: enable_store
 *  @desc    : Function that enables or disables the sensor
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data written on success, -1 on failure
 */
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int reg;

	reg = simple_strtoul(buf, NULL, 10);
	mutex_lock(&drv_data.mutex);
	if(reg == 1 ){
		isl_write_field(CONFIG0_REG, PROX_EN_MASK, 1);
		drv_data.power_state = 1;
		hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);
	}else if(reg == 0 && drv_data.power_state){
		isl_write_field(CONFIG0_REG, PROX_EN_MASK, 0);
		drv_data.power_state = 0;
		hrtimer_cancel(drv_data.timer);
		cancel_work_sync(&drv_data.work);
	}

	mutex_unlock(&drv_data.mutex);
	return strlen(buf);	
}

/** @function: reg_dump_show 
 *  @desc    : Function that shows complete register dump of sensor  
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data read on success, negative value on failure
 */
static ssize_t reg_dump_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	int i = 0;

	refresh_reg_cache();
	sprintf(buf," [REG]   : [VAL]\n");
	sprintf(buf,"%s ______________\n", buf);
	for (i = 0; i < REG_ARRAY_SIZE; i++)
		sprintf(buf, "%s [0x%02x]  : 0x%02x\n",buf,i, drv_data.reg_cache[i]);
	return strlen(buf);

}


/** @function: reg_write
 *  @desc    : Function for writing to a particular device register
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data written on success, -1 on failure
 */
static ssize_t reg_write(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	unsigned int reg, val;
	sscanf(buf, "%x %x",&reg, &val);
	if(reg > 0x0f || reg < 0 || val > 0xff || val < 0)
		return -EINVAL;
	mutex_lock(&drv_data.mutex);
	isl_write_field(reg, ISL_FULL_MASK, val);
	mutex_unlock(&drv_data.mutex);
	return strlen(buf);
}


/** @function: interpret_value
 *  @desc    : Function to interpret the binary codes as equivalent
 *             decimal value
 *  @args      
 *  regbase  : Reference to register map of sensor device
 *  arr      : Temproary array to hold decimal values
 *
 *  @return  : void
 */
void interpret_value(const struct isl29177_regmap *regbase, unsigned int *arr)
{
	switch(regbase->prox_slp){
		case 0:	arr[0] = 400; break;
		case 1:	arr[0] = 200; break;
		case 2:	arr[0] = 100; break;
		case 3:	arr[0] = 50; break;
		case 4:	case 5:	case 6:
		case 7:	arr[0] = 25; break;
	}

	switch(regbase->irdr_curr){
		case 0:	arr[1] = 3600; break;
		case 1:	arr[1] = 7100; break;
		case 2:	arr[1] = 10700; break;
		case 3:	arr[1] = 12500; break;
		case 4:	arr[1] = 14300; break;
		case 5:	arr[1] = 15000; break;
		case 6:	arr[1] = 17500; break;
		case 7:	arr[1] = 20000; break;
	}

	switch(regbase->prox_persist){
		case 0:	arr[2] = 1; break; 
		case 1:	arr[2] = 2; break; 
		case 2:	arr[2] = 4; break; 
		case 3:	arr[2] = 8; break; 
	}
}

/*
 * @function : show_log 
 *  @desc    : Displays sensor debug data 
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data shown on reading the sysfs attribute file
 *
 *  @return  : length of data read on success, negative value on failure
 */
static ssize_t show_log(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
        struct isl29177_regmap *regbase = (struct isl29177_regmap *)drv_data.reg_cache;
        unsigned int arr[3] = {0};

        mutex_lock(&drv_data.mutex);
        if(refresh_reg_cache() < 0) goto fail;
        interpret_value(regbase, arr);
        sprintf(buf, "-----------------------------------------------------------------------\n");
        sprintf(buf, "%sCONFIGURATION\n", buf);
        sprintf(buf, "%s-----------------------------------------------------------------------\n\n", buf);
        sprintf(buf, "%s%-22s  %-22s  %-22s\n", buf, "CONFIG0", "CONFIG1", "INTR CONFIG");
        sprintf(buf, "%s%-22s  %-22s  %-22s\n", buf, "-------", "-------", "-----------");
        sprintf(buf, "%s%-12s: %-5d%-3s  %-12s: %-5d%-3s  %-13s: %-5d%-3s\n", buf,
                        "PROX EN", regbase->prox_en, "", "PROX PULSE", regbase->prox_pulse, "", "IRDR TRIM", regbase->irdr_trim, "" );

        sprintf(buf, "%s%-12s: %-5d%-3s  %-12s: %-5d%-3s  %-13s: %-5d%-3s\n", buf,
                        "PROX SLEEP", arr[0], "ms", "HIGH OFFSET", regbase->high_offset, "","PROX PERSIST", arr[2], "");
        sprintf(buf, "%s%-12s: %-5d%-3s  %-12s: %-5d%-3s  %-13s: %-5d%-3s\n", buf,
                        "SHORT DET", regbase->shrt_det, "", "OFFSET ADJ", regbase->off_adj, "","PROX FLAG", regbase->prox_flag, "");
        sprintf(buf, "%s%-12s: %-5d%-3s  %-22s  %-13s: %-5d%-3s\n", buf,
                        "IRDR CURR", arr[1], " uA", "", "CONV DONE EN", regbase->conv_done_en, "");
        sprintf(buf, "%s%-47s %-12s : %-5d%-3s\n", buf, "", "INTR SHORT", regbase->intr_shrt,"");
        sprintf(buf, "%s%-47s %-12s : %-5d%-3s\n", buf, "", "INTR WASH EN", regbase->intr_wash_en,"");

        sprintf(buf, "%s-----------------------------------------------------------------------\n", buf);
        sprintf(buf, "%s%-12s %-21s %-12s\n", buf, "DATA", "", "STATUS");
        sprintf(buf, "%s-----------------------------------------------------------------------\n\n", buf);
        sprintf(buf, "%s%-12s: %-5s%-15s %-16s: %-5d%-3s\n",buf ,"STATE", rt.obj_pos ? "NEAR":"FAR", "", "POWER FAIL", regbase->pwr_fail, "");
        sprintf(buf, "%s%-12s: %-5s%-15s %-16s: %-5d%-3s\n",buf ,"LIGHT",rt.light ? "BRIGHT":"DIM", "", "PROX INTERRUPT", regbase->prox_intr, "");
        sprintf(buf, "%s%-12s  %-5s%-15s %-16s: %-5d%-3s\n",buf ,"", "", "", "CONVERSION DONE", regbase->conv_done, "");
        sprintf(buf, "%s%-12s: %-5d%-15s %-16s: %-5d%-3s\n",buf ,"REL PROX", (rt.rel_prox < 0) ? 0:rt.rel_prox, "", "IRDR SHORT", regbase->irdr_shrt, "");
        sprintf(buf, "%s%-12s: %-5d%-15s %-16s: %-5d%-3s\n",buf ,"BASE LINE", rt.baseline, "", "WASH FLAG", regbase->wash_flag, "");
        sprintf(buf, "%s%-12s: %-5d\n",buf ,"RAW PROX", rt.raw_prox);
        sprintf(buf, "%s%-12s: %-5d\n",buf ,"WASH", regbase->ambir);
        sprintf(buf, "%s%-12s: %-5d\n",buf ,"THRES HI", regbase->ht);
        sprintf(buf, "%s%-12s: %-5d\n",buf ,"THRES LOW", regbase->lt);
        sprintf(buf, "%s-----------------------------------------------------------------------\n\n", buf);

        mutex_unlock(&drv_data.mutex);
        return strlen(buf);
fail:
        mutex_unlock(&drv_data.mutex);
        return -1;
}

/*
 * @function : cmd_hndlr
 *  @desc    : Function that handles sensor configuration commands  
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data written on success, -1 on failure
 */
static ssize_t cmd_hndlr(struct kobject *kobj, struct kobj_attribute *attr, 
		const char *buf, size_t count)
{
	unsigned char cmd_type[MAX_BUFF_SIZE];
	uint32_t val;

	mutex_lock(&drv_data.mutex);	
	sscanf(buf, "%s %d",cmd_type, &val);
	if(!strcmp(cmd_type, "prox_persist") && (val >= 0 && val < 4))
		isl_write_field(INT_CONFIG_REG,0x30 , val);

	else if(!strcmp(cmd_type, "irdr_curr")  && (val >= 0 && val < 8)) 
		isl_write_field(CONFIG0_REG, 0x07, val);

	else if(!strcmp(cmd_type, "prox_slp") && (val >= 0 && val < 8))
		isl_write_field(CONFIG0_REG, 0x70, val);

	else if(!strcmp(cmd_type, "lt") && (val >= 0 && val < 256))
		isl_write_field(PROX_INT_TL_REG, 0xff, val);

	else if(!strcmp(cmd_type, "ht") && (val >= 0 && val < 256))
		isl_write_field(PROX_INT_TH_REG, 0xff, val);

	else {
		ERR( "%s:failed to write\n",__func__);
		mutex_unlock(&drv_data.mutex);
		return -EINVAL;
	}
	mutex_unlock(&drv_data.mutex);
	return strlen(buf);

}

/** @function	: report_prox_count
 *  @desc    	: Function to report proximity count to User space in the OS
 *
 *  @args
 *  prox_count 	: proximity count (0 - 255)
 *
 *  @return   	: void
 */
void report_prox_count(unsigned int prox_count)
{
	input_report_abs(drv_data.input_dev, ABS_DISTANCE, prox_count);
	input_sync(drv_data.input_dev);
}

/** @function: sensor_irq_thread
 *  @desc    : Sensor thread that handles interrupt operations scheduled by interrupt handler
 *
 *  @args
 *  work     : Holds data about the work queue that holds this thread
 *
 *  @return  : void
 */
static void sensor_irq_thread(struct work_struct *work)
{
	/* runtime sequence */
	runtime_sequence();

	/* Report prox count to Userspace */
	report_prox_count(rt.rel_prox);
}

/** @function: isl29177_hrtimer_handler
 *  @desc    : High resolution timer event handler
 *
 *  @args      
 *  timer    : structure pointer to hrtimer
 *
 *  @return  : HRTIMER_RESTART on success or HRTIMER_NORESTART on failure
 */
static enum hrtimer_restart isl29177_hrtimer_handler(struct hrtimer *timer)
{
	schedule_work(&drv_data.work);
	hrtimer_forward_now(drv_data.timer, drv_data.prox_poll_delay);	
	return HRTIMER_RESTART;
}


/** @function: setup_input_device
 *  @desc    : setup the input device subsystem and report the input data to user space
 *  @args    : void
 *
 *  @returns : 0 on success and error -1 on failure
 */
static int setup_input_device(void)
{
	drv_data.input_dev = input_allocate_device();

	if(!drv_data.input_dev) {
		ERR("Failed to allocate input device");
		return -1;
	}

	drv_data.input_dev->name = "isl29177";
	input_set_drvdata(drv_data.input_dev,&drv_data);

	/* Set event data type */
	input_set_capability(drv_data.input_dev, EV_ABS, ABS_DISTANCE);
	__set_bit(EV_ABS, drv_data.input_dev->evbit);
	input_set_abs_params(drv_data.input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	if(input_register_device(drv_data.input_dev)) {
		ERR("Failed to register input device");
		return -1;	
	}

	return 0;
}


/** @function: setup_hrtimer
 *  @desc    : High resolution timer setup and initialization for kernel
 *  @args    : void    
 *
 *  @returns : 0 on success and error -1 on failure
 */
static int setup_hrtimer(void)
{
	drv_data.timer = kzalloc(sizeof(struct hrtimer), GFP_KERNEL);
	if(drv_data.timer == NULL)
		return -1;

	hrtimer_init(drv_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	drv_data.prox_poll_delay = ns_to_ktime(100 * NSEC_PER_MSEC);
	drv_data.timer->function = isl29177_hrtimer_handler;
	hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);	
	DEBUG("%s:Successfully setup the high resolution timer",__func__);

	return 0;
}

/**  /sys/kernel/isl29177/debug - Path for the debug interface for drivers
 *  show_log : Used to display driver log for debugging
 *  cmd_hndlr : Used to process debug commands from userspace 
 */ 
static struct kobj_attribute debug_attribute =
__ATTR(debug, 0666, show_log, cmd_hndlr);  

/* sysfs object attribute for reg_mapping */
static struct kobj_attribute reg_map_attribute =
__ATTR(reg_map, 0666, reg_dump_show, reg_write);  

/* sysfs object for enabling and disabling the sensor */
static struct kobj_attribute enable_attribute =
__ATTR(enable, 0666, enable_show, enable_store);  
static struct attribute *isl29177_attrs[] = { &debug_attribute.attr ,
	&reg_map_attribute.attr,
	&enable_attribute.attr,
	NULL};

static struct attribute_group isl29177_attr_grp = { .attrs = isl29177_attrs, };


/** @function: setup_debugfs
 *  @desc    : Setup the separate kernel directory for isl29177 driver
 *  @args    : void
 *
 *  @returns : 0 on success and error number on failure
 */
static int setup_debugfs(void)
{

	drv_data.isl29177_kobj = kobject_create_and_add("isl29177", kernel_kobj);

	if(!drv_data.isl29177_kobj) 
		return SYSFS_FAIL;

	if(sysfs_create_group(drv_data.isl29177_kobj, &isl29177_attr_grp)) 
		return SYSFS_FAIL;

	return SYSFS_SUCCESS;
}


/** @function: isl29177_initialize
 *  @desc    : Sensor device initialization function for writing a particular 
 *             bit field in a particular register over I2C interface
 *  @args    : void
 *
 *  @return  : void
 */ 
static void isl29177_initialize(void)
{
	/* Reset the sensor device */
	isl_write_field(CONFIG2_REG, ISL_FULL_MASK, 0x38);
	/* Enable test mode */
	isl_write_field(CONFIG2_REG, ISL_FULL_MASK, 0x89); 			
	msleep(10);

	/* Set high offset */
	isl_write_field(CONFIG1_REG, ISL_FULL_MASK, 0x20);

	/* set persistence */
	isl_write_field(INT_CONFIG_REG, PROX_PRST_MASK, 0x2);

	/* enable interrupt & set HI and LOW Thresholds */
	isl_write_field(PROX_INT_TL_REG, ISL_FULL_MASK, PROX_LO_THRESHOLD);
	isl_write_field(PROX_INT_TH_REG, ISL_FULL_MASK, PROX_HI_THRESHOLD); 

	rt.high_th = PROX_HI_THRESHOLD;
	rt.low_th = PROX_LO_THRESHOLD;

#ifdef ISL29177_INTERRUPT_MODE
	isl_write_field(INT_CONFIG_REG, INT_PRX_EN_MASK, 0x1);
#endif

	/* disable residue reading */
	isl_write_field(TEST_MODE_4, ISL_FULL_MASK, 0);

	/* Enable OTP */
	isl_write_field(FUSE_CONTROL, ISL_FULL_MASK, 0x40);

	/* enable prox sensing */
	isl_write_field(CONFIG0_REG, ISL_FULL_MASK, 0x80 | IRDR_CURRENT | (PROX_SLEEP_MS << 4));
}

/** @function: XtalkAdj 
 *  @desc    : Function that does the cross-talk compensation 
 *  @args    : void
 *
 *  @return  : void
 */
int XtalkAdj()
{
	unsigned char pram, power, prox, i;
	unsigned int prox_offset;

	DEBUG( " --- In XtalkAdj --- \n");

	isl_read_field(STATUS_REG, 0x10, &power);
	if(power) {
		isl29177_initialize();
	}

	isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &prox);

	if(prox == 255)
	{
		/* Prox base in saturation */
		DEBUG( "XtalkAdj : Prox count in SATURATION\n");
		while (prox == 255)
		{
			pram = getproxoffset();	
			if(pram < 91)
				setproxoffset(pram + 1);
			else 
				break;	/* Cant increase offset beyond this */
			mdelay(2);	
			isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &prox);
		}

	} 

	if ( (100 < prox) && (prox < 255)) {
		/* High prox base range */
		DEBUG( "XtalkAdj : Prox count in HIGH RANGE prox = %d\n", prox);
		pram = getproxoffset();	
		prox_offset = (prox - 100) + lut_off[pram].prox_offset;

		/* Search LUT table for given prox offset */
		for( i = 0; i <= 91; i++) {
			if(prox_offset < lut_off[i].prox_offset) 
				break;
		}
		setproxoffset(i-1);
	} 

	if (prox == 0) {
		/* Offset is too high */
		DEBUG( "XtalkAdj : Prox count is too LOW\n");
		while (prox == 0)
		{
			pram = getproxoffset();
			if(pram > 0)
				setproxoffset(pram - 1);
			else 
				break; /* Cant reduce offset beyond this */

			mdelay(2);
			isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &prox);
		}

	}

	if ( (0 < prox) && (prox < 10)) {
		/* Low prox base range */
		DEBUG( "XtalkAdj : Prox count in LOW RANGE prox = %d\n", prox);
		pram = getproxoffset();	

		prox_offset = lut_off[pram].prox_offset - (10 - prox); 
		for( i = 0; i <= 91; i++) {
			if(prox_offset < lut_off[i].prox_offset) 
				break;
		}
		setproxoffset(i);
	}		

	return 0;
}

/** @function : measBase
 *  @desc     : Function to re-calculate proximity baseline for 
 *              sensor device
 *  @args     : void
 *
 *  @return   : void
 */ 
static void measBase(void)
{
	unsigned char power, prox, i;
	int sum = 0;

	/* check whether the powerfault occured */
	isl_read_field(STATUS_REG, 0x10, &power);

	if(power)
		isl29177_initialize();

	for (i = 0; i < 8; i++) {
		isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &prox);
		sum += prox;
	}

	prox = sum / 8;

	/* select the final base line */
	if(prox < rt.baseline && prox != 0)
		rt.baseline = prox;

	DEBUG("MeasBase: New baseline =  %d\n",rt.baseline);
	runtime_sequence();
}

/** @function: getproxoffset
 *  @desc    : Function to get the prox_offset, range, offset values
 *            
 *  @args    : void
 *
 *  @return  : index of matching current offset adjust settings
 */ 
static int getproxoffset(void)
{
	unsigned char offset, index_start = 0, index_end = 0, range0, range1;

	isl_read_field(CONFIG1_REG, 0x1F, &offset);
	isl_read_field(CONFIG1_REG, 0x20, &range0);
	isl_read_field(FUSE_REG, 0x04, &range1);


	switch (range1 << 1 | range0) {
		case 0: index_start = LUT_RANGE0_START_INDEX; index_end = LUT_RANGE1_START_INDEX - 1; break;
		case 1: index_start = LUT_RANGE1_START_INDEX; index_end = LUT_RANGE2_START_INDEX - 1; break;
		case 2: index_start = LUT_RANGE2_START_INDEX; index_end = LUT_RANGE3_START_INDEX - 1; break;
		case 3: index_start = LUT_RANGE3_START_INDEX; index_end = 91; break; 
	}

	for( ; offset > lut_off[index_start].offset && index_start <= index_end; index_start++) 
		DEBUG( "lut value = %d\n", lut_off[index_start].offset);

	DEBUG( "getproxoffset :  range1 = %d range0 = %d Current offset = %d \n",range1, range0, offset);

	return index_start;
}

/** @function: setproxoffset
 *  @desc    : Function to set the prox_offset, range, offset values based on
 *             LUT index supplied
 *  @args    : index value of LUT table
 *
 *  @return  : void
 */ 
static void setproxoffset( int tick)
{
	unsigned char  range0, range1, bscat;

	range0 = lut_off[tick].range & 0x1;
	range1 = (lut_off[tick].range & 0x2) >> 1;
	bscat = lut_off[tick].offset;

	DEBUG( "setproxoffset: tick = %d range1 = %d ,range0 = %d ,bscat = %d, prox_offset = %d\n",tick ,range1, range0, bscat, lut_off[tick].prox_offset);

	isl_write_field(CONFIG1_REG, 0x20, range0);
	isl_write_field(FUSE_REG, 0x04, range1);
	isl_write_field(CONFIG1_REG, 0x1F, bscat);
}

/** @function: runtime_sequence
 *  @desc    : Function that executes sensor device runtime statemachine on 
 *             every interrupt of high resolution timer 
 *            
 *  @args    : void
 *  @return  : void 
 */ 
static void runtime_sequence(void)
{
	unsigned char sts, wash, prox, power, measBaseTime = 30;
	static unsigned char nearfar, offsetreduced, primed;
	static long offsetreductioncount, baselinepersistcount, driftcounter; 
	long pram;

	/**
	 * This routine contains 3 possible state changes
	 * 1) Init : if "brown out" detected (initNeeded)
	 * 2) measBase : if prox=0 for consecutive offsetPersist cycles (offsetReductionCount)
	 * 3) xTalkAdj : if offset has been adjusted already but is still 0
	 */


	/*****************************************************************
	 *	START OF RUNTIME SEQUENCE				 *
	 *****************************************************************/
	if(primed) {
		DEBUG( " --- Start runtime_sequence ---\n");
		/* Read Proximity done status  */
		mdelay(10);
		isl_read_field(STATUS_REG, 0x04, &sts);
		DEBUG( "Subseq [0] : sts = %d\n", sts);

		isl_read_field(PROX_DATA_REG, 0xFF, &prox);
		isl_read_field(PROX_AMBIR_REG, 0xFF, &wash);

		/********************************************************************************
		 * SUBSEQ:1 READ PROX AND REMEASURE BASELINE (TIME INTERVAL BASED) IF REQUIRED	*
		 ********************************************************************************/
		/* CASE - PROX DONE */
		if(sts) {
			rt.raw_prox = prox;
			DEBUG( "Subseq [1] : Prox = %d\n", prox);

			/* Occasionally remeasure the baseline to compensate for drift */
			driftcounter++;
			if((driftcounter > (1000 * measBaseTime / 100)) && (prox < rt.low_th)) {
				primed = 0;
				DEBUG( "Subseq [1] : Drifcounter = %d\n",driftcounter);
				measBase();
			}
			/* CASE - PROX NOT DONE */
		} else {
			/* Check for power fault */
			isl_read_field(STATUS_REG, 0x10, &power);
			if(power) {
				primed = 0;
				isl29177_initialize();
				goto exit;
			} else {
				/* Exit due to power fault */
				goto exit;
			}
		}
		/*****************
		 *  END SUBSEQ:1 *
		 ****************/

		/*****************************************************************
		 * SUBSEQ:2 WASHOUT DETECTION AND TRACKING			 *
		 *****************************************************************/
		/* DEBUG - Monitor the pulse bit */
		isl_read_field(CONFIG1_REG, 0x40, &rt.np);
		DEBUG( "Subseq [2] RUNTIME: %d\n",rt.np);

		/** CASE - WASHOUT OCCURED 
		 *	guardband threshold by 1 LSB
		 */
		if(wash > 117 + 1 ) {
			prox = 0;
			nearfar = 0;
			rt.light = 1;
			DEBUG("Subseq [2] : Washout detected BRIGHT\n");

			/* CASE - NO WASHOUT OCCURED */
		} else {			

			/*****************************************************************
			 * SUBSEQ:2A ADJUSTMENT FOR HIGH OFFSET			 	 *
			 *****************************************************************/
			rt.light = 0;
			DEBUG("Subseq [2a] : Washout detected DIM\n");
			/**
			 *  Check if offset is too high, drop value by 1 , remeasure baseline
			 *  If has already reduced ones , readjust offset [PC] 
			 */
			DEBUG( "Subseq [2a] : Prox = %d\n", prox);
			if(prox == 0) {
				offsetreductioncount++;
			} else {
				offsetreductioncount = 0;
				offsetreduced = 0;
			}

			if(offsetreductioncount >= rt.offsetpersist ){
				/**
				 *  If drop by 1 didn't work , readjust offset 
				 */
				if(offsetreduced) {
					primed = 0;
					DEBUG( "Subseq [2a] : Xtalk adjusment called (No effect of offset reduction)\n");
					XtalkAdj();
				} else {
					offsetreduced = 1;
					pram = getproxoffset();
					DEBUG( "Subseq [2a] : [Current offset] pram  =  %d \n",pram);
					if(pram > 0){
						pram--;
						setproxoffset(pram);
						primed = 0;
						measBase();
					}
				}

				/****************
				 * END SUBSEQ:2A *
				 ****************/

				/****************************************************************************************
				 * SUBSEQ:2B READ PROX AND REMEASURE BASELINE (NEW LOW BASELINE BASED) IF REQUIRED	*
				 *****************************************************************************************/
			} else {
				rt.rel_prox = prox - rt.baseline;
				DEBUG( "Subseq [2b] : Rel Prox = %d ,Baseline = %d \n",rt.rel_prox,rt.baseline);
				if(rt.rel_prox < 0){
					baselinepersistcount++;
					rt.rel_prox = 0;
					DEBUG( "Subseq [2b] : Baselinepersistcount= %d ,Baselinepersist = %d \n", baselinepersistcount ,rt.baselinepersist);
					if(baselinepersistcount >= rt.baselinepersist){
						baselinepersistcount = 0;
						primed = 0;
						measBase();
					}
				} else {
					DEBUG( "Subseq [2b] : Baselinepersistcount= %d ,Baselinepersist = %d \n", baselinepersistcount, rt.baselinepersist);
					baselinepersistcount = 0;
				}

				/**
				 * Decide object postion 
				 */
				if(rt.rel_prox > rt.high_th) {
					nearfar = 1;
					rt.obj_pos = 1;
					DEBUG( "Subseq [2b] : Threshhi = %d  OBJECT NEAR\n",rt.high_th);
				} else {
					if(rt.rel_prox < rt.low_th){
						DEBUG( "Subseq [2b] : Threshlo = %d  OBJECT FAR\n",rt.low_th);
						nearfar = 0;
						rt.obj_pos = 0;
					} else {
						DEBUG( "Subseq [2b] : OBJECT in HYSTERISIS Region\n");
					}
				}

				/****************
				 * END SUBSEQ:2B *
				 ****************/
			}
		}
		/*****************
		 * END SUBSEQ:2	 *
		 *****************/


		DEBUG( " --- End runtime_sequence ---\n");
		/*********************************
		 * SKIP RUNTIME SEQUENCE	 *
		 ********************************/
	} else {

		DEBUG( " <=== Skipped runtime_sequence ===> \n");
		driftcounter = 0;
		primed = 1;
	}
exit:
	return;
}


/*
 * @function : isl29177_probe
 *  @desc    : Device detection is successful, allocate required resources for managing the sensor
 *  @args     
 *  kobj     
 *  client   : Structure to i2c client representing the device
 *  id       : device id of the sensor id
 *  @returns : Returns strlen of buf on success and -1 on failure
 */
static int isl29177_probe(struct i2c_client *client, const struct i2c_device_id *id)
{	
	struct isl29177_pdata *pdata = client->dev.platform_data; 
	unsigned char val = 0;

	if(!client || !pdata)
		return -ENODEV;

	drv_data.client = client;
	drv_data.pdata = pdata;

	/* Verify device id - Device ID Reg 00h */		
	if(isl_read_field(DEVICE_ID_REG, 0xF0, &val))
		goto end;
	if( val != 0x6 ) {
		ERR("Invalid device id"); 	
		goto end;  	
	}

	/* Initialize the sensor device */
	isl29177_initialize();
	msleep(10);
	XtalkAdj();
	isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &rt.baseline); 
	rt.offsetpersist = 4;
	rt.baselinepersist = 8;
	measBase();

#ifdef ISL29177_INTERRUPT_MODE
	if(!gpio_is_valid(pdata->gpio_irq)) {
		ERR("Invalid gpio number");
		goto end;
	} 

	if(gpio_request(pdata->gpio_irq, "isl29177-irq")) {
		ERR("Failed to request gpio %d", pdata->gpio_irq);	
		goto end;	
	}   	
	if(gpio_direction_input(pdata->gpio_irq)) {
		ERR("Failed to set direction for gpio %d", pdata->gpio_irq);
		goto fail;
	} 
#endif
	mutex_init(&drv_data.mutex);
	INIT_WORK(&drv_data.work, sensor_irq_thread);
#ifdef ISL29177_INTERRUPT_MODE
	if(request_irq((drv_data.irq=gpio_to_irq(pdata->gpio_irq)), /*(irq_handler_t)*/isl29177_irq_handler,
				IRQF_TRIGGER_FALLING, "isl29177",&drv_data)) {
		ERR("Failed to request irq for gpio %d", pdata->gpio_irq);
		goto gpio_fail;
	}
#endif
	/* Setup the debug interface for driver */
	if(setup_debugfs()){
		ERR("%s:setup debugfs failed\n",__func__);	
		goto sysfs_err;
	}

	/* Setup the input device infrastructure for user input subsystem */
	if(setup_input_device()) 
		goto err_input_register_device;

	/* High resolution timer setup for user data polling */
	if(setup_hrtimer()) 
		goto err_irq_fail;

	DEBUG("%s:Sensor probe successful\n",__func__);
	
    return 0;

err_irq_fail:
	input_unregister_device(drv_data.input_dev);
err_input_register_device:
	sysfs_remove_group(drv_data.isl29177_kobj,&isl29177_attr_grp);
sysfs_err:
	mutex_destroy(&drv_data.mutex);
	cancel_delayed_work((struct delayed_work*)&drv_data.work);
#ifdef ISL29177_INTERRUPT_MODE
	free_irq(drv_data.irq, NULL);
gpio_fail:
	gpio_free(pdata->gpio_irq);
#endif
end:
	return -1;
}

/** @function: isl29177_remove
 *  @desc    : Driver callback that frees all resources that are
 *  @args    : structure to i2c_client
 *  @returns : returns 0 on success
 */
static int isl29177_remove(struct i2c_client *client)
{
	hrtimer_cancel(drv_data.timer);
	cancel_delayed_work((struct delayed_work*)&drv_data.work);
	sysfs_remove_group(drv_data.isl29177_kobj,&isl29177_attr_grp);
	input_unregister_device(drv_data.input_dev);
	free_irq(gpio_to_irq(drv_data.pdata->gpio_irq),NULL);
	gpio_free(drv_data.pdata->gpio_irq);		
	input_unregister_device(drv_data.input_dev);
	mutex_destroy(&drv_data.mutex);

	return 0;
}

/** @function: isl29177_get_mode
 *  @desc    : Callback function that reads the current proximity en/dis status
 *  @args    
 *  @returns : returns mode [0/1] on success
 */
static int isl29177_get_mode(void)
{
	unsigned char mode;
	isl_read_field(CONFIG0_REG, PROX_EN_MASK, &mode);
	return mode;
}

/** @function: isl29177_set_power_state
 *  @desc    : Set the device power state depending upon the prox status 
 *  @args    
 *  reg      : register address 
 *  mask     : MASK bits
 *  mode     : enable/disable bit 
 *  @returns : returns 0 on success and error -1 on failure
 */
static int isl29177_set_power_state(unsigned char reg, unsigned char mask, unsigned char mode)
{

	if(!drv_data.power_state){ 
		hrtimer_cancel(drv_data.timer);
		cancel_work_sync(&drv_data.work);
		/* Keep a copy of all the sensor register data */
		refresh_reg_cache();
		return isl_write_field(reg, mask, mode);
	}	
	return 0;
}

#ifdef CONFIG_PM
/** @function	: isl29177_suspend
 *  @desc    	: Function that handles suspend event of the OS
 *
 *  @args
 *  i2c_client	: Structure representing the I2C device
 *  mesg	: power management event message
 *
 *  @return	: 0 on success and -1 on error
 */
static int isl29177_suspend(struct i2c_client *client, pm_message_t mesg)
{
	/*
	 * disable power only if proximity is disabled. If proximity
	 * is enabled, leave power on because proximity is allowed
	 * to wake up device.
	 */
	DEBUG("%s: Sensor suspended\n",__func__);
	drv_data.power_state = isl29177_get_mode();		
	return isl29177_set_power_state(CONFIG0_REG, ISL_FULL_MASK, ISL29177_PD_MODE);

}

/** @function: isl29177_set_mode
 *  @desc    : Callback function that writes the proximity en/dis status
 *  @args    
 *  @returns : returns mode [0/1] on success and -1 on failure
 */
static int isl29177_set_mode(int16_t power_state)
{

	/* push -1 to input subsystem to enable real value to go through next */
	input_report_abs(drv_data.input_dev, ABS_MISC, -1);
	hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);	
	return power_state?isl_write_field(CONFIG0_REG, PROX_EN_MASK, power_state):0;

}

/** @function	: isl29177_resume
 *  @desc    	: Function that handles resume event of the OS
 *
 *  @args	: struct i2c_client *client
 *  i2c_client	: Structure representing the I2C device
 *
 *  @return	: 0 on success and error -1 on failure 
 */
static int isl29177_resume(struct i2c_client *client)
{

	if(!drv_data.power_state){	
		refresh_reg_cache();
	}

	DEBUG("%s: Sensor resumed\n",__func__);
	return isl29177_set_mode(drv_data.power_state);
}
#endif

/*
 *  I2C Driver structure representing the driver operation / callbacks
 */ 
static struct i2c_driver isl29177_driver = {
	.driver   = { .name = "isl29177"},	
	.id_table = isl_device_ids,
	.probe    = isl29177_probe, 
	.remove   = isl29177_remove,
#ifdef CONFIG_PM
	.suspend  = isl29177_suspend,
	.resume   = isl29177_resume,
#endif
};

static int __init isl29177_init(void)
{
	return i2c_add_driver(&isl29177_driver);
}


static void __exit isl29177_exit(void)
{
	i2c_del_driver(&isl29177_driver);	
}

module_init(isl29177_init);
module_exit(isl29177_exit);
MODULE_DESCRIPTION("ISL29177 Sensor device driver");
MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("VVDN Technologies Pvt Ltd.");
MODULE_VERSION("1.0.1");

