/*
 *	File            : isl29023.c
 *	Desc            : Sample driver to illustrate sensor features of ISL29023 
 *			  ambient light sensor
 *	Version		: 1.0
 *	Copyright       : Intersil Inc. (2014)
 *	License         : GPLv2
 */

/**
 * Comment below macro to eliminate debug prints
 */
#define ISL29023_DEBUG

//#define ISL29023_INTERRUPT_MODE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/input/isl29023.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/kobject.h>


#define ISL29023_DEBUG

#ifdef ISL29023_DEBUG
#define DEBUG(x...)     printk(KERN_ALERT x)
#else
#define DEBUG(x...)
#endif

#define ERR(x...)       printk(KERN_ERR x)

/**
 *  Data structure to hold driver runtime resources
 *  @ pdata             - Platform data passed to the driver
 *  @ input_dev         - Reference to the input device registered
 *                        during the driver probe
 *  @ timer             - Reference to the high resolution timer registered
 *                        in the driver probe
 *  @ als_poll_delay   - Timer interval of high resolution timer
 *  @ client            - Reference to I2C slave (sensor device) 
 *  @ isl29023_kobj     - Kernel object used as parent node for sysfs entry
 *  @ mutex             - Provides mutex based synchronization for userspace
 *                        access to driver sysfs files
 *  @ work              - Holds the task / thread reference to be submitted to
 *                        the work queue
 *  @ irq               - irq number associated with interrupt pin to CPU
 *  @ power_state       - Indicates whether sensor is enabled / disabled
 *  @ reg_cache         - Copy of complete register set of sensor
 *                        device
 */
struct isl29023_drv_data {
	struct isl29023_pdata *pdata;
	struct input_dev *input_dev;
	struct hrtimer *timer;
	ktime_t als_poll_delay;
	struct i2c_client *client;
	struct kobject *isl29023_kobj;
	struct mutex mutex;
	struct work_struct work;
	unsigned int irq;
	int16_t power_state;
	unsigned char reg_cache[REG_ARRAY_SIZE];
}drv_data;

/**
 * Data structure that represents register map of ISL29023
 */
struct isl29023_regmap
{
	/* COMMAND1_REG */
	unsigned char	persist:2;
	unsigned char	intr_flag:1;
	unsigned char	:2;
	unsigned char	mode :3;
	/* COMMAND2_REG */
	unsigned char	range:2;
	unsigned char	resolution:2;
	unsigned char	:4;	
	/* DATA_LSB  */
	unsigned char 	data_lsb;	
	/* DATA_MSB  */
	unsigned char	data_msb;
	/* INTR_LT_LSB */ 
	unsigned char	int_lt_lsb;
	/* INTR_LT_MSB */
	unsigned char	int_lt_msb;
	/* INTR_HT_LSB */
	unsigned char	int_ht_lsb;
	/* INT_HT_MSB */
	unsigned char	int_ht_msb;
};

/* function prototype */
static int isl_read_field(unsigned char reg, unsigned char mask, unsigned char *val);
static int isl_read_field16(unsigned char reg, unsigned int *val);
static int isl_write_field(unsigned char reg, unsigned char mask, unsigned char val);
static int isl_write_field16(unsigned char reg, unsigned int val);

static int refresh_reg_cache(void);
static ssize_t reg_dump_show(struct kobject *kobj, struct kobj_attribute *attr,                                                              
                char *buf);
static ssize_t reg_write(struct kobject *kobj, struct kobj_attribute *attr,
                const char *buf, size_t count);                       

