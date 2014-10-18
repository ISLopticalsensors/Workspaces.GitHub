/**
 *	File 		: isl29037.c
 *	Desc 		: Sample driver to illustrate sensor features of ISL29037 
 *			  proximity sensor
 * 	Copyright 	: Intersil Inc. (2014)
 * 	License 	: GPLv2
 */

/**
 * Comment below macro to eliminate debug prints
 */
//#define ISL29037_DEBUG

//#define ISL29037_INTERRUPT_MODE
#ifdef ISL29037_DEBUG
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
#include <linux/input/isl29037.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/kobject.h>


/**
 *  Data structure to hold driver runtime resources
 *  @ input_dev 	- Reference to the input device registered 
 *		  	  during the driver probe
 *  @ timer		- Reference to the high resolution timer registered
 *		  	  in the driver probe
 *  @ prox_poll_delay 	- Timer interval of high resolution timer 
 *  @ client		- Reference to I2C slave (sensor device)  
 *  @ isl29037_kobj 	- Kernel object used as parent node for sysfs entry
 *  @ mutex		- Provides mutex based synchronization for userspace
 *			  access to driver sysfs files
 *  @ work		- Holds the task / thread reference to be submitted to
 * 			  the work queue 
 *  @ irq		- irq number associated with interrupt pin to CPU
 *  @ power_state	- Indicates whether sensor is enabled / disabled
 *  @ reg_cache 	- Copy of complete register set of sensor
 *  			  device 
 */
struct isl29037_drv_data { 
	struct input_dev  *input_dev_prox;
	struct input_dev  *input_dev_als;
	struct hrtimer *timer;
	ktime_t prox_poll_delay;
	struct i2c_client *client;
	struct kobject *isl29037_kobj;
	struct mutex mutex;
	struct work_struct work;
	unsigned char prox_mode;
	unsigned char als_mode;
	unsigned char reg_cache[0x0F];
};

/* 
 * Data structure for holding runtime state machine parameters
 * @report_prox		- prox count (directly read from sensor)
 * @report_als		- als count (directly read from sensor)
 * @wash		- Ambient IR count 
 * @prox		- prox value in access light condition
 * @obj_pos		- Indicates if the object is NEAR / FAR to the sensor
 *
 * **/

struct isl29037_sm {
	unsigned char 	report_prox;
	unsigned int 	report_als;
	
	unsigned char 	wash;
	unsigned char	prox;
	unsigned char 	obj_pos;
};

/**
 * Data structure that represents register map of ISL29037
 */
struct isl29037_regmap
{
	/* DEVICE_ID_REG	0x00 */
	unsigned char device_id;
	/* CONFIG0_REG		0x01 */
	unsigned char irdr_curr:2;
	unsigned char prox_slp:3;
	unsigned char prox_en:1;
	unsigned char :2;
	/* CONFIG1_REG		0x02 */
	unsigned char als_range:2;
	unsigned char als_en:1;
	unsigned char prox_offset:4;
	unsigned char int_alg:1;
	/* CONFIG2_REG		0x03 */
	unsigned char als_ir_comp:5;
	unsigned char :3;
	/* INT_CONFIG_REG	0x04 */
	unsigned char als_prox_int_cfg:1;
	unsigned char als_int_perst:2;
	unsigned char als_int_flag:1;
	unsigned char pwr_fail:1;
	unsigned char prox_int_perst:2;
	unsigned char prox_int_flag:1;
	
	/* PROX THRESHOLD REG	0x05, 0x06*/
	unsigned char prox_lt;
	unsigned char prox_ht;
	/* ALS THRESHOLD REG	0x07, 0x08, 0x09 */
	unsigned char als_lt;
	unsigned char als_lht;
	unsigned char als_ht;
	/* PROX_DATA		0x0A */
	unsigned char prox_data;
	/* ALS_DATA		0x0B, 0x0C */
	unsigned char als_hb;
	unsigned char als_lb;
	/* AMBIR_DATA		0x0D */
	unsigned char prox_wash:1;
	unsigned char ambir:7;
	/* CONFIG3_REG		0x0E */
	unsigned char soft_reset;
	
};

