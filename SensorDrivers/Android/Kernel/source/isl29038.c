/****************************************************************************
 *      
 *      File            : isl29038.c
 *      
 *      Description     : Device Driver for ISL29038 ALS Sensor
 *
 *      License         : GPLv2
 *      
 *      Copyright       : Intersil Corporation (c) 2013 
 *
 *       
 *                      by Sanoj Kumar <sanoj.kumar@vvdntech.com>
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
 *                      
 *****************************************************************************/ 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/isl29038.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/irq.h>
#include <linux/delay.h>

#ifndef ISL29038_INTERRUPT_MODE
#define ISL29038_INTERRUPT_MODE
#endif

/* private members of this sensor*/
static struct isl29038_data {
	struct 	mutex lock;
	struct 	work_struct work;
	struct 	kset *isl_kset;
	struct 	kobject *isl_kobj;
	struct 	i2c_client *isl_client;
#ifdef ISL29038_INTERRUPT_MODE
	uint32_t irq_num;
#endif
	uint16_t als_mode;
	uint16_t prox_mode;
} pri_data;

/* Global Client Structure */
static struct i2c_device_id isl_device_id[] = {
	{"isl29038", ISL29038_I2C_ADDR},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl_device_id);

/*
 * @fn          isl29038_i2c_read_word16
 *
 * @brief       This wrapper function reads a word (16-bit) from 
 *              sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */


static int32_t isl29038_i2c_read_word16(struct i2c_client *client,
                                 unsigned char reg_addr, uint16_t *buf)
{
        int16_t reg_h;
        int16_t reg_l;

        reg_l = i2c_smbus_read_byte_data(client, reg_addr);
        if (reg_l < 0)
                return -1;
        reg_h = i2c_smbus_read_byte_data(client, reg_addr - 1);
        if (reg_h < 0)
                return -1;
        *buf = (reg_h << 8) | reg_l;
        return 0;
}

/*
 * @fn          show_prox_mode
 *
 * @brief       This function shows the current proximity sensing mode
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_prox_mode(struct kobject *kobj, 
		struct kobj_attribute *attr, char *buf)
{	
	uint16_t reg;

	mutex_lock(&pri_data.lock);	
	reg = i2c_smbus_read_byte_data(pri_data.isl_client,CONFIG_REG_0);	
	if(reg < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return sprintf(buf , "%s", (reg & R_PROX_MODE_MASK) ? "enable":"disable");

}

/*
 * @fn          store_prox_mode
 *
 * @brief       This function stores the enable/disable proximity sensing mode
 *              of sensor device
 *                            
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t store_prox_mode(struct kobject *kobj, 
		struct kobj_attribute *attr, const char *buf,
				size_t count)
{
	uint16_t mod;
	int16_t reg;

	mutex_lock(&pri_data.lock);	
	if(!strcmp(buf, "enable"))
		mod = ISL29038_PROX_ENABLE;
	else if(!strcmp(buf, "disable"))
		mod = ISL29038_PROX_DISABLE;
	else {
		__dbg_invl_err("%s",__func__);		
		goto err_out;
	}
	reg = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
	if(reg < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	reg = (reg & W_PROX_MODE_MASK) | mod;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_0 ,reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}			
	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);
	return -1;
	
}

#ifdef ISL29038_INTERRUPT_MODE

/*
 * @fn          show_prox_persist
 *
 * @brief       This function displays the current interrupt
 *              persistency of Proximity sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_prox_persist(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t p_perst;

	mutex_lock(&pri_data.lock);
	p_perst = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
	if(p_perst < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	switch((p_perst & R_PROX_PERSIST_MASK) >> ISL_PROX_INTR_PERSIST_SHIFT)
	{
		case 0x00 :sprintf(buf, "%d", 1); break;	 
		case 0x01 :sprintf(buf, "%d", 2); break;
		case 0x02 :sprintf(buf, "%d", 4); break;
		case 0x03 :sprintf(buf, "%d", 8); break;
	}	
	mutex_unlock(&pri_data.lock);
	return strlen(buf);	

}

/*
 * @fn          store_prox_persist
 *
 * @brief       This function writes the interrupt
 *              persistency of prox sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_prox_persist(struct kobject *kobj, 
		struct kobj_attribute *attr, const char *buf, 
				size_t count)
{
	int32_t p_perst ;
	int16_t reg;
	mutex_lock(&pri_data.lock);
	p_perst = simple_strtoul(buf, NULL, 10);		
	switch(p_perst)
	{
	case 1: p_perst = ISL_PROX_INTR_PERSIST_SET_1;break;
	case 2: p_perst = ISL_PROX_INTR_PERSIST_SET_2;break;
	case 4: p_perst = ISL_PROX_INTR_PERSIST_SET_4;break;
	case 8: p_perst = ISL_PROX_INTR_PERSIST_SET_8;break;
	default: 
		__dbg_invl_err("%s",__func__);		
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	
	reg = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
	if(reg < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	p_perst = (reg & W_PROX_PERSIST_MASK) | p_perst;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_3, p_perst) < 0){
		__dbg_write_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}			
	mutex_unlock(&pri_data.lock);
	return strlen(buf);	
}

/*
 * @fn          show_prox_lt
 *
 * @brief       This function shows the low proximity interrupt threshold
 *              value of 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_prox_lt(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t p_lt;	

	mutex_lock(&pri_data.lock);
	p_lt = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_PROX_INT_TL);
	if(p_lt < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}	
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", p_lt);
}

/*
 * @fn          store_prox_lt
 *
 * @brief       This function writes the low interrupt threshold
 *              value in 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_prox_lt(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	int32_t p_lt;

	mutex_lock(&pri_data.lock);			
	p_lt = simple_strtoul(buf, NULL, 10);
	if(p_lt < 0 || p_lt > 255){
		__dbg_invl_err("%s",__func__);		
		return -1;
	}
	if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_PROX_INT_TL, p_lt) < 0){
		__dbg_write_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);

}

/*
 * @fn          show_prox_ht
 *
 * @brief       This function shows the high interrupt threshold
 *              value of 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_prox_ht(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t p_ht;	

	mutex_lock(&pri_data.lock);
	p_ht = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_PROX_INT_TH);
	if(p_ht < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}	
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", p_ht);

	
}

/*
 * @fn          store_prox_ht
 *
 * @brief       This function writes the high interrupt threshold
 *              value in 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_prox_ht(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	int32_t p_ht;
	mutex_lock(&pri_data.lock);			
	p_ht = simple_strtoul(buf, NULL, 10);
	if(p_ht < 0 || p_ht > 255){
		__dbg_invl_err("%s",__func__);		
		return -1;
	}
	if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_PROX_INT_TH, p_ht) < 0){
		__dbg_write_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);

}
#endif

/*
 * @fn          show_prx_ambir_stat
 *
 * @brief       This function shows the current proximity status
 *              of sensor device
 *
 * @return      Returns the length of the buffer on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_prx_ambir_stat(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t prx_st;
	mutex_lock(&pri_data.lock);	
	prx_st = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_PROX_AMBIR_REG);
	if(prx_st < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	if(prx_st & 0x01)
		sprintf(buf, "%s\n", "Prox-Washout");
	else 
		sprintf(buf, "%s\n", "Normal Operation");
	mutex_unlock(&pri_data.lock);	
	return strlen(buf);

}

/*
 * @fn          store_p_sleep_uS
 *
 * @brief       This function writes the proximity sleep to the device
 *              
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_p_sleep_uS(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t sleep_t;

	mutex_lock(&pri_data.lock);
	sleep_t = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
	if(sleep_t < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	
	/* All readings are in micro second range */
	sleep_t = (sleep_t & R_PROX_SLP_MASK) >> ISL29038_PROX_SLP_SHIFT;
	switch(sleep_t)
	{
		case 0x00:sprintf(buf ,"%s","400000");break;
		case 0x01:sprintf(buf ,"%s","100000");break;
		case 0x02:sprintf(buf ,"%s","50000");break;
		case 0x03:sprintf(buf ,"%s","25000");break;
		case 0x04:sprintf(buf ,"%s","12500");break;
		case 0x05:sprintf(buf ,"%s","6250");break;
		case 0x06:sprintf(buf ,"%s","3125");break;
		case 0x07:sprintf(buf ,"%s","0");break;
	}
	mutex_unlock(&pri_data.lock);	
	return strlen(buf);
}