static ssize_t show_log(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t cmd_hndlr(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);                                  
void interpret_value(const struct isl29023_regmap *regbase, unsigned int *arr, unsigned char *str);        

void report_lux_value(unsigned int lux_value);                        
static void sensor_irq_thread(struct work_struct *work);              
static enum hrtimer_restart isl29023_hrtimer_handler(struct hrtimer *timer);

static int setup_input_device(void);
static int setup_hrtimer(void);                                       
static int setup_debugfs(void);                                       
static void isl29023_initialize(void);

static int isl29023_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int isl29023_remove(struct i2c_client *client);                
static int isl29023_get_mode(void);                                   
static int isl29023_set_power_state(unsigned char reg, unsigned char mask, unsigned char mode);
static int isl29023_suspend(struct i2c_client *client, pm_message_t mesg);
static int isl29023_set_mode(int16_t power_state);
static int isl29023_resume(struct i2c_client *client);
static void autorange(void);
/**
 * Supported I2C Devices
 */
static struct i2c_device_id isl_device_ids[] = {
	{
		"isl29023" ,            /* Device name */
		ISL29023_I2C_ADDR      /* Device address */
	},
	{ },
};

struct isl29023_drv_data drv_data;

MODULE_DEVICE_TABLE(i2c, isl_device_ids);

/** @function: isl_read_field
 *  @desc    : read function for reading a particular bit field in a particular register
 *             from isl29023 sensor registers over I2C interface
 *  @args
 *  reg      : register to read from (0x00h to 0x08h)
 *  mask     : specific bit or group of continuous bits to read from
 *  val      : out pointer to store the required value to be read
 *
 *  @return  : 0 on success, -1 on failure
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

/** @function: isl_read_field16
 *  @desc    : read function for reading a particular 16 bit field 
 *             from isl29023 sensor registers over I2C interface
 *  @args
 *  reg      : register to read from (0x03h to 0x08h)
 *  mask     : specific bit or group of continuous bits to read from
 *  val      : out pointer to store the required value to be read
 *
 *  @return  : 0 on success, -1 on failure
 */
static int32_t isl_read_field16(unsigned char reg, unsigned int *buf)
{
	unsigned char reg_h;
	unsigned char reg_l;

	isl_read_field(reg, ISL_FULL_MASK, &reg_l);
	isl_read_field(reg + 1, ISL_FULL_MASK, &reg_h);

	*buf = (reg_h << 8) | reg_l;
	return 0;
}


/** @function: isl_write_field
 *  @desc    : write function for writing a particular bit field in a particular register
 *             to isl29023 sensor registers over I2C interface
 *  @args
 *  reg      : register to write to (0x00h to 0x0Fh)
 *  mask     : specific bit or group of continuous bits to write to
 *  val      : value to be written to the register bits defined by mask
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

/** @function: isl_write_field16
 *  @desc    : write function for writing a particular 16 bit data in a particular register
 *             to isl29023 sensor registers over I2C interface
 *  @args
 *  reg      : register to write to (0x03h to 0x0Fh)
 *  mask     : specific bit or group of continuous bits to write to
 *  val      : value to be written to the register bits defined by mask
 *
 *  @return  : 0 on success, -1 on failure
 */
static int isl_write_field16(unsigned char reg, unsigned int val)
{
	unsigned char reg_l;
	unsigned char reg_h;

	reg_l =(unsigned char) (ISL_LSB_MASK & val);
	reg_h =(unsigned char) ((ISL_MSB_MASK & val) >> 8);
	isl_write_field(reg, ISL_FULL_MASK, reg_l);
	isl_write_field(reg+1, ISL_FULL_MASK, reg_h);
	DEBUG("In %s :: REG_L = %d REG_H = %d\n",__func__,reg_l,reg_h);
	return 0;
}

/** @function: autorange
 *  @desc    : autorange function to readjust the range and resolution for the isl29023 sensor driver
 *	       
 *  @args
 *  void     : 
 *
 *  @return  : 0 on success, -1 on failure
 */

static void autorange(void)
{
	unsigned int val;
	unsigned char als_range, adc_resolution;

	isl_read_field16(DATA_LSB, &val);
	isl_read_field(COMMAND2_REG, ADC_RESOLUTION_MASK, &adc_resolution);
	isl_read_field(COMMAND2_REG, ALS_RANGE_MASK, &als_range);

	switch(adc_resolution)
	{
		case 3:
			switch(als_range)
			{
				case 0:
					if(val > 0xC)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 1:
					if(val > 0xC)
						als_range = 2;
					else if(val < 0x10)
						als_range = 0;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 2:
					if(val > 0xC)
						als_range = 3;
					else if(val < 0x8)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 3:
					if(val < 0x8)
						als_range = 2;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
			}
			break;
		case 2:
			switch(als_range)
			{
				case 0:
					if(val > 0xCC)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 1:
					if(val > 0xCC)
						als_range = 2;
					else if(val < 0xC)
						als_range = 0;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 2:
					if(val > 0xCC)
						als_range = 3;
					else if(val < 0xC)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 3:
					if(val < 0xC)
						als_range = 2;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
			}
			break;
		case 1:
			switch(als_range)
			{
				case 0:
					if(val > 0xCCC)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 1:
					if(val > 0xCCC)
						als_range = 2;
					else if(val < 0xCC)
						als_range = 0;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 2:
					if(val > 0xCCC)
						als_range = 3;
					else if(val < 0xCC)
						als_range = 1;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;

				case 3:
					if(val < 0xCC)
						als_range = 2;
					else break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;

			}
			break;
		case 0:
			switch(als_range){
				case 0:
					if(val > 0xCCCC)
						als_range = 1 ;
					else  break;
					isl_write_field(COMMAND2_REG, 0x03, als_range);
					break;
				case 1:
					{
						if(val > 0xCCCC)
							als_range = 2;
						else if(val < 0xCCC)
							als_range = 0;
						else break;
						isl_write_field(COMMAND2_REG, 0x03, als_range);
						break;

					}
				case 2:
					{
						if(val > 0xCCCC)
							als_range = 3;
						else if(val < 0xCCC)
							als_range = 1;
						else break;
						isl_write_field(COMMAND2_REG, 0x03, als_range);
						break;
					}
				case 3:
					{
						if(val < 0xCCC)
							als_range = 2;
						else break;
						isl_write_field(COMMAND2_REG, 0x03, als_range);
						break;
					}
			}
	}
}
#ifdef ISL29023_INTERRUPT_MODE

/** @function: isl29023_irq_handler
 *  @desc    : Sensor interrupt handler that handles interrupt events and schedules the work queue
 *
 *  @args     
 *  irq      : irq number allocated for device
 *  dev_id   : handle for interrupt
 *  work     : Holds data about the work queue that holds this thread
 *
 *  @return  : IRQ_HANDLED
 */
static irqreturn_t isl29023_irq_handler(int irq, void *dev_id)
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
        for(i = 0; i < REG_ARRAY_SIZE; i++){
                if(isl_read_field(i, ISL_FULL_MASK, &drv_data.reg_cache[i])) return -EIO;
        }
        return 0;
}

