/******************************************************************************
 *	file		: isl29125.c
 *
 *	Description	: Driver for ISL29125 RGB light sensor
 *
 *	License		: GPLv2
 *
 *	Copyright	: Intersil Corporation (c) 2013
 *
 *            		by Murali M. <muralim@vvdntech.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.

******************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/isl29125.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/slab.h>
#define ISL29125_INTERRUPT_MODE

#ifdef ISL29125_INTERRUPT_MODE
static struct work_struct work;
static unsigned char irq_num;
#endif
static struct mutex rwlock_mutex;
static struct i2c_client *isl_client;

/* Devices supported by this driver and their I2C address */
static struct i2c_device_id isl_sensor_device_table[] = {
	{"isl29125", ISL29125_I2C_ADDR},
	{}
};

/*
 * @fn          set_optical_range
 *
 * @brief       This function sets the optical sensing range of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int set_optical_range(int *range)
{
	int ret;

	ret = i2c_smbus_read_byte_data(isl_client, CONFIG1_REG);
	if (ret < 0) {
		__dbg_read_err("%s",__func__);
		return -1;
	}
	if(*range == 4000)
		ret |= RGB_SENSE_RANGE_4000_SET;
	else if (*range == 330)
		ret &= RGB_SENSE_RANGE_330_SET;
	else
		return -1;
	ret = i2c_smbus_write_byte_data(isl_client, CONFIG1_REG, ret);
	if (ret < 0) {
		__dbg_write_err("%s",__func__);
		return -1;
	}
	return 0;
}

/*
 * @fn          get_optical_range
 *
 * @brief       This function gets the optical sensing range of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int get_optical_range(int *range)
{
	int ret;
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG1_REG);
	if (ret < 0) 
		return -1;
	
	*range = (ret & (1 << RGB_DATA_SENSE_RANGE_POS))?4000:330;
	return 0;
}

/*
 * @fn          get_adc_resolution_bits
 *
 * @brief       This function gets the adc resolution of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int get_adc_resolution_bits(int *res)
{
	int ret;
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG1_REG);
	if (ret < 0) 
		return -1;
	
	*res = (ret & (1 << ADC_BIT_RESOLUTION_POS))?12:16;
	return 0;
}

/*
 * @fn          set_adc_resolution_bits
 *
 * @brief       This function sets the adc resolution of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int set_adc_resolution_bits(int *res)
{
	int reg;
	reg = i2c_smbus_read_byte_data(isl_client, CONFIG1_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		return -1;
	}
	if(*res)
		reg |= ADC_RESOLUTION_12BIT_SET;
	else
		reg &= ADC_RESOLUTION_16BIT_SET;

	if(i2c_smbus_write_byte_data(isl_client, CONFIG1_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		return -1;
	}
	return 0;
}

/*
 * @fn          set_mode
 *
 * @brief       This function sets the operating mode of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int set_mode(int mode)
{
	short int reg;

	reg = i2c_smbus_read_byte_data(isl_client, CONFIG1_REG);
	if (reg < 0) 
		return -1;
	reg = (reg & RGB_OP_MODE_CLEAR) | mode;
	if(i2c_smbus_write_byte_data(isl_client, CONFIG1_REG, reg) < 0)
		return -1;
	
	return 0;
}

/*
 * @fn          autorange
 *
 * @brief       This function processes autoranging of sensor device
 *
 * @return      void
 *
 */

static void autorange(int green)
{
	int ret;
	unsigned int adc_resolution, optical_range;

	ret = get_adc_resolution_bits(&adc_resolution);
	if (ret < 0) {
		pr_err("%s : %s:Failed to get adc resolution\n", ISL29125_MODULE, __func__);
		return;
	}
	ret = get_optical_range(&optical_range);
	if (ret < 0) {
		pr_err( "%s : %s: Failed to get optical range\n", ISL29125_MODULE, __func__);
		return;
	}
	switch (adc_resolution) {
		case 12:
			switch(optical_range) {
				case 330:
					/* Switch to 4000 lux */
					if(green > 0xCCC) {
						optical_range = 4000;
						set_optical_range(&optical_range);
					}
					break;
				case 4000:
					/* Switch to 330 lux */
					if(green < 0xCC) {
						optical_range = 330;
						set_optical_range(&optical_range);
					}
					break;
			}
			break;
		case 16:
			switch(optical_range) {
				case 330:
					/* Switch to 4000 lux */
					if(green > 0xCCCC) {
						optical_range = 4000;
						set_optical_range(&optical_range);
					}

					break;
				case 4000:
					/* Switch to 330 lux */
					if(green < 0xCCC) {
						optical_range = 330;
						set_optical_range(&optical_range);

					}
					break;
			}
			break;
	}
}