/*
 * @fn          store_p_sleep_uS
 *
 * @brief       This function writes the proximity sleep to the device
 *              
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_p_sleep_uS(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	int16_t reg, ret;
	ulong sleep_t;
	mutex_lock(&pri_data.lock);

	if (strict_strtoul(buf, 10, &sleep_t)){
		__dbg_invl_err("%s", __func__);
		goto err_out;		
	}
	
	switch(sleep_t)	 
	{
		case 400000  	:reg = ISL29038_PROX_SLP_400ms;break;
		case 100000  	:reg = ISL29038_PROX_SLP_100ms;break;
		case 50000   	:reg = ISL29038_PROX_SLP_50ms;break;
		case 25000   	:reg = ISL29038_PROX_SLP_25ms;break;
		case 12500  	:reg = ISL29038_PROX_SLP_12500us;break;
		case 6250  	:reg = ISL29038_PROX_SLP_6250us;break;
		case 3125 	:reg = ISL29038_PROX_SLP_3125us;break;
		case 0    	:reg = ISL29038_PROX_SLP_0ms;break;
		default   	:__dbg_invl_err("%s", __func__);goto err_out;
	}

	ret = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
	if(ret < 0){
		__dbg_read_err("%s",__func__);	
		goto err_out;
	}
	reg = (ret & W_PROX_SLP_MASK) | reg;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_0, reg) < 0){
		__dbg_write_err("%s",__func__);	
		goto err_out;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);

err_out:
	mutex_unlock(&pri_data.lock);
	return -1;
}

/*
 * @fn          store_ir_curr_uA
 *
 * @brief       This function writes the IR drive current to device
 *              
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_curr_uA(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t ir_cur;
	mutex_lock(&pri_data.lock);
	ir_cur = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
	if(ir_cur < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	switch(ir_cur & 0x03)
	{
	case 0x00:sprintf(buf, "%s","31250");break;
	case 0x01:sprintf(buf, "%s","62500");break;
	case 0x02:sprintf(buf, "%s","125000");break;
	case 0x03:sprintf(buf, "%s","250000");break;
	default:goto err_out;
	}	
	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out: 
	mutex_unlock(&pri_data.lock);
	return -1;
}


/*
 * @fn          store_ir_curr_uA
 *
 * @brief       This function writes the IR drive current to device
 *              
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_ir_curr_uA(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	uint16_t reg, ret;
	ulong val;

	mutex_lock(&pri_data.lock);
	if(strict_strtoul(buf, 10, &val ))
		goto err_out;

	/* Input values are in micro Ampere scale and they are processed as milliAmpere scale */
	switch(val)
	{
		case 31250 	: reg = ISL29038_IRDR_DRV_31250uA; break;
		case 62500 	: reg = ISL29038_IRDR_DRV_62500uA; break;
		case 125000  	: reg = ISL29038_IRDR_DRV_125mA;   break;
		case 250000  	: reg = ISL29038_IRDR_DRV_250mA;   break;
		default   	:
			    __dbg_invl_err("%s",__func__);		
			    goto err_out; 
	}
	ret = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	reg = (ret & W_IR_LED_CURR_MASK) | reg;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_0, reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}	
	mutex_unlock(&pri_data.lock);
	return strlen(buf);