/** @function   : report_lux_value
 *  @desc       : Function to report lux value to User space in the OS
 *
 *  @args
 *  lux_count   : lux value (0 - 65535)
 *
 *  @return     : void
 */
void report_lux_value(unsigned int lux_value)
{
        input_report_abs(drv_data.input_dev, ABS_MISC, lux_value);
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
	unsigned int lux;
        /* runtime sequence */
	isl_read_field16(DATA_LSB, &lux);
        autorange();

        /* Report rox count to Userspace */
        report_lux_value(lux);
}

/** @function: isl29023_hrtimer_handler
 *  @desc    : High resolution timer event handler
 *
 *  @args
 *  timer    : structure pointer to hrtimer
 *
 *  @return  : HRTIMER_RESTART on success or HRTIMER_NORESTART on failure
 */
static enum hrtimer_restart isl29023_hrtimer_handler(struct hrtimer *timer)
{
        schedule_work(&drv_data.work);
        hrtimer_forward_now(drv_data.timer, drv_data.als_poll_delay);
        return HRTIMER_RESTART;
}

/** @function: interpret_value
 *  @desc    : Function to interpret the binary codes as equivalent
 *             decimal value
 *  @args
 *  regbase  : Reference to register map of sensor device
 *  arr      : Temproary array to hold decimal values
 *  str      : Temproary array to hold string values
 *
 *  @return  : void
 */