/*
 * @fn         	isl29125_i2c_read_word16
 *
 * @brief       This function reads a word (16-bit) from sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl29125_i2c_read_word16(struct i2c_client *client, unsigned char reg_addr, unsigned short *buf)
{
	short int reg_h;
	short int reg_l;

	reg_l = i2c_smbus_read_byte_data(client, reg_addr);
	if (reg_l < 0) 
		return -1;
	
	reg_h = i2c_smbus_read_byte_data(client, reg_addr + 1);
	if (reg_h < 0) 
		return -1;
	
	*buf = (reg_h << 8) | reg_l;
	return 0;
}


/*
 * @fn         	isl29125_i2c_write_word16
 *
 * @brief       This function writes a word (16-bit) to sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl29125_i2c_write_word16(struct i2c_client *client, unsigned char reg_addr, unsigned short *buf)
{
	unsigned char reg_h;
	unsigned char reg_l;

	/* Extract LSB and MSB bytes from data */
	reg_l = *buf & 0xFF;
	reg_h = (*buf & 0xFF00) >> 8;

	if(i2c_smbus_write_byte_data(client, reg_addr, reg_l) < 0)
		return -1;
	if(i2c_smbus_write_byte_data(client, reg_addr + 1, reg_h) < 0)
		return -1;
	return 0;
}


/*
 * @fn         	show_red
 *
 * @brief       This function exports the RED Lux value in adc dec code to sysfs
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_red(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(isl29125_i2c_read_word16(client, RED_DATA_LBYTE_REG, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}

	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}


/*
 * @fn         	show_green
 *
 * @brief       This function export GREEN Lux value in adc dec code to sysfs
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */
static ssize_t show_green(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(isl29125_i2c_read_word16(client, GREEN_DATA_LBYTE_REG, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}

#ifndef ISL29125_INTERRUPT_MODE
	/* Process autoranging of sensor */
	autorange(reg);
#endif
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);

}

/*
 * @fn         	show_blue
 *
 * @brief       This function export BLUE Lux value in adc dec code to sysfs
 *
 * @return      Returns length of data buffer read on success otherwise returns an error (-1)
 *
 */