err_out:
	mutex_unlock(&pri_data.lock);	
	return -1;

}

/*
 * @fn          show_intr_algorithm
 *
 * @brief       This function reads the interrupt algorithm to from device
 *              
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_algorithm(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int16_t intr_alg;	
	mutex_lock(&pri_data.lock);

	intr_alg = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);	
	if(intr_alg < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	if((intr_alg & R_INTR_ALGO_MASK) >> ISL_INT_ALGO_SHIFT)
		sprintf(buf, "%s\n","hysteresis");
	else
		sprintf(buf, "%s\n","window");	
	mutex_unlock(&pri_data.lock);	
	return strlen(buf);
		
err_out:
	mutex_unlock(&pri_data.lock);	
	return -1;
}

/*
 * @fn          store_intr_algorithm
 *
 * @brief       This function writes the interrupt algorithm to the device
 *              
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_intr_algorithm(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	int16_t  ret;	
	uint16_t intr_alg;
	mutex_lock(&pri_data.lock);		
	if(!strcmp(buf, "window"))
		intr_alg = ISL_INT_ALGO_WINDOW_COMP;
	else if(!strcmp(buf, "hysteresis"))
		intr_alg = ISL_INT_ALGO_HYSTER_WIND;
	else{
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
	ret = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	intr_alg = (ret & W_INTR_ALGO_MASK) | intr_alg;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1, intr_alg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}
	mutex_unlock(&pri_data.lock);	
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);	
	return -1;

}

/*
 * @fn          show_prox_data
 *
 * @brief       This function shows the proximity data sensed by sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_prox_data(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	int16_t p_data;

	mutex_lock(&pri_data.lock);
	p_data = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_PROX_DATA_REG);
	if(p_data < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", p_data);

}

/*
 * @fn          show_als_mode
 *
 * @brief       This function shows the als mode status sensed by sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_als_mode(struct kobject *kobj, 
		struct kobj_attribute *attr, char *buf)
{
	int16_t mode;
	mutex_lock(&pri_data.lock);	
	mode = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);
	if(mode < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	mutex_unlock(&pri_data.lock);	
	return sprintf(buf, "%s\n", ((mode & 0x04) >> 2)?"enable":"disable");
	
err_out:
	mutex_unlock(&pri_data.lock);	
	return -1;
	
}

/*
 * @fn          store_als_mode
 *
 * @brief       This function stores the als mode status sensed by sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t store_als_mode(struct kobject *kobj, 
		struct kobj_attribute *attr, const char *buf,
				size_t count)
{
	int16_t mode, reg = 0;

	mutex_lock(&pri_data.lock);			
	if(!strcmp(buf, "enable"))
		reg = ISL_ALS_ENABLE;
	else if(!strcmp(buf, "disable"))
		reg = ISL_ALS_DISABLE;
	else{ 	
		__dbg_invl_err("%s",__func__);
		goto in_err;
	}
	mode = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);
	if(mode < 0){
		__dbg_read_err("%s", __func__);
		goto in_err;
	}
	mode = (mode & W_ALS_MODE_MASK) | reg;
	if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1, mode) < 0){
		__dbg_write_err("%s", __func__);
		goto in_err;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);

in_err:
	mutex_unlock(&pri_data.lock);
	return -1;

}

/*
 * @fn          show_als_range
 *
 * @brief       This function shows the current optical sensing range
 *              of sensor device
 *
 * @return      Returns the range on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_als_range(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	uint16_t range;
	mutex_lock(&pri_data.lock);
	range = i2c_smbus_read_byte_data(pri_data.isl_client,REG_CONFIG_1);
	if(range < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	switch(range & 0x03)
	{
	case 0x00:sprintf(buf,"%d",125);break;
	case 0x01:sprintf(buf,"%d",250);break;
	case 0x02:sprintf(buf,"%d",2000);break;
	case 0x03:sprintf(buf,"%d",4000);break;
	default :goto err_out;
	}

	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);
	return -1;

}

/*
 * @fn          store_als_range
 *
 * @brief       This function writes the optical sensing ranges
 *              of sensor device
 *                          
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t store_als_range(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	long range;
	uint32_t reg, ret;
	mutex_lock(&pri_data.lock);
	if(strict_strtoul(buf, 10, &range ))
		goto err_out;
	if(range < 0 || range > 4095){
		__dbg_invl_err("%s",__func__);
		goto err_out;
	}
	switch(range)
	{
		case 125:reg = ISL_ALS_RANGE_125Lux;break;
		case 250:reg = ISL_ALS_RANGE_250Lux;break;
		case 2000:reg = ISL_ALS_RANGE_2000Lux;break;
		case 4000:reg = ISL_ALS_RANGE_4000Lux;break;
		default :__dbg_invl_err("%s",__func__);
			 goto err_out;break;
	}		
	ret = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);		
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	reg = (ret & R_ALS_RANGE_MASK) | reg ;	
	if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1, reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);
	return -1;
}

/*
 * @fn          show_als_data
 *               
 * @brief       This function shows the als data sensed by sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_als_data(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	uint16_t reg;
	
	mdelay(20);
	mutex_lock(&pri_data.lock);
	if(isl29038_i2c_read_word16(pri_data.isl_client, 
				ISL_ALS_DATA_LBYTE, &reg) < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}	
	mutex_unlock(&pri_data.lock);
	/* Mask the upper nibble */
	return sprintf(buf, "%d",(reg & 0x0fff)); 
}

