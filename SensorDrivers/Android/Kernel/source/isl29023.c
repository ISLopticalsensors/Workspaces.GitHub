/****************************************************************************
 * 	
 *	File 		: isl29023.c
 *	
 *	Description 	: Device Driver for ISL29023 ALS Sensor
 *
 *	License 	: GPLv2
 *	
 *	Copyright 	: Intersil Corporation (c) 2013 
 *
 *	  		by Sanoj Kumar <sanoj.kumar@vvdntech.com>
 *
 *	This program is free software; you can redistribute it and/or modify
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
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/isl29023.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/sysfs.h> 
#define ISL29023_INTERRUPT_MODE 
#define NORM_CONST	1000 
//#define ISL_DBUG

static struct isl29023_data {
	struct i2c_client *client_data;
	struct work_struct work;
	struct mutex isl_mutex;
	int32 last_mod;
	ulong range;
	int16 intr_flag;
	uint16 last_ir_lt;
	uint32 irq_num;
	uint16 last_ir_ht;
	uint16 last_als_lt;
	uint16 last_als_ht;
	
} isl_data;
static int att_const = 6;        	//on the behalf of LM1=216 LM2=200 VIS1=482 VIS2=470
static int ir_const = 5; 		//IR1=470 IR2=460

/*Devices supported by this driver and their addresses*/
static struct i2c_device_id isl_device_table[] = {
	{"isl29023",ISL29023_I2C_ADDR},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl_device_table);

/*
 * @fn          get_adc_resolution_bits
 *
 * @brief       This function reads the current dc resolution of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 get_adc_resolution_bits(uint16 *res)
{
	int16 ret;
	ret = i2c_smbus_read_byte_data(isl_data.client_data, REG_CMD_2);
	if(ret < 0)
		return -1;

	ret = (ret & ISL_RES_MASK) >> 2;
	switch(ret)
	{
		case 0: *res = 16; break;
		case 1: *res = 12; break;
		case 2: *res = 8;  break;
		case 3: *res = 4;  break;
		default: break;
	}
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

static int32 set_adc_resolution_bits(uint16 res)
{
	uint16 reg;
	reg = i2c_smbus_read_byte_data(isl_data.client_data, 
					REG_CMD_2);
	if(reg < 0)
		return -1;

	reg =  ( reg & ISL_ADC_READ_MASK ) | res;
	if(i2c_smbus_write_byte_data(isl_data.client_data, 
					REG_CMD_2, reg) < 0)	
		return -1;
	return 0;	
}

/*
 * @fn          show_adc_res_bits
 *
 * @brief       This function shows the current adc resolution of sensor
 *		device (16/12/8/4 -bit)
 *
 * @return      Returns the length of data buffer on success otherwise 
 *		returns an error (-1)
 *
 */