static int isl_read_field(unsigned char reg, unsigned char mask, unsigned char *val);
static int isl_write_field(unsigned char reg, unsigned char mask, unsigned char val);

static int refresh_reg_cache(void);
static ssize_t reg_dump_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf);
static ssize_t reg_write(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count);
void interpret_value(const struct isl29037_regmap *regbase, unsigned char *arr[5]);
static ssize_t show_log(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t cmd_hndlr(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);

void report_prox_count(int IR_count);
static void sensor_thread(struct work_struct *work);
static enum hrtimer_restart isl29037_hrtimer_handler(struct hrtimer *timer);

static int setup_input_device(void);
static int setup_hrtimer(void);
static int setup_debugfs(void);
static int isl29037_initialize(void);


static int isl29037_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int isl29037_remove(struct i2c_client *client);
static int isl29037_suspend(struct i2c_client *client, pm_message_t mesg);
static int isl29037_resume(struct i2c_client *client);

/**
 * Supported I2C Devices
 */
static struct i2c_device_id isl_device_ids[] = {
	{ 
		"isl29037" ,		/* Device name */
		ISL29037_I2C_ADDR      /* Device address */
	},
	{ },
};

MODULE_DEVICE_TABLE(i2c, isl_device_ids);

struct isl29037_drv_data drv_data;
struct isl29037_sm rt;


/** @function: isl_read_field
 *  @desc    : read function for reading a particular bit field in a particular register
 *   	       from isl29037 sensor registers over I2C interface
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
 *   	       to isl29037 sensor registers over I2C interface
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


/** @function: isl_read_field16
 *  @desc    : read function for reading the 16bit field from the registers
 *   	       to isl29037 sensor registers over I2C interface
 *  @args    
 *  reg	     : register to read from (0x00h to 0x0Fh)
 *  val	     : value to be read from the register bits
 *
 *  @return  : 0 on success, -1 on failure
 *
 */
static void isl_read_field16(unsigned char reg, unsigned int *val)
{
	unsigned char low=0,high=0;
	isl_read_field(reg, 0xFF, &high);
	isl_read_field(reg+1, 0xFF, &low);
	if(reg == 0x08 || reg == 0x0B){
		*val = ((high & 0x0F) << 8) | low;
	}
	else
	{
		*val = (low << 8 ) | (high & 0xF0);
		*val = *val >> 4;
	}
}


/** @function: isl_write_field16
 *  @desc    : write function for writing the 16bit field from the registers
 *   	       to isl29037 sensor registers over I2C interface
 *  @args    
 *  reg	     : register to write to (0x00h to 0x0Fh)
 *  val	     : value to be write to the register bits
 *
 *  @return  : 0 on success, -1 on failure
 *
 */
static void isl_write_field16(unsigned char reg, unsigned int val)
{
        unsigned char low=0,high=0;
	
	if(reg == ALS_INT_TLH_REG || reg == PROX_DATA_HB)
	{
		low = val & ISL_FULL_MASK;	
		high = (val & 0xF00) >> 8;
        	isl_write_field(reg+1, ISL_FULL_MASK, low);
        	isl_write_field(reg, ISL_LSB_MASK, high);
	}
	else
	{
		low = val & 0xF;	
		high = (val & 0xFF0) >> 4;
        	isl_write_field(reg+1, ISL_MSB_MASK, low);
        	isl_write_field(reg, ISL_FULL_MASK, high);
	}
}

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
	for(i = 0; i < REG_ARRAY_SIZE; i++){
		if(isl_read_field(i, ISL_FULL_MASK, &drv_data.reg_cache[i])) return -EIO;
	}
	return 0;
}

/** @function: store_prox
 *  @desc    : Function that enables or disables the proximity mode mainly used for HAL support
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data written on success, -1 on failure
 */
static ssize_t store_prox(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	unsigned char reg;
	
	reg = simple_strtoul(buf, NULL, 10);
	mutex_lock(&drv_data.mutex);
	if(reg == 1){
		isl_write_field(CONFIG0_REG, PROX_EN_MASK, 1);
		hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);
	}
	else if(reg == 0){
		isl_write_field(CONFIG0_REG, PROX_EN_MASK, 0);
		hrtimer_cancel(drv_data.timer);
		cancel_work_sync(&drv_data.work);
	}
	else
		return -1;
	mutex_unlock(&drv_data.mutex);	
	return count;
}