/*
 * @fn          show_alsir_compensation
 *               
 * @brief       This function shows the alsir compensation sensed 
 *              
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_alsir_compensation(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{	
	uint16_t alsir_comp;

	mutex_lock(&pri_data.lock);
	alsir_comp = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_2);		
	if(alsir_comp < 0){
		__dbg_read_err("%s",__func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", alsir_comp & ISL_ALSIR_COMP_MASK);
}

/*
 * @fn          store_alsir_compensation
 *               
 * @brief       This function stores the alsir compensation sensed 
 *              
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t store_alsir_compensation(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, 
			size_t count)
{
	ulong alsir_comp;

	mutex_lock(&pri_data.lock);
	if(strict_strtoul(buf, 10, &alsir_comp)){
		__dbg_invl_err("%s", __func__);
                goto err_out;
	}
        if(alsir_comp < 0 || alsir_comp > 31){
		__dbg_invl_err("%s", __func__);
                goto err_out;
        }
	if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_2,
					 alsir_comp) < 0){
		__dbg_write_err("%s", __func__);
                goto err_out;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);
	return -1;

}

#ifdef ISL29038_INTERRUPT_MODE
/*
 * @fn          show_als_low_thres
 *
 * @brief       This function shows the low interrupt threshold
 *              value of 12-bit register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_als_low_thres(struct kobject *kobj, 
		struct kobj_attribute *attr, char *buf)
{	
	uint16_t reg, reg_l, reg_h;

        mutex_lock(&pri_data.lock);
	/* Read the LSB of Low threshold */
	reg_l = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_ALS_INT_TL0);
	if( reg_l < 0){
                __dbg_read_err("%s", __func__);
               goto err_out; 
        }
	reg_l &= ISL_LT_MASK; 		/* Mask the lower nibble */
	/* Read the MSB of Low threshold */
	reg_h = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_ALS_INT_TL1);
        if( reg_l < 0){
                __dbg_read_err("%s", __func__);
               goto err_out; 
        }
	reg = (reg_h << 8) | reg_l;
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", (reg >> 4));
err_out:
        mutex_unlock(&pri_data.lock);
        return -1;
	
}

/*
 * @fn          store_als_low_thres
 *
 * @brief       This function stores the low alsir interrupt threshold
 *              value of 12-bit register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_als_low_thres(struct kobject *kobj, 
		struct kobj_attribute *attr, const char *buf,
			 size_t count)
{
	int32_t reg;
        bytes_r ret;
        uint16_t reg_h;
        uint16_t reg_l;

        mutex_lock(&pri_data.lock);
        reg = simple_strtoul(buf, NULL, 10);
        if (reg < 0 || reg > 4095){
                __dbg_invl_err("%s", __func__);
                goto err_out;
        }
        ret = i2c_smbus_read_byte_data(pri_data.isl_client,ISL_ALS_INT_TL0);
        if(ret < 0){
                __dbg_read_err("%s", __func__);
                goto err_out;
        }
	reg <<= 4;
        /* Extract LSB and MSB bytes from data */
        reg_l = reg & 0xFF;
        reg_h = (reg & 0xFF00) >> 8;
        reg_l = (ret & 0x0f) | reg_l;

        /* Write Low byte threshold at 0x08h */
        if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_ALS_INT_TL0, reg_l) < 0){
                __dbg_write_err("%s", __func__);
                goto err_out;
        }
        /* Write high byte threshold at 0x09h retaining upper nibble */
        if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_ALS_INT_TL1, reg_h) < 0){
                __dbg_write_err("%s", __func__);
                goto err_out;
        }
        mutex_unlock(&pri_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&pri_data.lock);
        return -1;

}

/*
 * @fn          show_als_high_thres
 *
 * @brief       This function shows the high alsir interrupt threshold
 *              value of 12-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_als_high_thres(struct kobject *kobj, 
		struct kobj_attribute *attr, char *buf)
{
	int16_t reg;
        uint16_t reg_l;
        uint16_t reg_h;

        mutex_lock(&pri_data.lock);
        reg_l = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_ALS_INT_TH0);
        if (reg_l < 0){
                __dbg_read_err("%s", __func__);
               goto err;
        }
        reg_h = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_ALS_INT_TH1);
        if (reg_l < 0){
                __dbg_read_err("%s", __func__);
               goto err;
        }
        reg_h &= ISL_HT_MASK;  		/* Mask the lower nibble */
        reg = (reg_h << 8) | reg_l;

        mutex_unlock(&pri_data.lock);
        return sprintf(buf, "%d", reg);
err:
        mutex_unlock(&pri_data.lock);
        return -1;

}