static ssize_t show_adc_res_bits(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16 reg;
	mutex_lock(&isl_data.isl_mutex);
	if(get_adc_resolution_bits(&reg) < 0){
		pr_err("%s :%s :Failed\n",MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	sprintf(buf,"%d",reg);
	mutex_unlock(&isl_data.isl_mutex);	
	return strlen(buf);
} 

/*
 * @fn          store_adc_res_bits
 *
 * @brief       This function sets the adc resolution of
 *		sensor device (16/12/8/4 -bit)
 *
 * @return      Returns the length of data buffer on success
 *		otherwise returns an error (-1)
 *
 */

static ssize_t store_adc_res_bits(struct device *dev, 
			struct device_attribute *attr, const char *buf,
				size_t count)
{
	int16 reg;
	mutex_lock(&isl_data.isl_mutex);

	if(!strcmp(buf, "16")){
		reg = ADC_RES_16BIT_SET;
	}else if(!strcmp(buf, "12")){
		reg = ADC_RES_12BIT_SET;
	}else if (!strcmp(buf, "8")){
		reg = ADC_RES_8BIT_SET;
	}else if(!strcmp(buf, "4")){
		reg = ADC_RES_4BIT_SET;
	}else{
		printk("%s :%s :Invalid ADC value"
				"\n", MODULE_NAME, __func__ );
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	if(set_adc_resolution_bits(reg) < 0){
		printk("%s : %s failed"
				"\n",MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          show_intr_persistency
 *
 * @brief       This function displays the current interrupt
 *		persistency of sensor device
 *
 * @return      Returns the length of data buffer on success 
 *		otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_persistency(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int16 reg;
	int16 intr_persist;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);		
	reg = i2c_smbus_read_byte_data(client, REG_CMD_1);
	if(reg < 0){
		mutex_unlock(&isl_data.isl_mutex);
		printk("%s :%s :Failed to read REG_CMD_1"
				"\n",MODULE_NAME, __func__);
		return -1;
	}
	switch(reg & 0x03)
	{
		case 0:	intr_persist = 1; break;
		case 1:	intr_persist = 4; break;
		case 2:	intr_persist = 8; break;
		case 3:	intr_persist = 16;break;
	} 
	sprintf(buf,"%d\n", intr_persist);
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          store_intr_persistency
 *
 * @brief       This function sets the interrupt persistency of
 *		sensor device
 *
 * @return      Returns size of buffer data on success otherwise
 *		returns an error (-1)
 *
 */

static ssize_t store_intr_persistency(struct device *dev,
		struct device_attribute *attr, 
		const char *buf, size_t count)
{
	int16 intr_persist;
	int16 reg;
	int32 ret;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	intr_persist = simple_strtoul(buf, NULL, 10);

	if (intr_persist == 16){
		intr_persist = INTR_PERSIST_SET_16BIT;
	}else if (intr_persist == 8){
		intr_persist = INTR_PERSIST_SET_8BIT;
	}else if (intr_persist == 4){
		intr_persist = INTR_PERSIST_SET_4BIT;
	}else if (intr_persist == 1){
		intr_persist = INTR_PERSIST_SET_1BIT;
	}else {
		printk("%s : %s:Invalid value"
				"\n", MODULE_NAME, __func__);
		goto err_out;
	}

	reg = i2c_smbus_read_byte_data(client, REG_CMD_1);
	if (reg < 0) {
		pr_err( "%s : %s failed to read i2c-dev"
				"\n", MODULE_NAME, __func__);
		goto err_out;
	}

	reg = (reg & INTR_PERSIST_BIT_CLEAR) | intr_persist;
	ret = i2c_smbus_write_byte_data(client, REG_CMD_1, reg);
	if (ret < 0) {
		pr_err( "%s : %s: Failed to write i2c-dev"
				"\n", MODULE_NAME, __func__);
		goto err_out;
	}
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
err_out:
	mutex_unlock(&isl_data.isl_mutex);
	return -1;
}

/*
 * @fn          isl29023_i2c_read_word16
 *
 * @brief       This wrapper function reads a word (16-bit) from 
 *		sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 isl29023_i2c_read_word16(struct i2c_client *client,
			uchar reg_addr, uint32 *buf)
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
 * @fn          isl29023_i2c_write_word16
 *
 * @brief       This function writes a word (16-bit) to sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 isl29023_i2c_write_word16(struct i2c_client *client,
			uchar reg_addr, uint16 *buf)
{
	uchar reg_h;
	uchar reg_l;

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
 * @fn          show_ir_compensation
 *
 * @brief       This function exports the IR compensation value to sysfs
 *
 * @return      Returns the length of data buffer on success 
 *		otherwise returns an error (-1)
 *
 */
static ssize_t show_ir_compensation(struct device *dev,
		struct device_attribute *attr, char *buf)
{	int ir_val,als_val;
	struct i2c_client *client = to_i2c_client(dev);

	if(isl_data.range ==  64000){
		mutex_lock(&isl_data.isl_mutex);
		if(isl29023_i2c_read_word16(client, ALS_DATA_LSB, &als_val) < 0){
			pr_err( "%s :%s :Failed to read ALS data\n",
					MODULE_NAME,__func__);
			return -1;
		}
		if(isl29023_i2c_read_word16(client, ALS_DATA_LSB, &ir_val) < 0){
			pr_err( "%s :%s :Failed to read ALS data\n",
					MODULE_NAME,__func__);
			return -1;
		}
		mutex_unlock(&isl_data.isl_mutex);
		sprintf(buf, "%d",((att_const * NORM_CONST * als_val) + (ir_const * ir_val))/65536);
	}
	else{
		printk(KERN_INFO "\nThe IR sensing range must be 64000\n");
	}
	return strlen(buf);
}

/*
 * @fn          show_ir_current
 *
 * @brief       This function exports the IR Lux value in adc 
 *		code to sysfs
 *
 * @return      Returns the length of data buffer on success 
 *		otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ir_val;
	mutex_lock(&isl_data.isl_mutex);
	if(isl29023_i2c_read_word16(client, ALS_DATA_LSB, &ir_val) < 0){
		pr_err( "%s :%s :Failed to read IR Count"
				"\n",MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	sprintf(buf, "%d",ir_val);
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

#ifdef ISL29023_INTERRUPT_MODE
/*
 * @fn          show_intr_threshold_high
 *
 * @brief       This function shows the high interrupt threshold
 *		value in adc dec code
 *
 * @return      Returns the length of data buffer on success 
 *		otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_high(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	uint32 reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	if(isl_data.intr_flag){
		sprintf(buf, "%d\n",isl_data.last_ir_ht);
		goto end;
	}else{
		if(isl29023_i2c_read_word16(client, 
				HIGH_THRESHOLD_LBYTE_REG, &reg) < 0){
			pr_err( "%s : %s: Failed to read i2c-dev"
					"\n", MODULE_NAME, __func__);
			mutex_unlock(&isl_data.isl_mutex);
			return -1;
		}
		sprintf(buf, "%d\n", reg);
	}
end:
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          store_intr_threshold_high
 *
 * @brief       This function sets the high interrupt threshold 
 *		value in adc dec code
 *
 * @return      Returns length of data buffer on success otherwise
 *		 returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_high(struct device *dev, 
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint16 reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 65535){
		pr_err( "%s : %s: Invalid input threhold"
				"\n", MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}

	if(isl29023_i2c_write_word16(client, 
				HIGH_THRESHOLD_LBYTE_REG, &reg) < 0){
		pr_err( "%s : %s: Failed to write word"
				"\n", MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}

	if(isl_data.intr_flag)
		isl_data.last_ir_ht = reg;
	else
		isl_data.last_als_ht = reg;

	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          show_intr_threshold_low
 *
 * @brief       This function shows the low interrupt threshold
 *		value in adc dec code
 *
 * @return      Returns the length of data buffer on success 
 *		otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_low(struct device *dev, 
		struct device_attribute *attr,
		char *buf)
{
	uint32  reg;
	int32 	ret;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	if(isl_data.intr_flag){
		sprintf(buf, "%d\n",isl_data.last_ir_lt);
		goto end;
	}
	else{
		ret = isl29023_i2c_read_word16(client, 
				LOW_THRESHOLD_LBYTE_REG, &reg);
		if (ret < 0) {
			pr_err( "%s : %s: Failed to read word\n",
					MODULE_NAME,  __func__);
			mutex_unlock(&isl_data.isl_mutex);
			return -1;
		}
		sprintf(buf, "%d", reg);
	}
end:
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          store_intr_threshold_low
 *
 * @brief       This function sets the low interrupt threshold value in
 *		adc dec code
 *
 * @return      Returns the length of data buffer on success otherwise 
 *		returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_low(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int32 ret;
	uint16 reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 65535){
		pr_err( "%s : %s: Invalid threshold\n",
				MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	ret = isl29023_i2c_write_word16(client, 
				LOW_THRESHOLD_LBYTE_REG, &reg);
	if (ret < 0) {
		pr_err( "%s : %s: Failed to write word\n", 
				MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}

	if(isl_data.intr_flag)
		isl_data.last_ir_lt = reg;	
	else
		isl_data.last_als_lt = reg;

	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}
#endif


/*
 * @fn          isl_get_sensing_range
 *
 * @brief       This function gets the current ALS sensing range of 
 *              sensor device
 *
 * @return      Returns the data on buffer on success otherwise returns 
 *              an error (-EINVAL)
 *
 */

static int isl_get_sensing_range(unsigned int *als_range)
{
        int ret;

        ret = i2c_smbus_read_byte_data(isl_data.client_data,REG_CMD_2);
        if(ret < 0){
                mutex_unlock(&isl_data.isl_mutex);
                pr_err( "%s :%s :Failed to read the REG_CMD_2"
                                "\n", MODULE_NAME, __func__);
                return -1;
        }
        switch(ret & 0x03)
	{
		case 0x00:
			*als_range = 1000;
			break;
		case 0x01:
			*als_range = 4000;
			break;
		case 0x02:
			*als_range = 16000;
			break;
		case 0x03:
			*als_range = 64000;
			break;
	}
        return 0;
}

/*
 * @fn          isl_set_sensing_mode
 *
 * @brief       This function sets the operating mode of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 isl_set_sensing_mode(int mod)
{
	int32 reg;

	reg = i2c_smbus_read_byte_data(isl_data.client_data, 
						REG_CMD_1);
	if(reg < 0)
		return -EINVAL;
	reg = (reg & 0x1F) | mod;		
	if(i2c_smbus_write_byte_data(isl_data.client_data,
					REG_CMD_1 ,reg) < 0)
		return -EINVAL;
	return mod;
}

/*
 * @fn          isl_get_sensing_mode
 *
 * @brief       This function gets the operating mode of sensor
 *		device
 *
 * @return      Returns the mode register value on success
 *		otherwise returns an error (-1)
 *
 */

static int32 isl_get_sensing_mode(struct i2c_client *client)
{
	int32 reg ;
	reg = i2c_smbus_read_byte_data(client , REG_CMD_1);
	if(reg < 0)
		return -1;
	return ((reg & 0xf0) >> 5);

}

/*
 * @fn          show_sensing_mode
 *
 * @brief       This function shows the current optical sensing mode
 *		of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *		(-1)
 *
 */

static ssize_t show_sensing_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int32 reg;
	struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl_data.isl_mutex);
	reg = isl_get_sensing_mode(client);
	if(reg < 0){
		pr_err( "%s :%s failed to read i2c-dev"
				"\n",MODULE_NAME, __func__);
		mutex_unlock(&isl_data.isl_mutex);
		return -1;
	}
	switch(reg)
	{
		case ISL_MOD_POWERDOWN:
			sprintf(buf, "%s\n", "pwdn");
			break;

		case ISL_MOD_ALS_ONCE:
			sprintf(buf, "%s\n", "alsonce");
			break;

		case ISL_MOD_IR_ONCE:
			sprintf(buf, "%s\n", "ironce");
			break;

		case ISL_MOD_RESERVED:
			sprintf(buf, "%s\n", "reserved");
			break;

		case ISL_MOD_ALS_CONT:
			sprintf(buf, "%s\n", "alscontinuous");
			break;

		case ISL_MOD_IR_CONT:
			sprintf(buf, "%s\n", "ircontinuous");
			break;
	}
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          store_sensing_mode
 *
 * @brief       This function sets the current optical sensing mode 
 *		of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise
 *		returns an error (-1)
 *
 */

static ssize_t store_sensing_mode(struct device *dev, 
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int32 mode;
	struct i2c_client *client = to_i2c_client(dev);	

	mutex_lock(&isl_data.isl_mutex);
	if(!strcmp(buf, "pwdn")){
		mode = ALS_OP_MODE_PWDN_SET;
	}else if(!strcmp(buf, "alsonce")){ 
		mode = ALS_OP_MODE_ALS_ONCE;
		isl_data.intr_flag = 0;
	}else if(!strcmp(buf, "ironce")){
		mode = ALS_OP_MODE_IR_ONCE;
		isl_data.intr_flag = 1;
	}else if(!strcmp(buf, "reserved")){
		mode = ALS_OP_MODE_RESERVERD;
	}else if(!strcmp(buf, "alscontinuous")){
		mode = ALS_OP_MODE_ALS_CONT;
		isl_data.intr_flag = 0;
	}else if(!strcmp(buf, "ircontinuous")){
		mode = ALS_OP_MODE_IR_CONT;
		isl_data.intr_flag = 1;
	}else {
		mutex_unlock(&isl_data.isl_mutex);
		pr_err( "%s : %s: Invalid mode string"
				"\n", MODULE_NAME, __func__);
		return -1;
	}

	if(isl_set_sensing_mode(mode) < 0){		
		mutex_unlock(&isl_data.isl_mutex);
		pr_err( "%s :%s :Failed to set Sensing mode"
				"\n",MODULE_NAME,__func__);
		return -1;
	}
	if(isl_data.intr_flag){
		if(isl29023_i2c_write_word16(client, LOW_THRESHOLD_LBYTE_REG,
					&isl_data.last_ir_lt))
			pr_err( "%s :%s :Failed to "
					"write i2c-dev\n", MODULE_NAME, __func__);
		if(isl29023_i2c_write_word16(client, HIGH_THRESHOLD_LBYTE_REG,
					&isl_data.last_ir_ht))
			pr_err( "%s :%s :Failed to write "
					" i2c-dev\n",MODULE_NAME, __func__);
	}
	else{
		if(isl29023_i2c_write_word16(client, LOW_THRESHOLD_LBYTE_REG,
					&isl_data.last_als_lt)){
			pr_err( "%s :%s :Failed to write "
					"i2c-dev\n",MODULE_NAME, __func__);
		}
		if(isl29023_i2c_write_word16(client, HIGH_THRESHOLD_LBYTE_REG,
					&isl_data.last_als_ht)){
			pr_err( "%s :%s :Failed to write "
					" i2c-dev\n",MODULE_NAME, __func__);
		}
	}
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);

}

/*
 * @fn          isl_set_sensing_range
 *
 * @brief       This function sets the current optical sensing range of 
 *		sensor device
 *
 * @return      Returns the data on buffer on success otherwise returns 
 *		an error (-EINVAL)
 *
 */

static int32 isl_set_sensing_range(unsigned long val)
{
	int16 reg;
	reg = i2c_smbus_read_byte_data(isl_data.client_data, 
					REG_CMD_2);
	if(reg < 0)
		return -EINVAL;
	reg = ( reg & ISL_OP_SET_MASK ) | val;
	reg = i2c_smbus_write_byte_data(isl_data.client_data, 
					REG_CMD_2, reg);
	if(reg < 0)
		return -EINVAL;
	return reg;
}

/*
 * @fn          autorange
 *
 * @brief       This function processes autoranging of sensor device
 *
 * @return      void
 *
 */

static void autorange(unsigned short val)
{
        uint16_t adc_resolution = 0;
	uint32_t als_range = 0;

        if(get_adc_resolution_bits(&adc_resolution) < 0){
                pr_err( "%s : %s :Failed to read the adc resolution\n",
                                MODULE_NAME, __func__);
                return;
        }
        if(isl_get_sensing_range(&als_range) < 0){
                pr_err( "%s : %s :Failed to read the ALS sensing"
				" range\n", MODULE_NAME, __func__);
                return;
        }
        switch(adc_resolution)
        {
                case 4:
			switch(als_range){
				case 1000: if(val > 0xC)
						   als_range = 0x01;
					   else  break;
					   isl_set_sensing_range(als_range);
					   break;
				case 4000:
					   if(val > 0xC)
						   als_range = 0x02;
					   else if(val < 0x10)
						   als_range = 0x00;
					   else   break;
					   isl_set_sensing_range(als_range);
					   break;
				case 16000:
					   if(val > 0xC)
						   als_range = 0x03;
					   else if(val < 0x8)
						   als_range = 0x01;
					   else break;
					   isl_set_sensing_range(als_range);
					   break;
				case 64000:
					   if(val < 0x8)
						   als_range = 0x02;
					   else break;
					   isl_set_sensing_range(als_range);
					   break;

			}
                        break;
                case 8:
                        switch(als_range){
                                case 1000:
                                        if(val > 0xCC)
                                                als_range = 0x01 ;
                                        else break;

                                        isl_set_sensing_range(als_range);
                                        break;
                                case 4000:
                                        if(val > 0xCC)
                                                als_range = 0x02;
                                        else if(val < 0xC)
                                                als_range = 0x00;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                case 16000:
                                        if(val > 0xCC)
                                            als_range = 0x03;
                                        else if(val < 0xC)
                                            als_range = 0x01;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                case 64000:
                                        if(val < 0xC)
                                                als_range = 0x02;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                        }
                        break;
                case 12:
                        switch(als_range){
                                case 1000:
                                        if(val > 0xCCC)
                                                als_range = 0x01 ;
                                        else break ;
                                        isl_set_sensing_range(als_range);
                                        break;
                                case 4000:
                                        if(val > 0xCCC)
                                                als_range = 0x02;
                                        else if(val < 0xCC)
                                                als_range = 0x00;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                case 16000:
                                        if(val > 0xCCC)
                                                als_range = 0x03;
                                        else if(val < 0xCC)
                                                als_range = 0x01;
					else
                                        	break;
                                 	isl_set_sensing_range(als_range);
                                        break;

                                case 64000:
                                        if(val < 0xCC)
                                                als_range = 0x02;
                                        else   break; 
                                        isl_set_sensing_range(als_range);
                                        break;

                        }
                        break;
                case 16:
                        switch(als_range){
                                case 1000:
                                        if(val > 0xCCCC)
                                                als_range = 0x01 ;
                                        else  break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                case 4000:
                                {
                                        if(val > 0xCCCC)
                                                als_range = 0x02;
                                        else if(val < 0xCCC)
                                                als_range = 0x00;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;

                                }
                                case 16000:
                                {
                                        if(val > 0xCCCC)
                                                als_range = 0x03;
                                        else if(val < 0xCCC)
                                                als_range = 0x01;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                }
                                case 64000:
                                {
                                        if(val < 0xCCC)
                                                als_range = 0x02;
                                        else break;
                                        isl_set_sensing_range(als_range);
                                        break;
                                }
                        }
        }
#ifdef ISL_DBUG
	pr_err("%s: als_range = %x\n", __func__, als_range);
#endif
}
/*
 * @fn          show_sensing_range
 *
 * @brief       This function shows the current optical sensing range
 *		of sensor device
 *
 * @return      Returns the length of data written on buffer on success 
 *		otherwise returns an error (-1)
 *
 */

static ssize_t show_sensing_range(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{	int val;
	int32 ret;
	mutex_lock(&isl_data.isl_mutex);
	ret = i2c_smbus_read_byte_data(isl_data.client_data,REG_CMD_2);
	if(ret < 0){
		mutex_unlock(&isl_data.isl_mutex);	
		pr_err( "%s :%s :Failed to read i2c-dev"
				"\n", MODULE_NAME, __func__);
		return -1;
	}
	ret &= 0x03;
	val = (1 << (2 * (ret & 3)))*1000;
	sprintf(buf ,"%d",val);
	isl_data.range = val;
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          store_sensing_range
 *
 * @brief       This function stores the current optical sensing range 
 *		of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise 
 *		returns an error (-1)
 *
 */

static ssize_t store_sensing_range(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{	int val;
	mutex_lock(&isl_data.isl_mutex);
	if(strict_strtoul(buf, 10,  &isl_data.range))
		return -EINVAL;
	switch(isl_data.range)
	{
		case 1000:val = 0;
			  isl_data.range = 1000;break;
		case 4000:val = 1;
			  isl_data.range = 4000;break;
		case 16000:val = 2;
			  isl_data.range = 16000;break;
		case 64000:val = 3;
			  isl_data.range = 64000;break;
		default: mutex_unlock(&isl_data.isl_mutex);	
			 return -1;
	}
	if(isl_set_sensing_range(val) < 0){
		mutex_unlock(&isl_data.isl_mutex);	
		pr_err( "%s :%s failed\n",MODULE_NAME, __func__);
		return -1;
	}
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*
 * @fn          show_als_current
 *
 * @brief       This function shows the current optical lux of sensor
 *		device
 *
 * @return      Returns data buffer length on success otherwise returns
 *		an error (-1)
 *
 */

static ssize_t show_als_current(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int als_val;
	mutex_lock(&isl_data.isl_mutex);
	if(isl29023_i2c_read_word16(client, ALS_DATA_LSB, &als_val) < 0){
		mutex_unlock(&isl_data.isl_mutex);
		pr_err( "%s :%s :Failed to read ALS data\n",
				MODULE_NAME,__func__);
		return -1;
	}

#ifndef ISL29023_INTERRUPT_MODE
	autorange(asl_val);
#endif
	sprintf(buf,"%d",als_val);
	mutex_unlock(&isl_data.isl_mutex);
	return strlen(buf);
}

/*******************Attributes of ISL29023 ALS Sensor*********/

/* Device attributes for adc resolution sysfs */
static DEVICE_ATTR( adc_res_bits, ISL_SYSFS_PERM, 
		show_adc_res_bits, store_adc_res_bits);
/* Device attributes for sensor range sysfs */
static DEVICE_ATTR( sensing_range, ISL_SYSFS_PERM,
		show_sensing_range, store_sensing_range);
/* Device attributes for sensor operating mode sysfs */
static DEVICE_ATTR( sensing_mode, ISL_SYSFS_PERM, 
		show_sensing_mode, store_sensing_mode);
/* Device attributes for latest IR data sysfs */
static DEVICE_ATTR( ir_current, ISL_SYSFS_PERM,show_ir_current,
		 NULL );
static DEVICE_ATTR( ir_compensation, ISL_SYSFS_PERM,
		show_ir_compensation, NULL);

#ifdef ISL29023_INTERRUPT_MODE 
/* Device attributes for high interrupt threshold sysfs */
static DEVICE_ATTR( intr_threshold_high, ISL_SYSFS_PERM ,
		show_intr_threshold_high, store_intr_threshold_high);
/* Device attributes for Low interrupt threshold sysfs */
static DEVICE_ATTR( intr_threshold_low, ISL_SYSFS_PERM , 
		show_intr_threshold_low, store_intr_threshold_low);
/* Device attributes for interrupt persistency sysfs */
static DEVICE_ATTR( intr_persistency, ISL_SYSFS_PERM ,
		show_intr_persistency, store_intr_persistency);
#endif
/* Device attributes for current ALS data sysfs */
static DEVICE_ATTR( als_current, ISL_SYSFS_PERM, show_als_current, NULL);


/* Structure attributes for all sysfs device files for isl29023 sensor */
static struct attribute *isl29023_attr[] = {

	&dev_attr_adc_res_bits.attr,
	&dev_attr_sensing_range.attr,
	&dev_attr_sensing_mode.attr,
	&dev_attr_ir_current.attr,
	&dev_attr_ir_compensation.attr,
#ifdef ISL29023_INTERRUPT_MODE	
	&dev_attr_intr_threshold_high.attr,
	&dev_attr_intr_threshold_low.attr,
	&dev_attr_intr_persistency.attr,
#endif
	&dev_attr_als_current.attr,
	NULL
};

static struct attribute_group isl29023_attr_grp = {

	.attrs = isl29023_attr
};	 

#ifdef ISL29023_INTERRUPT_MODE
/*
 * @fn          sensor_irq_thread
 *
 * @brief       This thread is scheduled by sensor interrupt
 *
 * @return      void
 */

static void sensor_irq_thread(struct work_struct *work)
{
	int16 reg;
	unsigned int val;
	reg = i2c_smbus_read_byte_data(isl_data.client_data, 
						REG_CMD_1);
	if(reg < 0){
		pr_err( "%s :%s :Failed to read REG_CMD_1"
					"\n",MODULE_NAME, __func__);
		goto err;
	}
	
	if (isl29023_i2c_read_word16(isl_data.client_data, 
				ALS_DATA_LSB, &val) < 0)
		goto err;
	
	autorange(val);		
		
err:
#ifdef ISL_DBUG
	pr_err( "%s :%s :Interrupt Cleared\n", MODULE_NAME,
						 __func__);
#endif
	enable_irq(isl_data.irq_num);		
}

/*
 * @fn          isl_sensor_irq_handler
 *
 * @brief       This function is the interrupt handler for sensor.
 *		It schedules an interrupt thread and returns (free)
 *		interrupt.
 *
 * @return      IRQ_HANDLED
 *
 */

static irqreturn_t isl_sensor_irq_handler(int irq, void *dev_id)
{
	disable_irq_nosync(isl_data.irq_num);
	schedule_work(&isl_data.work);
	return IRQ_HANDLED;
}

#endif

/*
 * @fn          isl_set_default_config
 *
 * @brief       This function initializes the sensor device
 *		with default values
 *
 * @return      returns 0 on success otherwise returns an 
 *		error (-EINVAL)
 *
 */

static int32 isl_set_default_config(struct i2c_client *client)
{

	/* Reset the device to avoid previous saturation */
	if(i2c_smbus_write_byte_data(client, REG_CMD_1, 0x00) < 0)
		return -EINVAL;
	/*
	   Set Operating mode to ALS Continuous
	   Set Interrupt Persistency 16 cycles
	 */

	if(i2c_smbus_write_byte_data(client, REG_CMD_1, 0xa3) < 0) 
		return -EINVAL;

	/*
	   Set ADC Resolution to 16-bit
	   Set Sensing Range to 16000 
	 */

	if(i2c_smbus_write_byte_data(client , REG_CMD_2, 0x03) < 0) 
		return -EINVAL;

#ifdef ISL29023_INTERRUPT_MODE

	/* Writing interrupt low threshold as 0xCCC (5% of max range) */
	if(i2c_smbus_write_byte_data(client, LOW_THRESHOLD_LBYTE_REG,
							 0xCC) < 0)
		return -EINVAL;

	if(i2c_smbus_write_byte_data(client, LOW_THRESHOLD_HBYTE_REG, 
							0x0C) < 0)
		return -EINVAL;

	/* Writing interrupt high threshold as 0xCCCC (80% of max range)  */
	if(i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_LBYTE_REG,
							 0xCC) < 0) 
		return -EINVAL;

	if(i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_HBYTE_REG,
							 0xCC) < 0)
		return -EINVAL;

	isl_data.last_ir_lt = 0xCC;
	isl_data.last_ir_ht = 0x0C;
	isl_data.last_als_lt = 0xCC;
	isl_data.last_als_ht = 0xCC;

#endif
	if(i2c_smbus_read_byte_data(client, REG_CMD_1) < 0)
		return -EINVAL;

#ifdef ISL_DBUG
	pr_err( "%s : Device is configured to default\n",
			MODULE_NAME);
#endif
	return 0;
}

/*
 * @fn          isl_sensor_probe
 *
 * @brief       This function is called by I2C Bus driver on detection
 *		of sensor device.It validates the device and initialize
 *		the resources required by driver. 
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 __devinit isl_sensor_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int32 ret;
	struct isl29023_platform_data *pdata ;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);	

	pdata = client->dev.platform_data;

	if(pdata == NULL){
		pr_err( "%s : %s: Unable to find platform data"
				"\n", MODULE_NAME, __func__);
		return -1;
	}	
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)){
		pr_err("%s :%s :Adapter does not support all i2c" 
			"functionality\n", MODULE_NAME, __func__);
		return -EIO;	
	}
	isl_data.client_data = client;

	/* Initialise the sensor with default configuration */
	ret = isl_set_default_config(client);
	if(ret < 0){
		pr_err( "%s : %s: Failed to set default "
				"configuration\n",MODULE_NAME, __func__);
		return -1;
	}

#ifdef ISL29023_INTERRUPT_MODE
	
	/* Request GPIO for sensor interrupt */
	ret = gpio_request(pdata->gpio_irq, "isl29023");
	if(ret < 0){
		pr_err( "%s : %s failed for %d\n"
				,MODULE_NAME, __func__, pdata->gpio_irq);
		goto err;
	}

	 /*Configure interrupt GPIO direction */
	ret = gpio_direction_input(pdata->gpio_irq);
	if (ret < 0) {
		pr_err( "%s : %s: Failed to set direction"
				"\n", MODULE_NAME, __func__);
		goto gpio_err;
	}

	/* Configure the GPIO for interrupt */
	isl_data.irq_num = gpio_to_irq(pdata->gpio_irq);
	if (isl_data.irq_num < 0){
		pr_err( "%s : %s: Failed to get IRQ number for"
				"\n", MODULE_NAME, __func__);
		goto gpio_err;
	}

	ret = irq_set_irq_type(isl_data.irq_num, IRQ_TYPE_EDGE_FALLING);	
	if(ret < 0){
		pr_err( "Failed to configure the interrupt polarity\n");
		goto gpio_err;
	} 
	
	/* Initialize the work queue */
	INIT_WORK(&isl_data.work,sensor_irq_thread);

	/* Register irq handler for sensor */
	ret = request_irq(isl_data.irq_num, isl_sensor_irq_handler,
			IRQF_TRIGGER_FALLING , "isl29023", NULL);
	if (ret < 0) {
		pr_err( "%s : %s: Failed to register irq %d\n",
				MODULE_NAME, __func__, isl_data.irq_num);
		goto gpio_err;
	}
#endif
	/* Create the sysfs entries */
	ret = sysfs_create_group(&client->dev.kobj, &isl29023_attr_grp);
	if(ret < 0){
		pr_err( "%s :%s : Failed to create sysfs"
				"\n", MODULE_NAME, __func__);
		goto err;
	}

	isl_data.last_mod = 0;
	mutex_init(&isl_data.isl_mutex);
#ifdef ISL_DBUG
	pr_err( "%s : %s :I2C Device probed"
			"\n", MODULE_NAME, __func__);
#endif
	/* Clear any previous interrupt */
	i2c_smbus_read_byte_data(client, REG_CMD_1);

	return 0;
#ifdef ISL29023_INTERRUPT_MODE
gpio_err:
	gpio_free(pdata->gpio_irq);
#endif
err:
	return -1;
}

/*
 * @fn          isl_sensor_suspend
 *
 * @brief       This function puts the sensor device in standby mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_sensor_suspend(struct i2c_client *client,
		pm_message_t msg)
{
	int16 reg;
	reg = i2c_smbus_read_byte_data(client, REG_CMD_1);                                                                                            
	if(reg < 0){
		pr_err( "%s :%s :Failed to read i2c-dev"
				"\n",MODULE_NAME, __func__);                                                  
		goto err;
	}

	isl_data.last_mod = reg;
	reg = (reg & ALS_OP_MODE_CLEAR) | ALS_OP_MODE_PWDN_SET;    

	/* Put the sensor device in power down mode mode */
	if(i2c_smbus_write_byte_data(client, REG_CMD_1, reg) < 0){
		pr_err( "%s :%s :Failed to write i2c-dev"
				"\n", MODULE_NAME, __func__);                                               
		goto err;
	}
	pr_err( "%s :%s :Sensor suspended"
			"\n", MODULE_NAME, __func__);	

#ifdef ISL29023_INTERRUPT_MODE
	disable_irq_nosync(isl_data.irq_num);
#endif
	return 0;
err:
	return -1;
}

/*
 * @fn          isl_sensor_resume
 *
 * @brief       This function Resumes the sensor device from suspend mode
 *	        and puts it in Active conversion mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 isl_sensor_resume(struct i2c_client *client)
{
	int16 reg;

	reg = i2c_smbus_read_byte_data(client, REG_CMD_1);
	if(reg < 0){
		pr_err( "%s :%s :Failed to read i2c-dev\n",
				MODULE_NAME, __func__);
		goto err;
	}
	reg |= isl_data.last_mod;

	/* Put the sensor device in active conversion mode */
	if(i2c_smbus_write_byte_data(client, REG_CMD_1, reg) < 0){
		pr_err( "%s :%s :Failed to write i2c-dev"
				"\n", MODULE_NAME, __func__);
		goto err;
	}
	pr_err( "%s :%s :Sensor resumed\n",
				MODULE_NAME,__func__);

#ifdef ISL29023_INTERRUPT_MODE	
	enable_irq(isl_data.irq_num);
#endif
	return 0;
err:
	return -1;

}

/*
 * @fn          isl_sensor_remove
 *
 * @brief       This function is called when sensor device gets removed 
 *		from bus
 *
 * @return      Returns 0
 *
 */


static int32 __devexit isl_sensor_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj,&isl29023_attr_grp);

#ifdef ISL29023_INTERRUPT_MODE	
	free_irq(isl_data.irq_num,NULL);
	gpio_free(ISL29023_INTR_GPIO);
#endif
	return 0;
}

/* Driver information sent to kernel*/
static struct i2c_driver isl_sensor_driver = {
	.driver = {
		.name = "isl29023",
		.owner = THIS_MODULE,
	},
	.probe		=  isl_sensor_probe,
	.remove		=  isl_sensor_remove,
	.id_table 	=  isl_device_table,
	.suspend  	=  isl_sensor_suspend,
	.resume		=  isl_sensor_resume,

};

/*
 *  @fn          isl29023_init
 *
 *  @brief       This function initializes the driver
 *
 *  @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32 __init isl29023_init(void)
{
	return i2c_add_driver(&isl_sensor_driver);

}

/*
 * @fn          isl29023_exit
 *
 * @brief       This function is called to cleanup driver entry
 *
 * @return      Void
 *
 */
static void __exit isl29023_exit(void)
{
	i2c_del_driver(&isl_sensor_driver);
}


MODULE_AUTHOR ("sanoj.kumar@vvdntech.com");
MODULE_LICENSE ("GPLv2");
MODULE_DESCRIPTION ("Driver for ISL29023 ALS Sensor");

module_init(isl29023_init);
module_exit(isl29023_exit);