/** @function: show_prox
 *  @desc    : Function that shows the state of driver (enabled /disabled ) 
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data shown on reading the sysfs attribute file
 *
 *  @return  : length of data read on success, negative value on failure
 */
static ssize_t show_prox(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{	
	unsigned char prox_en;
	isl_read_field(CONFIG0_REG, PROX_EN_MASK, &prox_en);

	sprintf(buf, "%d\n",prox_en);
	return strlen(buf);
}

/** @function: store_als
 *  @desc    : Function that enables or disables the als mode mainly used for HAL support
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data written to the sysfs attribute file
 *
 *  @return  : length of data written on success, -1 on failure
 */
static ssize_t store_als(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	unsigned char reg;
	
	reg = simple_strtoul(buf, NULL, 10);
	mutex_lock(&drv_data.mutex);
	if(reg == 1){
		isl_write_field(CONFIG1_REG, ALS_EN_MASK, 1);
		hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);
	}
	else if(reg == 0){
		isl_write_field(CONFIG1_REG, ALS_EN_MASK, 0);
		hrtimer_cancel(drv_data.timer);
		cancel_work_sync(&drv_data.work);
	}
	else
		return -1;
	mutex_unlock(&drv_data.mutex);
	return count;
}

/** @function: show_als
 *  @desc    : Function that shows the state of driver (enabled /disabled ) 
 *
 *  @args    
 *  kobj     : reference to parent kernel object
 *  attr     : reference to sysfs attribute to which this callback belongs
 *  buf      : user data shown on reading the sysfs attribute file
 *
 *  @return  : length of data read on success, negative value on failure
 */