/*
 * @fn          store_als_high_thres
 *
 * @brief       This function stores the high interrupt threshold
 *              value in 12-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_als_high_thres(struct kobject *kobj, 
		struct kobj_attribute *attr, const char *buf,
			 size_t count)
{
	int32_t reg;
        bytes_r ret;
        uint16_t reg_h;
        uint16_t reg_l;

        mutex_lock(&pri_data.lock);
        reg = simple_strtoul(buf, NULL, 10);
        if (reg < 0 || reg > 4095){
                __dbg_invl_err("%s", __func__);
                goto err_out;
        }
        ret = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_ALS_INT_TH1);
        if(ret < 0){
                __dbg_read_err("%s", __func__);
                goto err_out;
        }
        /* Extract LSB and MSB bytes from input */
        reg_l = reg & 0xFF;
        reg_h = (reg & 0xFF00) >> 8;
        reg_h = (ret & 0xf0) | reg_h;

        /* Write Low byte threshold at 0x09h */
        if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_ALS_INT_TH0, reg_l) < 0){
                __dbg_read_err("%s", __func__);
                goto err_out;
        }
        /* Write high byte threshold at 0x08h retaining upper nibble */
        if(i2c_smbus_write_byte_data(pri_data.isl_client, ISL_ALS_INT_TH1, reg_h) < 0){
                __dbg_write_err("%s", __func__);
                goto err_out;
        }
        mutex_unlock(&pri_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&pri_data.lock);
        return -1;

}

/*
 * @fn          show_als_persist
 *
 * @brief       This function displays the current interrupt
 *              persistency of PROX sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_als_persist(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
       int16_t a_perst;

        mutex_lock(&pri_data.lock);
        a_perst = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
        if (a_perst < 0){
                __dbg_read_err("%s", __func__);
                mutex_unlock(&pri_data.lock);
                return -1;
        }
        switch((a_perst & R_ALS_PERST_MASK) >> ISL_ALS_INTR_PERSIST_SHIFT)
        {
                case 0x00 :sprintf(buf, "%d", 1); break;
                case 0x01 :sprintf(buf, "%d", 2); break;
                case 0x02 :sprintf(buf, "%d", 4); break;
                case 0x03 :sprintf(buf, "%d", 8); break;
                default :
                mutex_unlock(&pri_data.lock);
		return -1;
        }
        mutex_unlock(&pri_data.lock);
        return strlen(buf);
 
}

/*
 * @fn          store_als_persist
 *
 * @brief       This function writes the current interrupt
 *              persistency in PROX sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_als_persist(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf,
                         size_t count)                  
{
	int32_t a_perst ;
        uint16_t reg;
        mutex_lock(&pri_data.lock);
        a_perst = simple_strtoul(buf, NULL, 10);
        switch(a_perst)
        {
        case 1: a_perst = ISL_ALS_INTR_PERSIST_SET_1;break;
        case 2: a_perst = ISL_ALS_INTR_PERSIST_SET_2;break;
        case 4: a_perst = ISL_ALS_INTR_PERSIST_SET_4;break;
        case 8: a_perst = ISL_ALS_INTR_PERSIST_SET_8;break;
        default:
                __dbg_invl_err("%s",__func__);
		goto err_out;
        }

        reg = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
        if(reg < 0){
                __dbg_read_err("%s", __func__);
		goto err_out;
        }
        a_perst = (reg & W_ALS_PERST_MASK) | a_perst;

        if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_3, a_perst) < 0){
                __dbg_write_err("%s", __func__);
		goto err_out;
        }
        mutex_unlock(&pri_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&pri_data.lock);
        return -1;

}
#endif

/*
 * @fn          show_prox_offset_comp
 *               
 * @brief       This function shows the prox compensation 
 *              
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_prox_offset_comp(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	uint16_t prx_comp;
	
        mutex_lock(&pri_data.lock);
        prx_comp = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);
        if(prx_comp < 0){
                __dbg_read_err("%s",__func__);
                mutex_unlock(&pri_data.lock);
                return -1;
        }
        mutex_unlock(&pri_data.lock);
        return sprintf(buf, "%d", (prx_comp & R_PROX_OFFST_MASK) >> 3);

}

/*
 * @fn          store_prox_offset_comp
 *               
 * @brief       This function swrites the prox compensation 
 *              
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t store_prox_offset_comp(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf,
                         size_t count)
{
	long prox_comp;
        mutex_lock(&pri_data.lock);
        if(strict_strtoul(buf, 10, &prox_comp) < 0)
                goto err_out;
        if(prox_comp < 0 || prox_comp > 15){
                __dbg_invl_err("%s", __func__);
                goto err_out;
        }
	 
        if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1,
                                         (prox_comp & W_PROX_OFFST_COMP_MASK)) < 0){
                __dbg_read_err("%s",__func__);
		goto err_out;
        }
        mutex_unlock(&pri_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&pri_data.lock);
        return -1;

}

/*
 * @fn          show_dev_status
 *
 * @brief       This function shows the current device status
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_dev_status(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
	uint16_t ret;

        mutex_lock(&pri_data.lock);
        ret = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
        if(ret < 0){
                __dbg_read_err("%s",__func__);
                mutex_unlock(&pri_data.lock);
                return -1;
        }
	switch((ret & 0x10) >> 4)
	{
		case 0x00: sprintf(buf, "%s", "Normal Operation");break;
		case 0x01: sprintf(buf, "%s", "Brown-Out Detected");break;
	}
        mutex_unlock(&pri_data.lock);
	return strlen(buf);
}

/*
 * @fn          store_reset
 *
 * @brief       This function resets the sensor device
 *              
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t store_reset(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf,
                         size_t count)
{
	long val;	
	mutex_lock(&pri_data.lock);
	if(strict_strtoul(buf, 10, &val) < 0)
                goto err_out;
        if(val != 38){
                __dbg_invl_err("%s:<valid:38>", __func__);
                goto err_out;
        }
	if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_4, ISL_SOFT_RESET) < 0){
		__dbg_write_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&pri_data.lock);
	return -1;
	
}

/*
 * @fn          show_prox_ambir_data
 *
 * @brief       This function shows the current proximity ambient ir data
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_prox_ambir_data(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	uint16_t val;
	mutex_lock(&pri_data.lock);
	val = i2c_smbus_read_byte_data(pri_data.isl_client, ISL_PROX_AMBIR_REG);
	if(val < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&pri_data.lock);
		return -1;
	}
	mutex_unlock(&pri_data.lock);
	return sprintf(buf, "%d", (val & R_PROX_AMBIR_DATA_MASK) >> 1);

}

/* Proximity attributes */
static struct kobj_attribute prox_mode_attribute =
__ATTR(prox_mode, ISL29038_SYSFS_PERM, show_prox_mode, store_prox_mode);