static ssize_t show_blue(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(isl29125_i2c_read_word16(client, BLUE_DATA_LBYTE_REG, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);

}

/*
 * @fn          show_mode
 *
 * @brief       This function displays the sensor operating mode
 *
 * @return     	Returns the length of data buffer read on success otherwise returns an error (-1)
 *
 */

static ssize_t show_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	switch (reg & 0x7) {
		case 0: sprintf(buf, "pwdn");break;
		case 1:	sprintf(buf, "green");break;
		case 2:	sprintf(buf, "red");break;
		case 3:	sprintf(buf, "blue");break;
		case 4:	sprintf(buf, "standby");break;
		case 5:	sprintf(buf, "green.red.blue"); break;
		case 6:	sprintf(buf, "green.red");break;
		case 7:	sprintf(buf, "green.blue");break;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn        	store_mode
 *
 * @brief       This function sets the sensor operating mode
 *
 * @return      Returns the length of buffer data written on success otherwise returns an error (-1)
 *
 */

static ssize_t store_mode(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	uint16_t mode;
	int16_t ret;

	mutex_lock(&rwlock_mutex);
	if(!strcmp(buf, "pwdn")) {
		mode = RGB_OP_PWDN_MODE_SET;
	} else if(!strcmp(buf, "green")) {
		mode = RGB_OP_GREEN_MODE_SET;
	} else if(!strcmp(buf, "red")) {
		mode = RGB_OP_RED_MODE_SET;
	} else if(!strcmp(buf, "blue")) {
		mode = RGB_OP_BLUE_MODE_SET;
	} else if(!strcmp(buf, "standby")) {
		mode = RGB_OP_STANDBY_MODE_SET;
	} else if(!strcmp(buf, "green.red.blue")) {
		mode = RGB_OP_GRB_MODE_SET;
	} else if(!strcmp(buf, "green.red")) {
		mode = RGB_OP_GR_MODE_SET;
	} else if(!strcmp(buf, "green.blue")) {
		mode = RGB_OP_GB_MODE_SET;
	} else {
		__dbg_invl_err("%s",__func__);	
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	ret = set_mode(mode);
	if (ret < 0) {
		pr_err("%s:%s:Failed to set operating mode\n", ISL29125_MODULE, __func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn        	show_optical_range
 *
 * @brief       This function shows the current optical sensing range of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_optical_range(struct device *dev, struct device_attribute *attr, char *buf)
{
	int reg;

	mutex_lock(&rwlock_mutex);
	if(get_optical_range(&reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn        	show_adc_resolution_bits
 *
 * @brief       This function shows the current adc resolution of sensor device (12-bit or 16-bit)
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_adc_resolution_bits(struct device *dev, struct device_attribute *attr, char *buf)
{
	int reg;

	mutex_lock(&rwlock_mutex);
	if(get_adc_resolution_bits(&reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn        	store_adc_resolution_bits
 *
 * @brief       This function sets the adc resolution of sensor device (12-bit or 16-bit)
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_adc_resolution_bits(struct device *dev, struct device_attribute *attr, 
						const char *buf, size_t count)
{
	int reg;
	mutex_lock(&rwlock_mutex);

	if(!strcmp(buf, "16"))
		reg = 0;
	else if(!strcmp(buf, "12"))
		reg = 1;
	else {
		__dbg_invl_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	if(set_adc_resolution_bits(&reg) < 0){
		pr_err("%s:%s:Failed to set adc resolution\n",ISL29125_MODULE, __func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

#ifdef ISL29125_INTERRUPT_MODE
/*
 * @fn         	show_intr_threshold_high
 *
 * @brief       This function shows the high interrupt threshold value in adc dec code
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_high(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(isl29125_i2c_read_word16(client, HIGH_THRESHOLD_LBYTE_REG, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn         	store_intr_threshold_high
 *
 * @brief       This function sets the high interrupt threshold value in adc dec code
 *
 * @return      Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_high(struct device *dev, struct device_attribute *attr,
						 const char *buf, size_t count)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 65535) {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	if(isl29125_i2c_write_word16(client, HIGH_THRESHOLD_LBYTE_REG, &reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;

}

/*
 * @fn          show_intr_threshold_low
 *
 * @brief       This function shows the low interrupt threshold value in adc dec code
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_low(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(isl29125_i2c_read_word16(client, LOW_THRESHOLD_LBYTE_REG, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", reg);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn          store_intr_threshold_low
 *
 * @brief       This function sets the low interrupt threshold value in adc dec code
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */
static ssize_t store_intr_threshold_low(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 65535) {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	if(isl29125_i2c_write_word16(client, LOW_THRESHOLD_LBYTE_REG, &reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}

/*
 * @fn          show_intr_threshold_assign
 *
 * @brief       This function displays the color component for which the interrupts are generated
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_assign(struct device *dev, struct device_attribute *attr,
							 char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}

	/* Extract interrupt threshold assign value */
	reg = (reg & ((0x3) << INTR_THRESHOLD_ASSIGN_POS)) >> INTR_THRESHOLD_ASSIGN_POS;
	switch(reg) {
		case 0:	sprintf(buf, "%s", "none"); break;
		case 1:	sprintf(buf, "%s", "green");break;
		case 2:	sprintf(buf, "%s", "red");break;
		case 3:	sprintf(buf, "%s", "blue");break;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);

}

/*
 * @fn          store_intr_threshold_assign
 *
 * @brief       This function displays the color component for which the interrupts are generated
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_assign(struct device *dev, struct device_attribute *attr, 
						const char *buf, size_t count)
{
	short int reg;
	short int threshold_assign;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(!strcmp(buf, "none")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_CLEAR;
	} else if(!strcmp(buf, "green")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_GREEN;
	} else if(!strcmp(buf, "red")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_RED;
	} else if(!strcmp(buf, "blue")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_BLUE;
	} else {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	reg = (reg & INTR_THRESHOLD_ASSIGN_CLEAR) | threshold_assign;
	if(i2c_smbus_write_byte_data(client, CONFIG3_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}

/*
 * @fn          show_intr_persistency
 *
 * @brief       This function displays the current interrupt persistency of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_persistency(struct device *dev, struct device_attribute *attr, char *buf)
{

	short int reg;
	short int intr_persist;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	reg = (reg & (0x3 << INTR_PERSIST_CTRL_POS)) >> INTR_PERSIST_CTRL_POS;
	switch(reg) {
		case 0:	intr_persist = 1;break;
		case 1:	intr_persist = 2;break;
		case 2:	intr_persist = 4;break;
		case 3:	intr_persist = 8;break;
	}
	sprintf(buf, "%d", intr_persist);
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn          store_intr_persistency
 *
 * @brief       This function sets the interrupt persistency of sensor device
 *
 * @return      Returns size of buffer data on success otherwise returns an error (-1)
 *
 */

static ssize_t store_intr_persistency(struct device *dev, struct device_attribute *attr, 
					const char *buf, size_t count)
{
	short int reg;
	short int intr_persist;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	intr_persist = simple_strtoul(buf, NULL, 10);
	if (intr_persist == 8)
		intr_persist = INTR_PERSIST_SET_8;
	else if (intr_persist == 4)
		intr_persist = INTR_PERSIST_SET_4;
	else if (intr_persist == 2)
		intr_persist = INTR_PERSIST_SET_2;
	else if (intr_persist == 1)
		intr_persist = INTR_PERSIST_SET_1;
	else {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	reg &= INTR_PERSIST_CTRL_CLEAR;
	reg |= intr_persist << INTR_PERSIST_CTRL_POS;
	if(i2c_smbus_write_byte_data(client, CONFIG3_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}

/*
 * @fn         	show_rgb_conv_intr
 *
 * @brief       This function displays the RGB conversion interrupt Enable Disable status
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_rgb_conv_intr(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		pr_err( "%s : %s: Failed to read data\n", ISL29125_MODULE, __func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	reg = (reg & (1 << RGB_CONV_TO_INTB_CTRL_POS)) >> RGB_CONV_TO_INTB_CTRL_POS;
	sprintf(buf, "%s", reg?"disable":"enable");
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn        	store_rgb_conv_intr
 *
 * @brief       This function Enables or Disables the RGB conversion interrupt
 *
 * @return      Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_rgb_conv_intr(struct device *dev, struct device_attribute *attr, 
					const char *buf, size_t count)
{
	int rgb_conv_intr;
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(!strcmp(buf, "enable"))
		rgb_conv_intr = 0;
	else if(!strcmp(buf, "disable"))
		rgb_conv_intr = 1;
	else {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}

	reg |= (reg & RGB_CONV_TO_INTB_CLEAR) | rgb_conv_intr;
	if(i2c_smbus_write_byte_data(client, CONFIG3_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}


/*
 * @fn          show_adc_start_sync
 *
 * @brief       This function Displays the adc start synchronization method
 *
 * @return     	Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_adc_start_sync(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	reg = (reg & (1 << RGB_START_SYNC_AT_INTB_POS)) >> RGB_START_SYNC_AT_INTB_POS;
	sprintf(buf, "%s", reg?"risingIntb":"i2cwrite");
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 *
 *  @fn         store_adc_start_sync
 *
 * @brief       This function sets the adc start synchronization method
 *
 * @return      Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_adc_start_sync(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t count)
{
	short int reg, adc_start_sync;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(!strcmp(buf, "i2cwrite"))
		adc_start_sync = 0;
	else if(!strcmp(buf, "risingIntb"))
		adc_start_sync = 1;
	else {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	if(adc_start_sync)
		reg |= ADC_START_AT_RISING_INTB;
	else
		reg &= ADC_START_AT_I2C_WRITE;
	if(i2c_smbus_write_byte_data(client, CONFIG1_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}


/*
 * @fn          show_ir_comp_ctrl
 *
 * @brief       This function Displays the IR compensation control status
 *
 * @return      Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_comp_ctrl(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG2_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	reg = (reg & (1 << IR_COMPENSATION_CTRL_POS)) >> IR_COMPENSATION_CTRL_POS;
	sprintf(buf, "%s", reg?"disable":"enable");
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn          store_ir_comp_ctrl
 *
 * @brief       This function Enables or Disables the IR compensation control
 *
 * @return      Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_ir_comp_ctrl(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int ir_comp_ctrl;
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	if(!strcmp(buf, "enable"))
		ir_comp_ctrl = 0;
	else if(!strcmp(buf, "disable"))
		ir_comp_ctrl = 1;
	else {
		__dbg_invl_err("%s",__func__);
		goto err;
	}

	reg = i2c_smbus_read_byte_data(client, CONFIG2_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	reg = (reg & IR_COMPENSATION_CLEAR) | ir_comp_ctrl;
	if(i2c_smbus_write_byte_data(client, CONFIG2_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:
	mutex_unlock(&rwlock_mutex);
	return -1;
}

/*
 * @fn          show_active_ir_comp
 *
 * @brief       This function Displays the active IR compensation value
 *
 * @return     	Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t show_active_ir_comp(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG2_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		mutex_unlock(&rwlock_mutex);
		return -1;
	}
	sprintf(buf, "%d", (reg & 0x3F));
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
}

/*
 * @fn          store_active_ir_comp
 *
 * @brief       This function sets the active IR compensation value
 *
 * @return     	Returns length of data buffer on success otherwise returns an error (-1)
 *
 */

static ssize_t store_active_ir_comp(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&rwlock_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 63) {
		__dbg_invl_err("%s",__func__);
		goto err;
	}
	if(isl29125_i2c_write_word16(client, CONFIG2_REG, &reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
	mutex_unlock(&rwlock_mutex);
	return strlen(buf);
err:	
	mutex_unlock(&rwlock_mutex);
	return -1;
}

#endif
/* Attributes of ISL29125 RGB light sensor */
static DEVICE_ATTR(red, ISL29125_SYSFS_PERMISSIONS , show_red, NULL);
static DEVICE_ATTR(green, ISL29125_SYSFS_PERMISSIONS , show_green, NULL);
static DEVICE_ATTR(blue, ISL29125_SYSFS_PERMISSIONS , show_blue, NULL);
static DEVICE_ATTR(mode, ISL29125_SYSFS_PERMISSIONS , show_mode, store_mode);
static DEVICE_ATTR(optical_range, ISL29125_SYSFS_PERMISSIONS , show_optical_range, NULL);
static DEVICE_ATTR(adc_resolution_bits, ISL29125_SYSFS_PERMISSIONS , show_adc_resolution_bits,
			 store_adc_resolution_bits);

#ifdef ISL29125_INTERRUPT_MODE
static DEVICE_ATTR(intr_threshold_high , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_high,
			 store_intr_threshold_high);
static DEVICE_ATTR(intr_threshold_low , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_low,
			 store_intr_threshold_low);
static DEVICE_ATTR(intr_threshold_assign , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_assign,
			 store_intr_threshold_assign);
static DEVICE_ATTR(intr_persistency, ISL29125_SYSFS_PERMISSIONS , show_intr_persistency,
			 store_intr_persistency);
static DEVICE_ATTR(rgb_conv_intr, ISL29125_SYSFS_PERMISSIONS , show_rgb_conv_intr,
			 store_rgb_conv_intr);
static DEVICE_ATTR(adc_start_sync, ISL29125_SYSFS_PERMISSIONS , show_adc_start_sync, 
			store_adc_start_sync);

static DEVICE_ATTR(ir_comp_ctrl, ISL29125_SYSFS_PERMISSIONS , show_ir_comp_ctrl, store_ir_comp_ctrl);
static DEVICE_ATTR(active_ir_comp, ISL29125_SYSFS_PERMISSIONS , show_active_ir_comp, 
			store_active_ir_comp);
#endif

static struct attribute *isl29125_attributes[] = {

	/* read RGB value attributes */
	&dev_attr_red.attr,
	&dev_attr_green.attr,
	&dev_attr_blue.attr,

	/* Device operating mode */
	&dev_attr_mode.attr,

	/* Current optical sensing range */
	&dev_attr_optical_range.attr,

	/* Current adc resolution */
	&dev_attr_adc_resolution_bits.attr,

#ifdef ISL29125_INTERRUPT_MODE
	/* Interrupt related attributes */
	&dev_attr_intr_threshold_high.attr,
	&dev_attr_intr_threshold_low.attr,
	&dev_attr_intr_threshold_assign.attr,
	&dev_attr_intr_persistency.attr,
	&dev_attr_rgb_conv_intr.attr,
	&dev_attr_adc_start_sync.attr,

	/* IR compensation related attributes */
	&dev_attr_ir_comp_ctrl.attr,
	&dev_attr_active_ir_comp.attr,
#endif
	NULL
};

static struct attribute_group isl29125_attr_group = {
	.attrs = isl29125_attributes
};

#ifdef ISL29125_INTERRUPT_MODE

/*
 * @fn          sensor_irq_thread
 *
 * @brief       This thread is scheduled by sensor interrupt
 *
 * @return     	void
 */

static void sensor_irq_thread(struct work_struct *work)
{

	short int reg, intr_assign;
	unsigned short int green;
	int ret;

	/* Read the interrupt status flags from sensor */
	reg = i2c_smbus_read_byte_data(isl_client, STATUS_FLAGS_REG);
	if (reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err_out;
	}

	/* A threshold interrupt occured */
	if(reg & (1 << RGBTHF_FLAG_POS)) {
		intr_assign = i2c_smbus_read_byte_data(isl_client, CONFIG3_REG);
		if (intr_assign < 0) {
			__dbg_read_err("%s",__func__);
			goto err_out;
		}
		intr_assign &= 0x3;
		if (intr_assign == INTR_THRESHOLD_ASSIGN_GREEN) {
			/* GREEN interrupt occured */
			if(isl29125_i2c_read_word16(isl_client, GREEN_DATA_LBYTE_REG, &green) < 0){
				__dbg_read_err("%s",__func__);
				goto err_out;
			}
			autorange(green);
		}
	}

	if(reg & (1 << BOUTF_FLAG_POS)) {
		/* Brownout interrupt occured */
		ret = i2c_smbus_read_byte_data(isl_client, STATUS_FLAGS_REG);
		if( ret < 0) {
			__dbg_write_err("%s",__func__);
			goto err_out;
		}
		ret &= ~(1 << BOUTF_FLAG_POS);

		/* Clear the BOUTF flag */
		ret = i2c_smbus_write_byte_data(isl_client, STATUS_FLAGS_REG, ret);
		if( ret < 0) {
			__dbg_write_err("%s",__func__);
			goto err_out;
		}
	}
err_out:
	enable_irq(irq_num);
}

/*
 * @fn          isl_sensor_irq_handler
 *
 * @brief       This function is the interrupt handler for sensor. It schedules and interrupt
 *              thread and resturns.
 *
 * @return      IRQ_HANDLED
 *
 */

static irqreturn_t isl_sensor_irq_handler(int irq, void *dev_id)
{
	disable_irq_nosync(irq_num);
	schedule_work(&work);
	return IRQ_HANDLED;
}

#endif

/*
 * @fn          initialize_isl29125
 *
 * @brief       This function initializes the sensor device with default values
 *
 * @return      void
 *
 */

static void initialize_isl29125(struct i2c_client *client)
{
	unsigned char reg;


#ifdef ISL29125_INTERRUPT_MODE
	/* Set device mode to RGB ,
	   RGB Data sensing range 4000 Lux,
	   ADC resolution 16-bit,
	   ADC start at intb start*/
	i2c_smbus_write_byte_data(client, CONFIG1_REG, 0x2D);

#endif

	/* Default IR Active compenstation,
	   Disable IR compensation control */
	i2c_smbus_write_byte_data(client, CONFIG2_REG, 0x80);

#ifdef ISL29125_INTERRUPT_MODE
	/* Interrupt threshold assignment for Green,
	   Interrupt persistency as 8 conversion data out of windows */
	i2c_smbus_write_byte_data(client, CONFIG3_REG, 0x0D);

	/* Writing interrupt low threshold as 0xCCC (5% of max range) */
	i2c_smbus_write_byte_data(client, LOW_THRESHOLD_LBYTE_REG, 0xCC);
	i2c_smbus_write_byte_data(client, LOW_THRESHOLD_HBYTE_REG, 0x0C);

	/* Writing interrupt high threshold as 0xCCCC (80% of max range)  */
	i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_LBYTE_REG, 0xCC);
	i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_HBYTE_REG, 0xCC);
#endif
	/* Clear the brownout status flag */
	reg = i2c_smbus_read_byte_data(client, STATUS_FLAGS_REG);
	reg &= ~(1 << BOUTF_FLAG_POS);
	i2c_smbus_write_byte_data(client, STATUS_FLAGS_REG, reg);

	/* Set device mode to RGB ,
	   RGB Data sensing range 4000 Lux,
	   ADC resolution 16-bit,
	   ADC start at i2c write 0x01*/
	i2c_smbus_write_byte_data(client, CONFIG1_REG, 0x01);
}

/*
 * @fn          isl_sensor_probe
 *
 * @brief       This function is called by I2C Bus driver on detection of sensor device.
 *              It validates the device and initialize the resources required by driver.
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __devinit isl_sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct isl29125_platform_data *pdata;
	short int reg;

	pdata = client->dev.platform_data;
	if(pdata == NULL) {
		pr_err("%s:%s:Unable to find platform data\n", ISL29125_MODULE, __func__);
		return -1;
	}

	/* Read the device id register from ISL29125 sensor device */
	reg = i2c_smbus_read_byte_data(client, DEVICE_ID_REG);
	if(reg < 0){
		pr_err("%s :failed to read device id\n",__func__);
		return -1;
	}

	/* Verify whether we have a valid sensor */
	if( reg != ISL29125_DEV_ID) {
		pr_err( "%s : %s: Invalid device id for ISL29125 sensor device\n", ISL29125_MODULE, __func__);
		goto err;
	}
	/* Have a copy of i2c client information */
	isl_client = client;

	/* Initialize the default configurations for ISL29125 sensor device */
	initialize_isl29125(client);

#ifdef ISL29125_INTERRUPT_MODE

	/* Request gpio for sensor interrupt */
	if(gpio_request(pdata->gpio_irq, "isl29125") < 0){
		pr_err("%s:%s:Failed to request GPIO %d for ISL29125 sensor interrupt\n",
					 ISL29125_MODULE, __func__, pdata->gpio_irq);
		goto err;
	}

	/* Configure interrupt GPIO as input pin */
	if(gpio_direction_input(pdata->gpio_irq) < 0){
		pr_err( "%s : %s: Failed to set direction for ISL29125 interrupt gpio\n",
					 ISL29125_MODULE, __func__);
		goto gpio_err;
	}

	irq_num = gpio_to_irq(pdata->gpio_irq);
	if (irq_num < 0) {
		pr_err( "%s : %s: Failed to get IRQ number for ISL29125 sensor GPIO\n",
					 ISL29125_MODULE, __func__);
		goto gpio_err;
	}

	
	if(irq_set_irq_type(irq_num, IRQ_TYPE_EDGE_FALLING) < 0){
		pr_err("%s:Failed to configure the interrupt polarity\n",__func__);
		goto gpio_err;
	} 

	/* Initialize the sensor interrupt thread that would be scheduled by sensor
	   interrupt handler */
	INIT_WORK(&work, sensor_irq_thread);

	/* Register irq handler for sensor */
	if( request_irq(irq_num, isl_sensor_irq_handler, IRQF_TRIGGER_FALLING, "isl29125", NULL) < 0){
		pr_err( "%s : %s: Failed to register irq handler for ISL29125 sensor interrupt\n", ISL29125_MODULE, __func__);
		goto gpio_err;
	}

#endif

	/* Register sysfs hooks */
	if(sysfs_create_group(&client->dev.kobj, &isl29125_attr_group) < 0){
		pr_err( "%s : %s: Failed to create sysfs\n", ISL29125_MODULE, __func__);
		goto err;
	}

	/* Initialize a mutex for synchronization in sysfs file access */
	mutex_init(&rwlock_mutex);
	
	/* Start ADC conversion */
	i2c_smbus_write_byte_data(client, CONFIG1_REG, 0x01);
	
	return 0;

#ifdef ISL29125_INTERRUPT_MODE
gpio_err:
	gpio_free(pdata->gpio_irq);
#endif

err:
	return -1;
}

/*
 * @fn          isl_sensor_remove
 *
 * @brief       This function is called when sensor device gets removed from bus
 *
 * @return      Returns 0
 *
 */

static int __devexit isl_sensor_remove(struct i2c_client *client)
{

	sysfs_remove_group(&client->dev.kobj, &isl29125_attr_group);
#ifdef ISL29125_INTERRUPT_MODE
	/* Free interrupt number */
	free_irq(irq_num, NULL);

	/* Free requested gpio */
	gpio_free(ISL29125_INTR_GPIO);
#endif
	return 0;
}

/*
 * @fn          isl_sensor_suspend
 *
 * @brief       This function puts the sensor device in standby mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_sensor_suspend(struct i2c_client *client, pm_message_t msg)
{
	short int reg;

	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if(reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	reg = (reg & RGB_OP_MODE_CLEAR) | RGB_OP_STANDBY_MODE_SET;
	/* Put the sensor device in standby mode */
	if(i2c_smbus_write_byte_data(client, CONFIG1_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
#ifdef ISL29125_INTERRUPT_MODE
	disable_irq_nosync(irq_num);
#endif
	return 0;
err:
	return -1;
}

/*
 * @fn          isl_sensor_resume
 *
 * @brief       This function Resumes the sensor device from suspend mode and puts it in Active conversion mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_sensor_resume(struct i2c_client *client)
{

	short int reg;

	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if(reg < 0) {
		__dbg_read_err("%s",__func__);
		goto err;
	}
	reg &= RGB_OP_MODE_CLEAR;
	reg |= RGB_OP_GRB_MODE_SET;
	/* Put the sensor device in active conversion mode */
	if(i2c_smbus_write_byte_data(client, CONFIG1_REG, reg) < 0){
		__dbg_write_err("%s",__func__);
		goto err;
	}
#ifdef ISL29125_INTERRUPT_MODE
	enable_irq(irq_num);
#endif
	return 0;
err:
	return -1;
}

/* i2c device driver information */
static struct i2c_driver isl_sensor_driver = {
	.driver = {
		.name = "isl29125",
	},
	.probe    = isl_sensor_probe	   ,
	.remove   = isl_sensor_remove	   ,
	.id_table = isl_sensor_device_table,
	.suspend  = isl_sensor_suspend	   ,
	.resume	  = isl_sensor_resume	   ,
};

/*
 *  @fn          isl29125_init
 *
 *  @brief       This function initializes the driver
 *
 *  @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __init isl29125_init(void)
{

	/* Register i2c driver with i2c core */
	return i2c_add_driver(&isl_sensor_driver);

}

/*
 * @fn          isl29125_exit
 *
 * @brief       This function is called to cleanup driver entry
 *
 * @return      Void
 *
 */

static void __exit isl29125_exit(void)
{
	/* Unregister i2c driver with i2c core */
	i2c_del_driver(&isl_sensor_driver);
}

MODULE_AUTHOR("VVDN Technologies Pvt Ltd");
MODULE_LICENSE("GPLv2");
MODULE_DESCRIPTION("Driver for ISL29125 RGB light sensor");

module_init(isl29125_init);
module_exit(isl29125_exit);