static ssize_t show_als(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{	
	unsigned char als_en;
	isl_read_field(CONFIG1_REG, ALS_EN_MASK, &als_en);
	sprintf(buf, "%d\n",als_en);
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
 *  arr      : Temproary array to hold strings
 *
 *  @return  : void
 */
void interpret_value(const struct isl29037_regmap *regbase, unsigned char *arr[5])
{
	switch(regbase->irdr_curr){
		case 0:	arr[0] = "31.25"; break;
		case 1:	arr[0] = "62.5"; break;
		case 2:	arr[0] = "125"; break;
		case 3:	arr[0] = "250"; break;
	}
	switch(regbase->prox_slp){
		case 0:	arr[1] = "125"; break; 
		case 1:	arr[1] = "250"; break; 
		case 2:	arr[1] = "2000"; break; 
		case 3:	arr[1] = "4000"; break; 
	}
	switch(regbase->prox_slp){
		case 0:	arr[2] = "400"; break;
		case 1:	arr[2] = "100"; break;
		case 2:	arr[2] = "50"; break;
		case 3:	arr[2] = "25"; break;
		case 4:	arr[2] = "12.5"; break;
		case 5:	arr[2] = "6.25"; break;
		case 6:	arr[2] = "3.125"; break;
		case 7:	arr[2] = "0"; break;
	}
	switch(regbase->als_int_perst){
		case 0:	arr[3] = "1"; break; 
		case 1:	arr[3] = "2"; break; 
		case 2:	arr[3] = "4"; break; 
		case 3:	arr[3] = "8"; break; 
	}
	switch(regbase->prox_int_perst){
		case 0:	arr[4] = "1"; break; 
		case 1:	arr[4] = "2"; break; 
		case 2:	arr[4] = "4"; break; 
		case 3:	arr[4] = "8"; break; 
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
        struct isl29037_regmap *regbase = (struct isl29037_regmap *)drv_data.reg_cache;
        unsigned char *arr[5] = {};

        mutex_lock(&drv_data.mutex);
        if(refresh_reg_cache() < 0) goto fail;
        interpret_value(regbase, arr);
        sprintf(buf, "-----------------------------------------------------------------------\n");
        sprintf(buf, "%sConfigration Register:\n", buf);
        sprintf(buf, "%s-----------------------------------------------------------------------\n\n", buf);
        sprintf(buf, "%s%-25s%-25s%-25s\n", buf, "CONFIG0", "CONFIG1", "INTR CONFIG");
        sprintf(buf, "%s%-25s%-25s%-25s\n", buf, "-------", "-------", "-----------");
        sprintf(buf, "%s%-12s: %-5s%s    %-11s: %-5s%s    %-11s: %s\n", buf,
                        "IRDR Current", arr[0],"mA", "ALS Range", arr[1],"lux", "INT conf", (regbase->als_prox_int_cfg ? "ALS and PROX":"ALS or PROX") );

        sprintf(buf, "%s%-12s: %-5s%s    %-11s: %-10s  %-11s: %s\n", buf,
                        "PROX Sleep",arr[2] ,"ms", "ALS en", (regbase->als_en ?"Enable" :"Disable"), "ALS Perst",arr[3] );

        sprintf(buf, "%s%-12s: %-9s  %-11s: %-10d  %-11s: %d\n", buf,
                        "PROX En", (regbase->prox_en?"Enable" :"Disable"), "Prox Offset", regbase->prox_offset, "ALS Flag", regbase->als_int_flag );

        sprintf(buf, "%s%-23s  %-11s: %-10s  %-11s: %s\n", buf,
                        "","INT Algo", (regbase->int_alg ?"Hysteresis":"Window"), "Pwr Fail", (regbase->pwr_fail ?"Brown-out" :"Normal") );

        sprintf(buf, "%s%-48s  %-11s: %s\n", buf,
                        "", "PROX Perst", arr[4]);

        sprintf(buf, "%s%-48s  %-11s: %d\n", buf,
                        "", "PROX Flag", regbase->prox_int_flag);

        sprintf(buf, "%s\n-----------------------------------------------------------------------\n",buf);
        sprintf(buf, "%sData Register:\n", buf);
        sprintf(buf, "%s-----------------------------------------------------------------------\n\n", buf);
        
	sprintf(buf, "%s%-10s: %-11d  %-11s: %-10d  %-11s: %-10s\n", buf,
                        "ALS Data",( (regbase->als_hb<<8) | (regbase->als_lb) ) , "curr Prox",regbase->prox_data , "Obj Pos",(rt.obj_pos ? "NEAR":"FAR"));
	sprintf(buf, "%s%-10s: %-11d  %-11s: %-10d  %-11s: %-10d\n", buf,
"ALS LT", ( ((regbase->als_lt << 8) | (regbase->als_lht)) >> 4 ), "PROX LT",regbase->prox_lt , "Prox Wash",regbase->prox_wash);

	sprintf(buf, "%s%-10s: %-11d  %-11s: %-10d  %-11s: %-10d\n", buf,
"ALS HT", ( ((regbase->als_lht & 0xF) << 8) | (regbase->als_ht) ), "PROX HT",regbase->prox_ht , "PROX Ambir",regbase->ambir);

	sprintf(buf, "%s%-10s: %-11d  %-11s: %-10d\n", buf,"ALSIR Comp", regbase->als_ir_comp, "Prox", rt.prox);
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
	if(!strcmp(cmd_type, "als_persist") && (val >= 0 && val < 4))
		isl_write_field(INT_CONFIG_REG, 0x06, val);

	if(!strcmp(cmd_type, "prox_persist") && (val >= 0 && val < 4))
		isl_write_field(INT_CONFIG_REG, 0x60, val);

	else if(!strcmp(cmd_type, "irdr_curr")  && (val >= 0 && val < 4)) 
		isl_write_field(CONFIG0_REG, 0x03, val);

	else if(!strcmp(cmd_type, "prox_slp") && (val >= 0 && val < 8))
		isl_write_field(CONFIG0_REG, 0x1C, val);

        else if(!strcmp(cmd_type, "range") && (val >= 0 && val < 4))
                isl_write_field(CONFIG1_REG, 0x03, val);

	else if(!strcmp(cmd_type, "als_lt") && (val >= 0 && val < 4096))
		isl_write_field16(ALS_INT_TL_REG, val);

	else if(!strcmp(cmd_type, "als_ht") && (val >= 0 && val < 4096))
		isl_write_field16(ALS_INT_TLH_REG, val);

	else if(!strcmp(cmd_type, "prox_lt") && (val >= 0 && val < 256))
                isl_write_field(PROX_INT_TL_REG, 0xff, val);

        else if(!strcmp(cmd_type, "prox_ht") && (val >= 0 && val < 256))
                isl_write_field(PROX_INT_TH_REG, 0xff, val);

	else {
		ERR( "%s:failed to write\n",__func__);
		mutex_unlock(&drv_data.mutex);
		return -EINVAL;
	}
	mutex_unlock(&drv_data.mutex);
	return strlen(buf);

}


/** @function	: report_als_count
 *  @desc    	: Function to report proximity count to User space in the OS
 *
 *  @args
 *  prox_count 	: als count (0 - 4096)
 *
 *  @return   	: void
 */

void report_als_count(unsigned int als_count)
{
	input_report_abs(drv_data.input_dev_als, ABS_MISC, als_count);
	input_sync(drv_data.input_dev_als);
}


/** @function	: report_prox_count
 *  @desc    	: Function to report proximity count to User space in the OS
 *
 *  @args
 *  prox_count 	: proximity count (0 - 255)
 *
 *  @return   	: void
 */

void report_prox_count(int prox_count)
{
	input_report_abs(drv_data.input_dev_prox, ABS_DISTANCE, prox_count);
	input_sync(drv_data.input_dev_prox);
}

/** @function: sensor_thread
 *  @desc    : Sensor thread that handles interrupt operations scheduled by interrupt handler
 *
 *  @args
 *  work     : Holds data about the work queue that holds this thread
 *
 *  @return  : void
 */

static void sensor_thread(struct work_struct *work)
{
	unsigned char wash, prox;
		isl_read_field(PROX_AMBIR_REG, PROX_AMBIR_MASK, &wash);
		isl_read_field(PROX_DATA_REG, ISL_FULL_MASK, &prox);

		if(wash < rt.wash) {
		rt.obj_pos = 1;			//1:near
		rt.wash = wash;
		rt.prox = prox;
		}

		if(wash > rt.wash+1){
		rt.obj_pos = 0;			//0:far
		rt.prox = 0;
		}

	/* Report prox count to Userspace */
	isl_read_field16(PROX_DATA_HB,&rt.report_als);
	isl_read_field(PROX_DATA_REG,ISL_FULL_MASK,&rt.report_prox);
	report_prox_count(rt.report_prox);
	report_als_count(rt.report_als);
}

/** @function: isl29037_hrtimer_handler
 *  @desc    : High resolution timer event handler
 *
 *  @args      
 *  timer    : structure pointer to hrtimer
 *
 *  @return  : HRTIMER_RESTART on success or HRTIMER_NORESTART on failure
 */
static enum hrtimer_restart isl29037_hrtimer_handler(struct hrtimer *timer)
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
	drv_data.input_dev_prox = input_allocate_device();
	drv_data.input_dev_als = input_allocate_device();

	if(!drv_data.input_dev_als) {
		ERR("Failed to allocate ALS input device");
		return -1;
	}
	if(!drv_data.input_dev_prox) {
		ERR("Failed to allocate PROX input device");
		return -1;
	}

	drv_data.input_dev_prox->name = "isl29037_PROX";
	drv_data.input_dev_als->name = "isl29037_ALS";

	input_set_drvdata(drv_data.input_dev_prox,&drv_data);
	input_set_drvdata(drv_data.input_dev_als,&drv_data);

	/* Set event data type */
	input_set_capability(drv_data.input_dev_prox, EV_ABS, ABS_DISTANCE);
	input_set_capability(drv_data.input_dev_als, EV_ABS, ABS_MISC);
	__set_bit(EV_ABS, drv_data.input_dev_prox->evbit);
	__set_bit(EV_ABS, drv_data.input_dev_als->evbit);
	input_set_abs_params(drv_data.input_dev_prox, ABS_DISTANCE, 0, 1, 0, 0);
	input_set_abs_params(drv_data.input_dev_als, ABS_MISC, 0, 16000, 0, 0);

	if(input_register_device(drv_data.input_dev_als)) {
		ERR("Failed to register input device");
		return -1;	
	}
	if(input_register_device(drv_data.input_dev_prox)) {
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
	drv_data.timer->function = isl29037_hrtimer_handler;
	hrtimer_start(drv_data.timer, drv_data.prox_poll_delay, HRTIMER_MODE_REL);	
	DEBUG("%s:Successfully setup the high resolution timer",__func__);

	return 0;
}

/**  /sys/kernel/isl29037/debug - Path for the debug interface for drivers
 *  show_log : Used to display driver log for debugging
 *  cmd_hndlr : Used to process debug commands from userspace 
 */ 
static struct kobj_attribute debug_attribute =
__ATTR(debug, 0666, show_log, cmd_hndlr);  

/* sysfs object attribute for reg_mapping */
static struct kobj_attribute reg_map_attribute =
__ATTR(reg_map, 0666, reg_dump_show, reg_write);  

/* sysfs object for enabling and desabling the proximity mode*/
static struct kobj_attribute enable_prox_attribute =
__ATTR(enable_prox, 0666, show_prox, store_prox);  

/* sysfs object for enabling and desabling the als mode*/
static struct kobj_attribute enable_als_attribute =
__ATTR(enable_als, 0666, show_als, store_als);

static struct attribute *isl29037_attrs[] = { &debug_attribute.attr ,
	&reg_map_attribute.attr,
	&enable_prox_attribute.attr,
	&enable_als_attribute.attr,
	NULL};

static struct attribute_group isl29037_attr_grp = { .attrs = isl29037_attrs, };


/** @function: setup_debugfs
 *  @desc    : Setup the separate kernel directory for isl29037 driver
 *  @args    : void
 *
 *  @returns : 0 on success and error number on failure
 */
static int setup_debugfs(void)
{

	drv_data.isl29037_kobj = kobject_create_and_add("isl29037", kernel_kobj);

	if(!drv_data.isl29037_kobj) 
		return SYSFS_FAIL;

	if(sysfs_create_group(drv_data.isl29037_kobj, &isl29037_attr_grp)) 
		return SYSFS_FAIL;

	return SYSFS_SUCCESS;
}


/** @function: isl29037_initialize
 *  @desc    : Sensor device initialization function for writing a particular 
 *             bit field in a particular register over I2C interface
 *  @args    : void
 *
 *  @return  : 0 on success and error -1 on failure
 */ 
static int isl29037_initialize(void)
{
	/* Reset the sensor device */
        isl_write_field(CONFIG3_REG, ISL_FULL_MASK, 0x38);

	/* Reset the config2 register */
        isl_write_field(CONFIG2_REG, ISL_FULL_MASK, 0x00);

	/* set the configuration register 
	 IRDR curr: 31.25 mA, Prox sleep: 400 ms, Prox: disable*/
	isl_write_field(CONFIG0_REG, ISL_FULL_MASK, 0x00 | IRDR_CURR_31250uA | PROX_SLEEP_400ms);

	/* config1 range: 4000 Lux, Als: enable */ 	
        isl_write_field(CONFIG1_REG, ISL_FULL_MASK, 0x04 | ALS_RANGE_4000lux);
	
        isl_write_field(CONFIG2_REG, ISL_FULL_MASK, 0x00);

	/* threshold value for ALS register*/
        isl_write_field16(ALS_INT_TL_REG, ALS_LO_THRESHOLD);
        isl_write_field16(ALS_INT_TLH_REG, ALS_HI_THRESHOLD);

	/* Threshold value for PROX register */
        isl_write_field(INT_CONFIG_REG, ISL_FULL_MASK, 0x00);
        isl_write_field(CONFIG3_REG, ISL_FULL_MASK, 0x00);
	
	/* default AMBIR wash value */	
	rt.wash = 25;
	return 0;
}


/*
 * @function : isl29037_probe
 *  @desc    : Device detection is successful, allocate required resources for managing the sensor
 *  @args     
 *  kobj     
 *  client   : Structure to i2c client representing the device
 *  id       : device id of the sensor id
 *  @returns : Returns strlen of buf on success and -1 on failure
 */
static int isl29037_probe(struct i2c_client *client, const struct i2c_device_id *id)
{	
	unsigned char val = 0;

	drv_data.client = client;

	/* Verify device id - Device ID Reg 00h */		
	if(isl_read_field(DEVICE_ID_REG, 0xF0, &val))
		goto end;
	printk("Device ID :::: %x \n",val);
	if( val != 0xC ) {
		ERR("Invalid device id"); 	
		goto end;  	
	}

	/* Initialize the sensor device */
	if(isl29037_initialize() < 0){
		ERR("%s:Failed to initialize the device parameter\n",__func__);
		goto sysfs_err;
	}
	msleep(10);
	mutex_init(&drv_data.mutex);

	INIT_WORK(&drv_data.work, sensor_thread);
	
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
	input_unregister_device(drv_data.input_dev_prox);
err_input_register_device:
	sysfs_remove_group(drv_data.isl29037_kobj,&isl29037_attr_grp);
sysfs_err:
	mutex_destroy(&drv_data.mutex);
	cancel_delayed_work((struct delayed_work*)&drv_data.work);
end:
	return -1;
}

/** @function: isl29037_remove
 *  @desc    : Driver callback that frees all resources that are
 *  @args    : structure to i2c_client
 *  @returns : returns 0 on success
 */
static int isl29037_remove(struct i2c_client *client)
{
	hrtimer_cancel(drv_data.timer);
	cancel_delayed_work((struct delayed_work*)&drv_data.work);
	sysfs_remove_group(drv_data.isl29037_kobj,&isl29037_attr_grp);
	input_unregister_device(drv_data.input_dev_prox);
	input_unregister_device(drv_data.input_dev_prox);
	mutex_destroy(&drv_data.mutex);

	return 0;
}

/** @function	: isl29037_suspend
 *  @desc    	: Function that handles suspend event of the OS
 *
 *  @args
 *  i2c_client	: Structure representing the I2C device
 *  mesg	: power management event message
 *
 *  @return	: 0 on success and -1 on error
 */
static int isl29037_suspend(struct i2c_client *client, pm_message_t mesg)
{
	unsigned char als_mod, prox_mod;
	DEBUG("%s: Sensor suspended\n",__func__);
	/* Keep a copy of all the sensor register data */
	refresh_reg_cache();
	isl_read_field(CONFIG1_REG, ISL_FULL_MASK, &drv_data.als_mode);
	isl_read_field(CONFIG0_REG, ISL_FULL_MASK, &drv_data.prox_mode);

	als_mod = (ALS_EN_MASK & drv_data.als_mode) >> 2;
	prox_mod = (PROX_EN_MASK & drv_data.prox_mode) >> 5;

	if((als_mod == 1) || (prox_mod == 1)){	
		isl_write_field(CONFIG1_REG, ALS_EN_MASK, ISL29037_PD_MODE);
		isl_write_field(CONFIG0_REG, PROX_EN_MASK, ISL29037_PD_MODE);
	}
	return 0;	
}

/** @function	: isl29037_resume
 *  @desc    	: Function that handles resume event of the OS
 *
 *  @args	: struct i2c_client *client
 *  i2c_client	: Structure representing the I2C device
 *
 *  @return	: 0 on success and error -1 on failure 
 */
static int isl29037_resume(struct i2c_client *client)
{
	refresh_reg_cache();
	DEBUG("%s: Sensor resumed\n",__func__);
	isl_write_field(CONFIG1_REG, ISL_FULL_MASK, drv_data.als_mode);
	isl_write_field(CONFIG0_REG, ISL_FULL_MASK, drv_data.prox_mode);

	return 0;
}

/*
 *  I2C Driver structure representing the driver operation / callbacks
 */ 
static struct i2c_driver isl29037_driver = {
	.driver   = { .name = "isl29037"},	
	.id_table = isl_device_ids,
	.probe    = isl29037_probe, 
	.remove   = isl29037_remove,
#ifdef CONFIG_PM
	.suspend  = isl29037_suspend,
	.resume   = isl29037_resume,
#endif
};

static int __init isl29037_init(void)
{
	return i2c_add_driver(&isl29037_driver);
}


static void __exit isl29037_exit(void)
{
	i2c_del_driver(&isl29037_driver);	
}

module_init(isl29037_init);
module_exit(isl29037_exit);
MODULE_DESCRIPTION("ISL29037 Sensor device driver");
MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("VVDN Technologies Pvt Ltd.");