static struct kobj_attribute p_sleep_uS_attribute = 
__ATTR(p_sleep_uS, ISL29038_SYSFS_PERM, show_p_sleep_uS, 
				store_p_sleep_uS);

#ifdef ISL29038_INTERRUPT_MODE
static struct kobj_attribute prox_persist_attribute = 
__ATTR(prox_persist, ISL29038_SYSFS_PERM, show_prox_persist,
					store_prox_persist);	
static struct kobj_attribute prox_lt_attribute = 
__ATTR(prox_lt, ISL29038_SYSFS_PERM, show_prox_lt, store_prox_lt);

static struct kobj_attribute prox_ht_attribute = 
__ATTR(prox_ht, ISL29038_SYSFS_PERM, show_prox_ht, store_prox_ht);

#endif
static struct kobj_attribute ir_curr_uA_attribute = 
__ATTR(ir_curr_uA, ISL29038_SYSFS_PERM, show_ir_curr_uA,
				 store_ir_curr_uA);

static struct kobj_attribute prx_ambir_stat_attribute = 
__ATTR(prx_ambir_stat, ISL29038_SYSFS_PERM, show_prx_ambir_stat, NULL);

static struct kobj_attribute prox_offset_comp_attribute = 
__ATTR(prox_offset_comp, ISL29038_SYSFS_PERM, show_prox_offset_comp,
	
				store_prox_offset_comp);
static struct kobj_attribute prox_data_attribute = 
__ATTR(prox_data, ISL29038_SYSFS_PERM, show_prox_data, NULL);

static struct kobj_attribute prox_ambir_data_attribute = 
__ATTR(prox_ambir_data, 0666, show_prox_ambir_data, NULL);

/* ALS attributes */
static struct kobj_attribute als_mode_attribute = 
__ATTR(als_mode, ISL29038_SYSFS_PERM, show_als_mode, 
				store_als_mode);
static struct kobj_attribute intr_algo_attribute = 
__ATTR(intr_algo, ISL29038_SYSFS_PERM, show_intr_algorithm,
				 store_intr_algorithm);
static struct kobj_attribute als_data_attribute = 
__ATTR(als_data, ISL29038_SYSFS_PERM, show_als_data, NULL);

static struct kobj_attribute alsir_compensation_attribute = 
__ATTR(alsir_compensation, ISL29038_SYSFS_PERM, show_alsir_compensation,
				store_alsir_compensation); 
static struct kobj_attribute als_range_attribute = 
__ATTR(als_range, ISL29038_SYSFS_PERM, show_als_range, 
				store_als_range);
static struct kobj_attribute dev_status_attribute = 
__ATTR(dev_status, ISL29038_SYSFS_PERM, show_dev_status, NULL); 

static struct kobj_attribute reset_attribute = 
__ATTR(reset, 0666, NULL, store_reset);

#ifdef ISL29038_INTERRUPT_MODE

static struct kobj_attribute als_low_thres_attribute = 
__ATTR(als_low_thres,ISL29038_SYSFS_PERM, show_als_low_thres,
		store_als_low_thres );
static struct kobj_attribute als_high_thres_attribute = 
__ATTR(als_high_thres,ISL29038_SYSFS_PERM, show_als_high_thres,
		store_als_high_thres );
static struct kobj_attribute als_persist_attribute = 
__ATTR(als_persist, ISL29038_SYSFS_PERM, show_als_persist,
					store_als_persist);	
#endif

static struct attribute *isl29038_attrs[] = {
	/* Proximity attributes */
	&prox_mode_attribute.attr,
	&p_sleep_uS_attribute.attr,
#ifdef ISL29038_INTERRUPT_MODE
	&prox_persist_attribute.attr,
	&prox_lt_attribute.attr,
	&prox_ht_attribute.attr,
#endif
	&prox_ambir_data_attribute.attr,
	&ir_curr_uA_attribute.attr,
	&prox_offset_comp_attribute.attr,
	&prox_data_attribute.attr,
	&prx_ambir_stat_attribute.attr,

	/* ALS attributes */
	&intr_algo_attribute.attr,
	&als_mode_attribute.attr,
	&als_range_attribute.attr,
	&als_data_attribute.attr,
	&alsir_compensation_attribute.attr,
#ifdef ISL29038_INTERRUPT_MODE
	&als_persist_attribute.attr,
	&als_low_thres_attribute.attr,
	&als_high_thres_attribute.attr,
#endif
	&dev_status_attribute.attr, 		/* Brown out or normal operation */
	&reset_attribute.attr,
	