void interpret_value(const struct isl29023_regmap *regbase, unsigned int *arr, unsigned char *str)
{
        switch(regbase->range){
                case 0: arr[0] = 1000; break;
                case 1: arr[0] = 4000; break;
                case 2: arr[0] = 16000; break;
                case 3: arr[0] = 64000; break;
        }

        switch(regbase->resolution){
                case 0: arr[1] = 16; break;
                case 1: arr[1] = 12; break;
                case 2: arr[1] = 8; break;
                case 3: arr[1] = 4; break;
        }

        switch(regbase->mode){
                case 0: strcpy(str, "power down"); break;
                case 1: strcpy(str, "ALS once"); break;
                case 2: strcpy(str, "IR once"); break;
                case 3:	case 4:
                case 7: strcpy(str, "reserved"); break;
                case 5: strcpy(str, "ALS continuous"); break;
                case 6: strcpy(str, "IR continuous"); break;
        }

        switch(regbase->persist){
                case 0: arr[2] = 1; break;
                case 1: arr[2] = 4; break;
                case 2: arr[2] = 8; break;
                case 3: arr[2] = 16; break;
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
	struct isl29023_regmap *regbase = (struct isl29023_regmap *)drv_data.reg_cache;
	unsigned int arr[3] = {0};
	unsigned char str[15] = {'\0'};
	
	mutex_lock(&drv_data.mutex);
	if(refresh_reg_cache() < 0) goto fail;
	interpret_value(regbase, arr, str);
	autorange();

	sprintf(buf,"-----------------------------------------------------------\n");
	sprintf(buf,"%sCONFIGURATION\n",buf);
	sprintf(buf,"%s-----------------------------------------------------------\n\n",buf);
	sprintf(buf,"%s%-32s%-25s\n",buf,"COMMAND1","COMMAND2");
	sprintf(buf,"%s%-32s%-25s\n",buf,"--------","--------");
	sprintf(buf,"%s%-12s: %-15s%-3s%-12s: %-15d\n", buf, "MODE", str, "", "RESOLUTION", arr[1]);
	sprintf(buf,"%s%-12s: %-15d%-3s%-12s: %-15d\n", buf, "FLAG", regbase->intr_flag, "", "RANGE", arr[0]);
	sprintf(buf,"%s%-12s: %-15d\n", buf, "PERSISTENCY", arr[2]);
	sprintf(buf,"%s-----------------------------------------------------------\n",buf);                                                   
	sprintf(buf,"%s%-32s%-25s\n",buf,"DATA","INTERRUPT");
	sprintf(buf,"%s-----------------------------------------------------------\n",buf);

	sprintf(buf,"%s%-12s: %-15d%-3s%-12s: %-15d\n", buf, "DATA", (regbase->data_msb << 8) | (regbase->data_lsb), "", "LOW_TH", (regbase->int_lt_msb << 8) | (regbase->int_lt_lsb));                                                  
	sprintf(buf,"%s%-32s%-12s: %-15d\n", buf, " ", "HIGH_TH", (regbase->int_ht_msb << 8) | (regbase->int_ht_lsb));
	sprintf(buf,"%s------------------------------------------------------------\n\n", buf);                                   
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
static ssize_t cmd_hndlr(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count)
{
        unsigned char cmd_type[MAX_BUFF_SIZE];
        unsigned int val;
	
        mutex_lock(&drv_data.mutex);
        sscanf(buf, "%s %d",cmd_type, &val);
        if(!strcmp(cmd_type, "persist") && (val >= 0 && val < 4))	
		isl_write_field(COMMAND1_REG, INTR_PERSISTENCY_MASK , val);

        else if (!strcmp(cmd_type, "mode")  && ((val >= 0 && val <= 2) && (val >= 5 && val < 7)) )
		isl_write_field(COMMAND1_REG, OPERATION_MODE_MASK, val);

        else if(!strcmp(cmd_type, "range") && (val >= 0 && val < 4))
                isl_write_field(COMMAND2_REG, 0x03, val);
        
	else if(!strcmp(cmd_type, "resolution") && (val >= 0 && val < 4))
		isl_write_field(COMMAND2_REG, ADC_RESOLUTION_MASK, val);

        else if(!strcmp(cmd_type, "lt") && (val >= 0 && val < 65536))
		isl_write_field16(INTR_LT_LSB, val);

        else if(!strcmp(cmd_type, "ht") && (val >= 0 && val < 65536))
		isl_write_field16(INTR_HT_LSB, val);

        else{
                DEBUG("%s:failed to write\n",__func__);
                mutex_unlock(&drv_data.mutex);
                return -EINVAL;
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

        if(reg > 0x08 || reg < 0 || val > 0xff || val < 0)
                return -EINVAL;
        mutex_lock(&drv_data.mutex);
        isl_write_field(reg, ISL_FULL_MASK, val);
        mutex_unlock(&drv_data.mutex);
        return strlen(buf);
}

/**  /sys/kernel/isl29023/debug - Path for the debug interface for drivers
 *  show_log 	: Used to display driver log for debugging
 *  cmd_hndlr 	: Used to process debug commands from userspace 
 */
static struct kobj_attribute debug_attribute =
__ATTR(debug, 0666, show_log, cmd_hndlr);

/* sysfs object attribute for reg_mapping */
static struct kobj_attribute reg_map_attribute =
__ATTR(reg_map, 0666, reg_dump_show, reg_write);

static struct attribute *isl29023_attrs[] = { &debug_attribute.attr ,
	&reg_map_attribute.attr,
	NULL};

static struct attribute_group isl29023_attr_grp = { .attrs = isl29023_attrs, };

/** @function: setup_debugfs
 *  @desc    : setup the separate kernel directory for isl29023 driver
 *  @args    : void
 *  @returns : 0 on success and error number on failure
 */

static int setup_debugfs(void)
{

	drv_data.isl29023_kobj = kobject_create_and_add("isl29023", kernel_kobj);

	if(!drv_data.isl29023_kobj)
		return SYSFS_FAIL;
        if(sysfs_create_group(drv_data.isl29023_kobj, &isl29023_attr_grp))
                return SYSFS_FAIL;

	return SYSFS_SUCCESS;
}

/** @function: setup_hrtimer                                          
 *  @desc    : High resolution timer setup and initialization for kern
 el                                                                    
 *  @args    : void                                                   
 *                                                                    
 *  @returns : 0 on success and error -1 on failure                   
 */                                                                   
static int setup_hrtimer(void)                                        
{
	drv_data.timer = kzalloc(sizeof(struct hrtimer), GFP_KERNEL);
	if(drv_data.timer == NULL){
		DEBUG("%s:Unsuccessful to setup the high resolution timer",__func__);
		return -1;
	}
	hrtimer_init(drv_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	drv_data.als_poll_delay = ns_to_ktime(100 * NSEC_PER_MSEC);
	drv_data.timer->function = isl29023_hrtimer_handler;
	hrtimer_start(drv_data.timer, drv_data.als_poll_delay, HRTIMER_MODE_REL);
	DEBUG("%s:Successfully setup the high resolution timer",__func__);

	return 0;
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

	drv_data.input_dev->name = "isl29023";
	input_set_drvdata(drv_data.input_dev,&drv_data);

	/* Set event data type */
	input_set_capability(drv_data.input_dev, EV_ABS, ABS_MISC);

	__set_bit(EV_ABS, drv_data.input_dev->evbit);
	input_set_abs_params(drv_data.input_dev, ABS_MISC, 0, 1, 0, 0);

	if(input_register_device(drv_data.input_dev)) {
		ERR("Failed to register input device");
		return -1;
	}

	return 0;
}                                                                     

static void isl29023_initialize(void)
{
	/* set Command register 1
MODE			:ALS continuous
INTR PERSISTANCY	:1 clock cycle */
	isl_write_field(COMMAND1_REG, ISL_FULL_MASK, ALS_CONTINUOUS_MODE | PERSISTENCY_1); 		
	/* set Command register 2
RANGE		:16000
RESOLUTION		:16 bits */
	isl_write_field(COMMAND2_REG, ISL_FULL_MASK, RANGE_16000 | RESOLUTION_16);

	/*  Interrupt low and high threshold */
	isl_write_field16(INTR_LT_LSB, ALS_LO_THRESHOLD);
	isl_write_field16(INTR_HT_LSB, ALS_HI_THRESHOLD);
}


/*
 *  @function : isl29023_probe
 *  @desc    : Device detection is successful, allocate required resources for managing the sensor
 *  @args     
 *  kobj     
 *  client   : Structure to i2c client representing the device
 *  id       : device id of the sensor id
 *  @returns : Returns strlen of buf on success and -1 on failure
 */

static int isl29023_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct isl29023_pdata *pdata = client->dev.platform_data;

	if(!client || !pdata)
		return -ENODEV;

	drv_data.client = client;
	drv_data.pdata = pdata;

	/* Initialize the sensor driver */
	isl29023_initialize();
#ifdef ISL29023_INTERRUPT_MODE
	if(!gpio_is_valid(pdata->gpio_irq)) {
		pr_err("Invalid gpio number");
		goto end;
	}

	if(gpio_request(pdata->gpio_irq, "isl29023-irq")) {
		pr_err("Failed to request gpio %d", pdata->gpio_irq);
		goto end;
	}
	if(gpio_direction_input(pdata->gpio_irq)) {
		pr_err("Failed to set direction for gpio %d", pdata->gpio_irq);
		goto gpio_fail;
	}
#endif

	mutex_init(&drv_data.mutex);
	INIT_WORK(&drv_data.work, sensor_irq_thread);
#ifdef ISL29177_INTERRUPT_MODE
	if(request_irq((drv_data.irq=gpio_to_irq(pdata->gpio_irq)), /*(irq_handler_t)*/isl29023_irq_handler, 
				IRQF_TRIGGER_FALLING, "isl29023",&drv_data)){
		ERR("Failed to request irq for gpio %d", pdata->gpio_irq);
		goto gpio_fail;
	}
#endif
	/* Setup the debug interface for driver */
	if(setup_debugfs()){
		ERR("%s:setup debugfs failed\n",__func__);
		goto sysfs_err;
	}

	/* Setup the input device infrastructure for user input subsys
	   tem */                                                                
	if(setup_input_device())                                      
		goto err_input_register_device;                       

	/* High resolution timer setup for user data polling */       
	if(setup_hrtimer())                                           
		goto err_irq_fail;                                    

	return 0;

err_irq_fail:
        input_unregister_device(drv_data.input_dev);
err_input_register_device:
	sysfs_remove_group(drv_data.isl29023_kobj,&isl29023_attr_grp);
sysfs_err:
	mutex_destroy(&drv_data.mutex);
	cancel_delayed_work((struct delayed_work*)&drv_data.work);
#ifdef ISL29023_INTERRUPT_MODE
	free_irq(drv_data.irq, NULL);
gpio_fail:
	gpio_free(pdata->gpio_irq);
#endif
end:
	return -1;

}

/** @function: isl29023_remove
 *  @desc    : Driver callback that frees all resources that are
 *  @args    : structure to i2c_client
 *  @returns : returns 0 on success
 */
static int isl29023_remove(struct i2c_client *client)
{

	cancel_delayed_work((struct delayed_work*)&drv_data.work);
	sysfs_remove_group(drv_data.isl29023_kobj,&isl29023_attr_grp);
	free_irq(gpio_to_irq(drv_data.pdata->gpio_irq),NULL);
	gpio_free(drv_data.pdata->gpio_irq);

	mutex_destroy(&drv_data.mutex);

	return 0;
}

/** @function: isl29023_get_mode
 *  @desc    : Callback function that reads the current operation mode
 *  @args
 *  @returns : returns mode value on success
 */
static int isl29023_get_mode(void)
{
        unsigned char mode;
        isl_read_field(COMMAND1_REG, OPERATION_MODE_MASK, &mode);
        return mode;
}

/** @function: isl29023_set_power_state
 *  @desc    : Set the device power state depending upon the als status 
 *  @args
 *  reg      : register address 
 *  mask     : MASK bits
 *  mode     : enable/disable bit 
 *  @returns : returns 0 on success and error -1 on failure
 */
static int isl29023_set_power_state(unsigned char reg, unsigned char mask, unsigned char mode)
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

/** @function   : isl29023_suspend
 *  @desc       : Function that handles suspend event of the OS
 *
 *  @args
 *  i2c_client  : Structure representing the I2C device
 *  mesg        : power management event message
 *
 *  @return     : 0 on success and -1 on error
 */
static int isl29023_suspend(struct i2c_client *client, pm_message_t mesg)
{
        /*
         * disable power only if suspend function called. resume function call to
	 * to wake up the device active
         */
        DEBUG("%s: Sensor suspended\n",__func__);
        drv_data.power_state = isl29023_get_mode();
        return isl29023_set_power_state(COMMAND1_REG, OPERATION_MODE_MASK, ISL_CLEAR);

}

/** @function: isl29023_set_mode
 *  @desc    : Callback function that writes the operation mode status
 *  @args
 *  @returns : returns mode [0/1] on success and -1 on failure
 */
static int isl29023_set_mode(int16_t power_state)
{

        /* push -1 to input subsystem to enable real value to go through next */
        input_report_abs(drv_data.input_dev, ABS_MISC, -1);
        hrtimer_start(drv_data.timer, drv_data.als_poll_delay, HRTIMER_MODE_REL);
        isl_write_field(COMMAND1_REG, OPERATION_MODE_MASK, power_state);
	return 0;
}

/** @function   : isl29023_resume
 *  @desc       : Function that handles resume event of the OS
 *
 *  @args       : struct i2c_client *client
 *  i2c_client  : Structure representing the I2C device
 *
 *  @return     : 0 on success and error -1 on failure
 */
static int isl29023_resume(struct i2c_client *client)
{

        if(!drv_data.power_state){
                refresh_reg_cache();
        }

        DEBUG("%s: Sensor resumed\n",__func__);
        return isl29023_set_mode(drv_data.power_state);
}

/*
 *  I2C Driver structure representing the driver operation / callbacks
 */
static struct i2c_driver isl29023_driver = {
	.driver   = { .name = "isl29023"},
	.id_table = isl_device_ids,
	.probe    = isl29023_probe,
	.remove   = isl29023_remove,
	.suspend  = isl29023_suspend,
	.resume   = isl29023_resume,
};



static int __init isl29023_init(void)
{
	return i2c_add_driver(&isl29023_driver);
}


static void __exit isl29023_exit(void)
{
	i2c_del_driver(&isl29023_driver);
}

module_init(isl29023_init);
module_exit(isl29023_exit);
MODULE_DESCRIPTION("ISL29023 Sensor device driver");
MODULE_LICENSE("GPLv2");
MODULE_AUTHOR("VVDN Technologies Pvt Ltd.");