	NULL

};

static struct attribute_group isl29038_attr_grp = {
	.attrs = isl29038_attrs,

};

#ifdef ISL29038_INTERRUPT_MODE
/*
 * @fn          isl29038_irq_thread
 *
 * @brief       This work thread is scheduled by sensor interrupt handler
 *              to clear the interrupt flags of sensor
 *
 * @return      void
 */

static void isl29038_irq_thread(struct work_struct *work)
{
	short int ret;

        ret = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_3);
        if(ret < 0){
                __dbg_read_err("%s", __func__);
                goto err;
        }
        if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_3,
                                		(ret & 0x67)) < 0){
                __dbg_write_err("%s", __func__);
                goto err;
        }
err:
        enable_irq(pri_data.irq_num);
}

/*
 * @fn          isl29038_irq_handler
 *
 * @brief       This function is the interrupt handler for sensor.
 *              It schedules an interrupt thread and returns (free)
 *              interrupt.
 *
 * @return      IRQ_HANDLED
 *
 */

static irqreturn_t isl29038_irq_handler(int irq, void *dev_id)
{
	disable_irq_nosync(pri_data.irq_num);
        schedule_work(&pri_data.work);
        return IRQ_HANDLED;

}
#endif

/*
 * @fn          isl29038_init_default
 *
 * @brief       This function initializes the sensor device
 *              with default values
 *
 * @return      returns 0 on success otherwise returns an 
 *              error (-EINVAL)
 *
 */

static int isl29038_init_default(struct i2c_client *client)
{
	/* Brown-Out clear  */
	if(i2c_smbus_write_byte_data(client, CONFIG_REG_3, 0x00) < 0)
		return -EINVAL;

	if(i2c_smbus_write_byte_data(client, CONFIG_REG_0, 0x00) < 0)
		return -EINVAL;
	if(i2c_smbus_write_byte_data(client, REG_CONFIG_1, 0x07) < 0)
		return -EINVAL;
	if(i2c_smbus_write_byte_data(client, REG_CONFIG_2, 0x00) < 0)
		return -EINVAL;
#ifdef ISL29038_INTERRUPT_MODE
	if(i2c_smbus_write_byte_data(client, ISL_ALS_INT_TL0, 0x00) < 0)	
		return -EINVAL;
	if(i2c_smbus_write_byte_data(client, ISL_ALS_INT_TL1, 0x0C) < 0)	
		return -EINVAL;
	if(i2c_smbus_write_byte_data(client, ISL_ALS_INT_TH0, 0xCC) < 0)	
		return -EINVAL;
#endif
	if(i2c_smbus_write_byte_data(client, CONFIG_REG_4, 0x00) < 0)
		return -EINVAL;
	if(i2c_smbus_write_byte_data(client, CONFIG_REG_3, 0x00) < 0)
		return -EINVAL;
			
	return 0;	
}


/*
 * @fn          isl29038_probe
 *
 * @brief       This function is called by I2C Bus driver on detection
 *              of sensor device.It validates the device and initialize
 *              the resources required by driver. 
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __devinit isl29038_probe(struct i2c_client *client, 
				const struct i2c_device_id *id)
{
	short int dev_id;
	struct isl29038_platform_data *pdata;

	pdata = client->dev.platform_data;
	if(pdata == NULL){
		pr_err("%s :Unable to find platform data\n",
					 __func__);		
		return -1;
	} 	
	
	dev_id = i2c_smbus_read_byte_data(client , DEVICE_ID_REG);
	if((dev_id & ISL29038_ID_MASK) != ISL29038_ID){
		pr_err("%s :%s :Invalid device id for ISL29038 sensor\n",
				ISL29038_NAME, __func__);
		goto err;
	}

	/* create a global copy of client structure */
	pri_data.isl_client = client;

	/* Initialize the sensor device */		
	if(isl29038_init_default(client) < 0){
		pr_err("%s :Failed to initialize the device\n",__func__);
		goto err;
	}
#ifdef ISL29038_INTERRUPT_MODE
	/* Request GPIO for sensor interrupt */	
	if(gpio_request(pdata->gpio_irq, "isl29038") < 0){
		pr_err("%s :Failed to request gpio\n", __func__);
		goto err;
	}
	
	/* Configure gpio direction */	
	if(gpio_direction_input(pdata->gpio_irq) < 0){
		pr_err("%s :Failed to set gpio direction\n", __func__);
		goto gpio_err;
	}	
	/* Configure gpio for interrupt */
	pri_data.irq_num = gpio_to_irq(pdata->gpio_irq);	
	if(pri_data.irq_num < 0){
		pr_err("%s :Failed to get irq_num\n", __func__);
		goto gpio_err;
	}
		
	if(irq_set_irq_type(pri_data.irq_num, IRQ_TYPE_EDGE_FALLING) < 0){
		pr_err( "%s :Failed to configure the irq type\n",__func__);
		goto gpio_err;
	} 
	
	/* Initialize the work queue */
	INIT_WORK(&pri_data.work, isl29038_irq_thread);
	
	/* Request irq num*/
	if(request_irq(pri_data.irq_num, isl29038_irq_handler, 
				IRQF_TRIGGER_FALLING, "isl29038", NULL) < 0){
		pr_err("%s :failed to request irq handler\n", __func__);
		goto gpio_err;
	}	
	
#endif

	/*Create sysfs files for isl29038*/
        pri_data.isl_kset = kset_create_and_add("intersil", NULL, NULL);
        if(pri_data.isl_kset < 0){
                pr_err("%s :%s : Failed to create sysfs"
                                        "\n",ISL29038_NAME, __func__);
                kobject_put(pri_data.isl_kobj);
              //  return -1;
        }
        pri_data.isl_kobj = kobject_create_and_add("isl29038",&pri_data.isl_kset->kobj);
        if(pri_data.isl_kobj < 0){
                pr_err( "%s :%s : Failed to create sysfs"
                                        "\n", ISL29038_NAME, __func__);
                kobject_put(pri_data.isl_kobj);
                return -1;
        }
        if(sysfs_create_group(pri_data.isl_kobj, &isl29038_attr_grp) < 0){
                pr_err( "%s :%s : Failed to create sysfs"
                                        "\n", ISL29038_NAME, __func__);
                kobject_put(pri_data.isl_kobj);
                return -1;
        }
	mutex_init(&pri_data.lock);

	/* Clear any previous interrupt */
	if(i2c_smbus_write_byte_data(client, CONFIG_REG_3, 0x00) < 0)
	return 0;

#ifdef ISL29038_INTERRUPT_MODE
gpio_err:
	gpio_free(pdata->gpio_irq);
#endif
err:
	return -1;
}

/*
 * @fn          isl29038_remove
 *
 * @brief       This function is called when sensor device gets removed 
 *              from bus
 *
 * @return      Returns 0
 *
 */
static int __devexit isl29038_remove(struct i2c_client *client)
{
	sysfs_remove_group(pri_data.isl_kobj, &isl29038_attr_grp);
	kset_unregister(pri_data.isl_kset);
#ifdef ISL29038_INTERRUPT_MODE
	/* Free interrupt number */
	free_irq(pri_data.irq_num, NULL);
	/* Free requested gpio */
	gpio_free(ISL29038_GPIO_IRQ);
#endif
	return 0;

}

/*
 * @fn          isl29038_suspend
 *
 * @brief       This function puts the sensor device in standby mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl29038_suspend(struct i2c_client *client, pm_message_t mesg)
{	
	int32_t ret;
	ret = i2c_smbus_read_byte_data(pri_data.isl_client, REG_CONFIG_1);	
	if(ret < 0){
		__dbg_read_err("%s",__func__);
		goto err_out;
	}	

	/* retain last mode */
	if(ret & 0x04){
		pri_data.als_mode = ret;	
		pri_data.prox_mode = 0;
		/* ALS disable */	
		if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1,
						 ret & 0xfb) < 0){
			__dbg_write_err("%s",__func__);
			goto err_out;
		}	
	}
	else{
		ret = i2c_smbus_read_byte_data(pri_data.isl_client, CONFIG_REG_0);
		if(ret < 0){
			__dbg_read_err("%s",__func__);
			goto err_out;
		}	
		pri_data.prox_mode = ret;
		pri_data.als_mode  = 0;
		/* PROX disable */
		if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_0, 
						ret & 0xdf) < 0){
			__dbg_write_err("%s",__func__);
			goto err_out;
		}
	}	
	pr_err("%s :sensor suspended\n",ISL29038_NAME);
#ifdef ISL29038_INTERRUPT_MODE
        disable_irq_nosync(pri_data.irq_num);
#endif
	return 0;	
err_out:
	return -1;

}

/*
 * @fn          isl29038_resume
 *
 * @brief       This function Resumes the sensor device from suspend mode
 *              and puts it in Active conversion mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl29038_resume(struct i2c_client *client)
{
	if(pri_data.als_mode){	
		if(i2c_smbus_write_byte_data(pri_data.isl_client, REG_CONFIG_1, pri_data.als_mode) < 0){
			__dbg_write_err("%s",__func__);
			goto err_out;
		}
	}else if(pri_data.prox_mode){
		if(i2c_smbus_write_byte_data(pri_data.isl_client, CONFIG_REG_0, pri_data.prox_mode) < 0){
			__dbg_write_err("%s",__func__);
			goto err_out;
		}
	}
	pr_err("%s: sensor resumed\n",ISL29038_NAME);	
#ifdef ISL29038_INTERRUPT_MODE
        enable_irq(pri_data.irq_num);
#endif
	return 0;
err_out:
	return -1;

}

static struct i2c_driver isl29038_sensor_driver = {
		.driver = {
			.name = "isl29038",
			.owner = THIS_MODULE,
		},
		.probe    = isl29038_probe,
		.suspend  = isl29038_suspend,
		.resume   = isl29038_resume,
		.remove   = isl29038_remove,
		.id_table = isl_device_id,
};


/*
 *  @fn          isl29038_init
 *
 *  @brief       This function initializes the driver
 *
 *  @return      Returns 0 on success otherwise returns an error (-1)
 *
 */


static int __init isl29038_init(void)
{
	/* Register the i2c driver with i2c core */
	return i2c_add_driver(&isl29038_sensor_driver);

}

/*
 * @fn          isl29038_exit
 *
 * @brief       This function is called to cleanup driver entry
 *
 * @return      Void
 *
 */

static void __exit isl29038_exit(void)
{
	/* Unregister i2c driver with i2c core */
	i2c_del_driver(&isl29038_sensor_driver);

}

MODULE_AUTHOR  ("VVDN Technologies Pvt. Ltd.");
MODULE_LICENSE ("GPLv2");
MODULE_DESCRIPTION ("I2C device driver for ISL29038 ALS/PRX Sensor");

module_init(isl29038_init);
module_exit(isl29038_exit);

