/****************************************************************************
 *      
 *      File            : isl29028A.c
 *      
 *      Description     : Device Driver for ISL29028A ALS-PROX Sensor
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
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/irq.h>
#include <linux/isl29028A.h>
#include <linux/delay.h>

#ifndef _PRINTK_H_
#include <linux/printk.h>
#endif

#define ISL29028A_INTERRUPT_MODE

/* Private members of isl29028A sensor */
static struct isl29028A_data {
/* mutex lock for critical sections */
	struct mutex lock;
	struct work_struct work;
	struct kset *isl_kset;
	struct kobject *isl_kobj;
	uchar last_mod;
#ifdef ISL29028A_INTERRUPT_MODE
	uint16_t persist_flag;
	int32_t irq_num;
#endif

}isl_data;

/* Global i2c client data */
static struct i2c_client *isl_client;

/* Device Id table containing list of devices sharing this driver */
static struct i2c_device_id isl_device_table[] = {
	{"isl29028A", ISL29028A_I2C_ADDR},		/* Device slave address 0x45h*/
	{}
};

/*
 * @fn          isl29028A_i2c_read_word16
 *
 * @brief       This wrapper function reads a word (16-bit) from 
 *		sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32_t isl29028A_i2c_read_word16(struct i2c_client *client,
				 	uchar reg_addr,	uint32_t *buf)
{
	int16_t reg_h;
	int16_t reg_l;
	
	reg_l = i2c_smbus_read_byte_data(client, reg_addr);
	if (reg_l < 0) 
		return -1;
	reg_h = i2c_smbus_read_byte_data(client, reg_addr + 1);
	if (reg_h < 0) 
		return -1;
	*buf = (reg_h << 8) | reg_l;
	return 0;
}

#ifdef ISL29028A_INTERRUPT_MODE
/*
 * @fn          show_prox_low_thres
 *
 * @brief       This function shows the low proximity interrupt threshold
 *              value of 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_prox_low_thres(struct kobject *kobj, 
			struct kobj_attribute *attr, char *buf)
{
	int16_t ret;
	mutex_lock(&isl_data.lock);

	ret = i2c_smbus_read_byte_data(isl_client, ISL_PROX_LT_BYTE);
	if(ret < 0)
		goto err_out;
	mutex_unlock(&isl_data.lock);
	return sprintf(buf,"%d", ret);
err_out:
	__dbg_read_err("%s", __func__);
	mutex_unlock(&isl_data.lock);
	return -1;
}

/*
 * @fn          store_prox_low_thres
 *
 * @brief       This function writes the low interrupt threshold
 *              value in 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_prox_low_thres(struct kobject *kobj,
			 struct kobj_attribute *attr, const char *buf,
					 size_t count)
{
	int16_t reg;

	mutex_lock(&isl_data.lock);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg < 0 || reg > 255){
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
	if (i2c_smbus_write_byte_data(isl_client, 
				    ISL_PROX_LT_BYTE, reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}	
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&isl_data.lock);
	return -1;
}

/*
 * @fn          show_prox_high_thres
 *
 * @brief       This function shows the high interrupt threshold
 *              value of 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_prox_high_thres(struct kobject *kobj,
                         struct kobj_attribute *attr,  char *buf)  
{
        bytes_r r_byte;

        mutex_lock(&isl_data.lock);
        r_byte = i2c_smbus_read_byte_data(isl_client, ISL_PROX_HT_BYTE);
        if(r_byte < 0){
		__dbg_read_err("%s", __func__);
                mutex_unlock(&isl_data.lock);
                return -1;
        }
        mutex_unlock(&isl_data.lock);
        return sprintf(buf, "%d", r_byte);
}
/*
 * @fn          store_prox_high_thres
 *
 * @brief       This function writes the high interrupt threshold
 *              value in 8-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_prox_high_thres(struct kobject *kobj,
                         struct kobj_attribute *attr, const char *buf,  
                                         size_t count) 
{
	bytes_r reg;

        mutex_lock(&isl_data.lock);
        reg = simple_strtoul(buf, NULL, 10);
        if (reg < 0 || reg > 255) {
		__dbg_invl_err("%s", __func__);
		goto err_out;
        }
        if(i2c_smbus_write_byte_data(isl_client, ISL_PROX_HT_BYTE, 
						    reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
        }
        mutex_unlock(&isl_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&isl_data.lock);
        return -EINVAL;
}

/*
 * @fn          show_alsir_low_thres
 *
 * @brief       This function shows the low interrupt threshold
 *              value of 12-bit register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_alsir_low_thres(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	uint32_t reg;

	mutex_lock(&isl_data.lock);
	if( isl29028A_i2c_read_word16(isl_client, ISL_ALSIR_TH1, &reg) < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}

	/* Clear Upper 4 bit of reg to make it 12-bit data */
	mutex_unlock(&isl_data.lock);
	return sprintf(buf, "%d", (reg & ISL_12BIT_THRES_MASK));
}

/*
 * @fn          store_alsir_low_thres
 *
 * @brief       This function shows the low alsir interrupt threshold
 *              value of 12-bit register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_alsir_low_thres(struct kobject *kobj,
			 struct kobj_attribute *attr, const char *buf,
					 size_t count)
{
	unsigned short reg;
	bytes_r ret;
	u8 reg_h;
	u8 reg_l;
	mutex_lock(&isl_data.lock);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 4095) {
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
	ret = i2c_smbus_read_byte_data(isl_client, ISL_ALSIR_TH2);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	/* Extract LSB and MSB bytes from data */
	reg_l = reg & 0xFF;
	reg_h = (reg & 0xFF00) >> 8;
	reg_h = (ret & 0xF0) | reg_h;
	
	/* Write Low byte threshold at 0x05h */
	ret = i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH1, reg_l);		
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	/* Write high byte threshold at 0x06h retaining upper nibble */
	ret = i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH2, reg_h);		
	if(ret < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}
		
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&isl_data.lock);
	return -1;
}

/*
 * @fn          show_alsir_high_thres
 *
 * @brief       This function shows the high alsir interrupt threshold
 *              value of 12-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_alsir_high_thres(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	unsigned int reg;

	mutex_lock(&isl_data.lock);
	if(isl29028A_i2c_read_word16(isl_client, ISL_ALSIR_TH2, &reg) < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	/* Clear Lower 4 bit of reg to make it 12-bit data */
	reg = (reg & 0xfff0) >> 4;		
	mutex_unlock(&isl_data.lock);
	return sprintf(buf, "%d", reg);

}

/*
 * @fn          store_alsir_high_thres
 *
 * @brief       This function shows the high interrupt threshold
 *              value in 12-bit threshold register
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_alsir_high_thres(struct kobject *kobj,
			 struct kobj_attribute *attr, const char *buf,
					 size_t count)
{
	unsigned int reg;
	u8  reg_l, reg_h;
	short int ret;
	mutex_lock(&isl_data.lock);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg <= 0 || reg > 4095) {
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
	ret = i2c_smbus_read_byte_data(isl_client, ISL_ALSIR_TH2);
        if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
        }
        /* Extract LSB and MSB bytes from data */
	reg <<= 4;
        reg_l = reg & 0xFF;
        reg_h = (reg & 0xFF00) >> 8;
	reg_l = (ret & 0x0f) | reg_l;

        /* Write Low byte threshold at 0x05h */
        if(i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH2, reg_l) < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
        }
        /* Write high byte threshold at 0x06h retaining upper nibble */
        if(i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH3, reg_h) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
        }

        mutex_unlock(&isl_data.lock);
        return strlen(buf);
err_out:
	mutex_unlock(&isl_data.lock);
	return -1;

}

/*
 * @fn          show_intr_perst
 *
 * @brief       This function displays the current interrupt
 *              persistency of ALS or PROX sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_perst(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	int8_t reg, als_persist, prox_persist;
	mutex_lock(&isl_data.lock);
	reg = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_2);
	if(reg < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	switch(isl_data.persist_flag)
	{
		case PROX_MODE:
			reg = (reg & 0x60) >> 5;
			switch(reg)
			{
				case 0x00:	prox_persist = 1; break;
				case 0x01:	prox_persist = 4; break;
				case 0x02:	prox_persist = 8; break;
				case 0x03:	prox_persist = 16;break;
				default:__dbg_invl_err("%s",__func__);break;
			}
			sprintf(buf,"%d", prox_persist);
			break;
		case ALS_MODE:	
			reg = (reg & 0x06) >> 1;		/* Mask the other bits to 0*/	
			switch(reg)
			{
				case 0x00:	als_persist = 1; break;
				case 0x01:	als_persist = 4; break;
				case 0x02:	als_persist = 8; break;
				case 0x03:	als_persist = 16;break;
				default:__dbg_invl_err("%s",__func__);
			}
			sprintf(buf,"%d", als_persist);
			break;
	}
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
	
}

/*
 * @fn          store_intr_perst
 *
 * @brief       This function sets the interrupt persistency of
 *              sensor device
 *		valid persistences : 1 /4 /8 /16	
 *
 * @return      Returns size of buffer data on success otherwise
 *              returns an error (-1)
 *
 */

static ssize_t store_intr_perst(struct kobject *kobj,
			 struct kobj_attribute *attr, const char *buf,
					 size_t count)
{
	int8_t reg;
	int16_t intr_persist;
	mutex_lock(&isl_data.lock);
	intr_persist = simple_strtoul(buf, NULL, 10);
	if(intr_persist <= 0 || intr_persist > 16){
		__dbg_invl_err("%s", __func__);	
		goto err_out;
	}
	switch(isl_data.persist_flag)
	{
		case PROX_MODE:
			if(intr_persist == 1){
				intr_persist = ISL_PROX_PERST_SET_1;
			}else if(intr_persist == 4){
				intr_persist = ISL_PROX_PERST_SET_4;
			}else if(intr_persist == 8){
				intr_persist = ISL_PROX_PERST_SET_8;
			}else if(intr_persist == 16){
				intr_persist = ISL_PROX_PERST_SET_16;
			}else{
				__dbg_invl_err("%s", __func__);
				goto err_out;
			}
			break;
		case ALS_MODE:
			if(intr_persist == 1){                      
                                intr_persist = ISL_ALS_PERST_SET_1;
                        }else if(intr_persist == 4){                
                                intr_persist = ISL_ALS_PERST_SET_4;
                        }else if(intr_persist == 8){                
                                intr_persist = ISL_ALS_PERST_SET_8;
                        }else if(intr_persist == 16){               
                                intr_persist = ISL_ALS_PERST_SET_16;     
                        }else{
				__dbg_invl_err("%s", __func__);
				goto err_out;
                        }
                        break;
	}
	reg = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_2);
        if (reg < 0) {
		__dbg_read_err("%s", __func__);
		goto err_out;
        }
      	if(isl_data.persist_flag) 
        	reg = (reg & 0x06) | intr_persist; 		/* PROXIMITY SENSING MODE*/
	else 
		reg = (reg & 0x60) | intr_persist;		/* ALS SENSING MODE */

        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_2, reg) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
        }
        mutex_unlock(&isl_data.lock);
        return strlen(buf);
err_out:
        mutex_unlock(&isl_data.lock);
        return -1;
	
}

#endif

/*
 * @fn          show_alsir_range
 *
 * @brief       This function shows the current optical sensing range
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_alsir_range(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	short int ret;    

	mutex_lock(&isl_data.lock);
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);	
        if (ret < 0) {
		__dbg_read_err("%s", __func__);
                mutex_unlock(&isl_data.lock);
                return -1;
        }
	ret = ( ret >> 1 ) & 0x01;		
	mutex_unlock(&isl_data.lock);	
	return sprintf(buf, "%d", (ret ? 2000 : 125));

}


/*
 * @fn          isl_set_sensing_range
 *
 * @brief       This function sets the operating sensing range of sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_set_sensing_range(short int val)
{
	short int ret;

	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
	if(ret < 0)
		return -1;
	val = (ret & 0xfd) | val;	
	ret = i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, val);
	if(ret < 0)
		return -1;
	return 0;

}

/*
 * @fn          store_alsir_range
 *
 * @brief       This function writes the optical sensing ranges
 *              of sensor device
 *	 	valid ranges LOW = 125
 *		 	    HIGH = 2000
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t store_alsir_range(struct kobject *kobj,
		 	struct kobj_attribute *attr, const char *buf,
					 size_t count)
{
	short int ret, val;

	mutex_lock(&isl_data.lock);
	ret = simple_strtoul(buf, NULL, 10);
	if(ret == 125){
		val = ISL_ALS_LOW_RANGE;
	}else if(ret == 2000){
		val = ISL_ALS_HIGH_RANGE;
	}else{
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
        if(isl_set_sensing_range(val) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
        }
	mutex_unlock(&isl_data.lock);	
	return strlen(buf);	
err_out:
        mutex_unlock(&isl_data.lock);
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
	short int ret;
	mutex_lock(&isl_data.lock);
	ret = i2c_smbus_read_byte_data(isl_client, ISL_PROX_DATA);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	mutex_unlock(&isl_data.lock);
	return sprintf(buf, "%d", ret);	

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
	unsigned int reg;

	mutex_lock(&isl_data.lock);	
	if(isl29028A_i2c_read_word16(isl_client, ISL_ALSIR_DT1, &reg) < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	mutex_unlock(&isl_data.lock);
	return sprintf(buf, "%d", (reg & 0x0fff));
}

/*
 * @fn          show_ir_data
 *               
 * @brief       This function shows the ir data sensed by sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_ir_data(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	
        unsigned int reg;                                                         
        mutex_lock(&isl_data.lock); 
        if(isl29028A_i2c_read_word16(isl_client, ISL_ALSIR_DT1, &reg) < 0){
		__dbg_read_err("%s", __func__);
                mutex_unlock(&isl_data.lock);
                return -1;                                                          
        }
        mutex_unlock(&isl_data.lock);                                                
        return sprintf(buf, "%d", (reg & 0x0fff));
}

/*
 * @fn          show_mode
 *
 * @brief       This function shows the current optical sensing mode
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_mode(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	short int mode;

	mutex_lock(&isl_data.lock);
	mode = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);	
	if(mode < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	if((mode & 0x05) == 0x05)
		sprintf(buf, "%s", "ir");
	else if((mode & 0x05) == 0x04)	
		sprintf(buf,"%s", "als");
	else if(mode & 0x80)
		sprintf(buf, "%s", "proximity");

	mutex_unlock(&isl_data.lock);	
	return strlen(buf);	

}

static int set_sensing_mode(uint16_t mode)
{
	short int reg;
	reg = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
	if(reg < 0)
		return -1;
	reg = (reg & 0x7A) | mode;
	if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, reg) < 0) 
		return -1;
	return 0;
}

/*
 * @fn          store_mode
 *
 * @brief       This function stores the current optical sensing mode
 *              of sensor device
 *		valid modes : als / ir / proximity		
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t store_mode(struct kobject *kobj,
			 struct kobj_attribute *attr, const char *buf,
						 size_t count)
{
	uint16_t mode;

	mutex_lock(&isl_data.lock);
	if(!strcmp(buf, "als")){
		mode = ISL_OP_MODE_ALS_SENSING;
		isl_data.persist_flag = ALS_MODE;
	}else if(!strcmp(buf, "ir")){
		mode = ISL_OP_MODE_IR_SENSING;
		isl_data.persist_flag = ALS_MODE;
	}else if(!strcmp(buf, "proximity")){
		mode = ISL_OP_MODE_PROX;
		isl_data.persist_flag = PROX_MODE;
	}else{
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}	
	if(set_sensing_mode(mode) < 0){
		__dbg_invl_err("%s", __func__);
		goto err_out;
	}
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&isl_data.lock);
	return -1;
}

/*
 * @fn          show_prox_status
 *
 * @brief       This function shows the current proximity status
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_prox_status(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buf)
{
	int ret;
	mutex_lock(&isl_data.lock);
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	ret &= 0x80;
	switch(ret >> 7)
	{
		case 0:	sprintf(buf, "disable");break;
		case 1:	sprintf(buf, "enable");	break;
	}
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
}

/*
 * @fn          show_ir_current
 *
 * @brief       This function exports the IR current to sysfs
 *		valid ir current is 110 / 220
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_current(struct kobject *kobj,
                         struct kobj_attribute *attr, char *buf)
{
	short int ret;
	unsigned short int val;
	
	mutex_lock(&isl_data.lock);
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
	ret = (ret >> 3) & 0x01; 	
	switch(ret)
	{
		case 0:	val = 110;	/* IRDR behaves as a pulsed 110mA current sink */
			break;
		case 1:	val = 220;	/* IRDR behaves as a pulsed 2200mA current sink */
			break;
	}
	mutex_unlock(&isl_data.lock);
	return sprintf(buf, "%d", val);
}

/*
 * @fn          store_ir_current
 *
 * @brief       This function writes the IR drive current to device
 *              valid ir current is 110 / 220
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_ir_current(struct kobject *kobj,
                         struct kobj_attribute *attr, const char *buf,
                                                 size_t count)
{
	short int ret;
	unsigned int val;
        mutex_lock(&isl_data.lock);
	val = simple_strtoul(buf, NULL, 10);
	if(val == 110){
		val = ISL_PROX_DR_110mA;
	}else if(val == 220){
		val = ISL_PROX_DR_220mA;
	}else {
		__dbg_invl_err("%s", __func__);
		mutex_unlock(&isl_data.lock);
		return -1;
	}
       	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1); 
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err_out;
	}
	val = (ret & 0xf7) | val;
	if (i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, val) < 0){
		__dbg_write_err("%s", __func__);
		goto err_out;
	}
	mutex_unlock(&isl_data.lock);
	return strlen(buf);
err_out:
	mutex_unlock(&isl_data.lock);
	return -1;
	
}

static ssize_t show_prox_sleep_t(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
        int16_t sleep_t;
        mutex_lock(&isl_data.lock);
        sleep_t = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
        if(sleep_t < 0){
                __dbg_read_err("%s", __func__);
                mutex_unlock(&isl_data.lock);
                return -1;
        }

        sleep_t = (sleep_t & 0x70) >> ISL_PROX_SLP_POS;
        switch(sleep_t)
	{
		case 0x00:sprintf(buf ,"%s","800ms");break;
                case 0x01:sprintf(buf ,"%s","400ms");break;
                case 0x02:sprintf(buf ,"%s","200ms");break;
                case 0x03:sprintf(buf ,"%s","100ms");break;
                case 0x04:sprintf(buf ,"%s","75ms");break;
                case 0x05:sprintf(buf ,"%s","50ms");break;
                case 0x06:sprintf(buf ,"%s","12.5ms");break;
                case 0x07:sprintf(buf ,"%s","0ms");break;
                default : return -1;
        }
        mutex_unlock(&isl_data.lock);
        return strlen(buf);
}

static ssize_t store_prox_sleep_t(struct kobject *kobj,
                struct kobj_attribute *attr, const char *buf,
                        size_t count)
{
        int16_t reg, ret;
        uint32_t sleep_t;
        mutex_lock(&isl_data.lock);
      	sleep_t = simple_strtoul(buf,NULL ,10);
       	if(sleep_t < 0 || sleep_t > 800)
	        goto err_out;
        switch(sleep_t)
        {
        case 800  :reg = ISL_PROX_SLP_800ms;break;
        case 400  :reg = ISL_PROX_SLP_400ms;break;
        case 200  :reg = ISL_PROX_SLP_200ms;break;
        case 100  :reg = ISL_PROX_SLP_100ms;break;
        case 75   :reg = ISL_PROX_SLP_75ms;break;
        case 50   :reg = ISL_PROX_SLP_50ms;break;
        case 125  :reg = ISL_PROX_SLP_12500us;break;
        case 0    :reg = ISL_PROX_SLP_0ms;break;
        default   :goto err_out;
        }

        ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
        if(ret < 0)
                goto err_out;

        reg = (ret & 0x8F) | reg;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, reg) < 0)
                goto err_out;

        mutex_unlock(&isl_data.lock);
        return strlen(buf);

err_out:
	__dbg_invl_err("%s",__func__);
        mutex_unlock(&isl_data.lock);
        return -1;
}



MODULE_DEVICE_TABLE(i2c,isl_device_table);

/* Kernel object structure attributes for mode sysfs */
static struct kobj_attribute mode_attribute = 
__ATTR(mode, 0666, show_mode, store_mode);

#ifdef ISL29028A_INTERRUPT_MODE
/* Kernel object structure attributes for prox_low_threshold sysfs */
static struct kobj_attribute prox_low_thres_attribute = 
__ATTR(prox_low_thres, 0666, show_prox_low_thres, store_prox_low_thres);

/* Kernel object structure attributes for prox_high_threshold sysfs */
static struct kobj_attribute prox_high_thres_attribute = 
__ATTR(prox_high_thres, 0666, show_prox_high_thres, store_prox_high_thres);

/* Kernel object structure attributes for alsir_low_threshold sysfs */
static struct kobj_attribute alsir_low_thres_attribute =
__ATTR(alsir_low_thres, 0666, show_alsir_low_thres, store_alsir_low_thres);

/* Kernel object structure attributes for alsir_high_threshold sysfs */
static struct kobj_attribute alsir_high_thres_attribute =
__ATTR(alsir_high_thres, 0666, show_alsir_high_thres, store_alsir_high_thres);

/* Kernel object structure attributes for intr_persistence sysfs */
static struct kobj_attribute intr_perst_attribute = 
__ATTR(intr_perst, 0666, show_intr_perst, store_intr_perst);
#endif

/* Kernel object structure attributes for alsir_range sysfs */
static struct kobj_attribute alsir_range_attribute = 
__ATTR(alsir_range,0666, show_alsir_range, store_alsir_range);

/* Kernel object structure attributes for prox_data sysfs */
static struct kobj_attribute prox_data_attribute = 
__ATTR(prox_data, 0666, show_prox_data, NULL);

/* Kernel object structure attributes for als_data sysfs */
static struct kobj_attribute als_data_attribute = 
__ATTR(als_data, 0666, show_als_data, NULL);

/* Kernel object structure attributes for ir_data sysfs */
static struct kobj_attribute ir_data_attribute = 
__ATTR(ir_data, 0666, show_ir_data, NULL);

/* Kernel object structure attributes for prox_status sysfs */
static struct kobj_attribute prox_status_attribute = 
__ATTR(prox_status, 0666, show_prox_status, NULL);

/* kernel object structure for ir_current */
static struct kobj_attribute ir_current_attribute =
__ATTR(ir_current, 0666, show_ir_current, store_ir_current);

static struct kobj_attribute prox_sleep_t_attribute = 
__ATTR(prox_sleep_t, 0666, show_prox_sleep_t, store_prox_sleep_t);

static struct attribute *isl29028A_attrs[] = {

#ifdef ISL29028A_INTERRUPT_MODE
	&prox_low_thres_attribute.attr,
	&prox_high_thres_attribute.attr,
	&alsir_low_thres_attribute.attr,
	&alsir_high_thres_attribute.attr,
	&intr_perst_attribute.attr,
#endif
	&prox_data_attribute.attr,
	&alsir_range_attribute.attr,
	&als_data_attribute.attr,
	&ir_data_attribute.attr,
	&prox_status_attribute.attr,
	&mode_attribute.attr,
	&ir_current_attribute.attr,
	&prox_sleep_t_attribute.attr,
	NULL
};

static struct attribute_group isl29028A_attr_grp = {
	.attrs = isl29028A_attrs,

};

#ifdef ISL29028A_INTERRUPT_MODE

/*
 * @fn          isl_sensor_irq_handler
 *
 * @brief       This function is the interrupt handler for sensor.
 *              It schedules an interrupt thread and returns (free)
 *              interrupt.
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
 * @fn          initialize_isl29028A
 *
 * @brief       This function initializes the sensor device
 *              with default values
 *
 * @return      returns 0 on success otherwise returns an 
 *              error (-EINVAL)
 *
 */

static int initialize_isl29028A(struct i2c_client *client)
{
	
	/* As per the device datasheet recommendations */
	/* Power down the device */
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, ISL_REG_CLEAR) < 0)
                return -EINVAL;
	/* Write 0x0E to configuration test register 1*/
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST1, ISL_REG_1_DEF) < 0)
                return -EINVAL;
	/* Write 0x029 to configuratio test register 2*/
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST2, ISL_REG_TEST_2) < 0)
                return -EINVAL;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST2, ISL_REG_CLEAR) < 0)
                return -EINVAL;
	mdelay(2);

	/*Initialize the dvice in als sensing mode */
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, 0x74) < 0)
                return -EINVAL;
        if(i2c_smbus_write_byte_data(client, CONFIG_REG_2, 0x66) < 0)
                return -EINVAL;

#ifdef ISL29028A_INTERRUPT_MODE

        /* Writing interrupt low threshold as 0xCCC (5% of max range) */
        if(i2c_smbus_write_byte_data(isl_client, ISL_PROX_LT_BYTE, ISL_PROX_LT_DEF) < 0)
                return -EINVAL;

        /* Writing interrupt high threshold as 0xCCCC (80% of max range)  */
        if(i2c_smbus_write_byte_data(client, ISL_PROX_HT_BYTE, ISL_PROX_HT_DEF) < 0)
                return -EINVAL;

        /* Writing interrupt low threshold as 0xCCC (5% of max range) */
        if(i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH1, ISL_ALSIR_TH1_DEF) < 0)
                return -EINVAL;

        if(i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH2, ISL_ALSIR_TH2_DEF) < 0 )
                return -EINVAL;

        /* Writing interrupt high threshold as 0xCCCC (80% of max range)  */
       if(i2c_smbus_write_byte_data(isl_client, ISL_ALSIR_TH3, ISL_ALSIR_TH3_DEF) < 0)

                return -EINVAL;
#endif
	return 0;
	
}

#ifdef ISL29028A_INTERRUPT_MODE

/*
 * @fn          isl29028A_irq_thread
 *
 * @brief       This work thread is scheduled by sensor interrupt handler
 *		to clear the interrupt flags of sensor
 *
 * @return      void
 */

static void isl29028A_irq_thread(struct work_struct *work)
{
	short int ret;
	
	ret = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_2);
	if(ret < 0){
		__dbg_read_err("%s", __func__);
		goto err;
	}
    	if(i2c_smbus_write_byte_data(isl_client,CONFIG_REG_2,
				(ret & ISL_INT_CLEAR_MASK)) < 0){
		__dbg_write_err("%s", __func__);
                goto err;
        }
err:
        enable_irq(isl_data.irq_num);
	
}
#endif

/*
 * @fn          isl_sensor_probe
 *
 * @brief       This function is called by I2C Bus driver on detection
 *              of sensor device.It validates the device and initialize
 *              the resources required by driver. 
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __devinit isl_sensor_probe(struct i2c_client *client, 
					const struct i2c_device_id *id)
{
	struct isl29028A_platform_data *pdata;
	
	pdata = client->dev.platform_data;
	if(pdata == NULL){
		pr_err("%s :%s :Unable to find platform"
				"data\n",ISL29028_NAME, __func__);
		return -EINVAL;	
	}

	isl_client = client; 
	isl_data.last_mod = 0;
	isl_data.persist_flag = 0;
	mutex_init(&isl_data.lock);

	/* Initialise the sensor with default configuration */
	if(initialize_isl29028A(isl_client) < 0){
		pr_err("%s : %s :Device initialization failed\n",
					ISL29028_NAME, __func__);
		return -1;
	}

#ifdef ISL29028A_INTERRUPT_MODE
	/* Request GPIO for sensor interrupt */
	if(gpio_request(pdata->gpio_irq, "isl29028A") < 0){	
		pr_err( "%s :%s :Failed to request GPIO\n"
			    "\n",ISL29028_NAME, __func__);
		goto err;
	}

	/* Configure interrupt GPIO as input pin */
	if(gpio_direction_input(pdata->gpio_irq) < 0){
		pr_err("%s : %s:Failed to set gpio direction\n",
					ISL29028_NAME, __func__);
		goto gpio_err;
	}

	/* Configure the GPIO for interrupt */
	isl_data.irq_num = gpio_to_irq(pdata->gpio_irq);
	if (isl_data.irq_num < 0){
		pr_err( "%s : %s:Failed to get IRQ number for"
					, ISL29028_NAME, __func__);
		goto gpio_err;
	}

	if(irq_set_irq_type(isl_data.irq_num, IRQ_TYPE_EDGE_FALLING) < 0){
		pr_err("%s:%s:Failed to set irq type\n", ISL29028_NAME, __func__);
		goto gpio_err;
	} 

	/* Initialize the work queue */
	INIT_WORK(&isl_data.work,isl29028A_irq_thread);

	/* Register irq handler for sensor */
	if(request_irq(isl_data.irq_num, isl_sensor_irq_handler, 
				IRQF_TRIGGER_FALLING, "isl29028A", NULL) < 0){
		pr_err("%s : %s:Failed to register irq handler",
						 ISL29028_NAME, __func__);
		goto gpio_err;
	}
#endif

	/*Create sysfs files for isl29028a*/
	isl_data.isl_kset = kset_create_and_add("intersil", NULL, NULL);
	if(isl_data.isl_kset < 0){
		pr_err("%s :%s :Failed to create sysfs"
					"\n",ISL29028_NAME, __func__);
		kobject_put(isl_data.isl_kobj);  
		goto gpio_err;
	}
	isl_data.isl_kobj = kobject_create_and_add("isl29028A",&isl_data.isl_kset->kobj);
	if(isl_data.isl_kobj < 0){
		pr_err("%s :%s :Failed to create sysfs"
					"\n",ISL29028_NAME, __func__);
		kobject_put(isl_data.isl_kobj);  
		goto gpio_err;
	}
	if(sysfs_create_group(isl_data.isl_kobj, &isl29028A_attr_grp) < 0){
		pr_err("%s :%s :Failed to create sysfs"
					"\n",ISL29028_NAME, __func__);
		kobject_put(isl_data.isl_kobj);  
		goto gpio_err;
	}
	return 0;

#ifdef ISL29028A_INTERRUPT_MODE
gpio_err:
	gpio_free(pdata->gpio_irq);
err:
	return -1;
#endif

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

        reg = i2c_smbus_read_byte_data(isl_client, CONFIG_REG_1);
        if(reg < 0){
		__dbg_read_err("%s", __func__);
                goto err;
        }
        isl_data.last_mod = reg;

        /* Put the sensor to ALS/IR Disabled and Proximity Disabled mode*/
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, ISL_OP_POWERDOWN) < 0){
		__dbg_write_err("%s", __func__);
                goto err;
        }
        pr_err("%s :%s :Sensor suspended\n",ISL29028_NAME, __func__);

#ifdef ISL29028A_INTERRUPT_MODE
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
 *              and puts it in Active conversion mode
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_sensor_resume(struct i2c_client *client)
{
	
        short int reg;

	/* As per the datasheet recommendations */
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1, ISL_REG_CLEAR) < 0)
                return -EINVAL;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST1, ISL_REG_TEST_1) < 0)
                return -EINVAL;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST2, ISL_REG_TEST_2) < 0)
                return -EINVAL;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_TEST2, ISL_REG_CLEAR) < 0)
                return -EINVAL;
        mdelay(2);

        reg = isl_data.last_mod;
        if(i2c_smbus_write_byte_data(isl_client, CONFIG_REG_1,reg) < 0){
		__dbg_read_err("%s", __func__);
                goto err;
        }
        pr_err("%s :%s :Sensor resumed\n",ISL29028_NAME, __func__);
#ifdef ISL29028A_INTERRUPT_MODE
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
 *              from bus
 *
 * @return      Returns 0
 *
 */

static int __devexit isl_sensor_remove(struct i2c_client *client)
{
	sysfs_remove_group(isl_data.isl_kobj, &isl29028A_attr_grp);
	kset_unregister(isl_data.isl_kset);
#ifdef ISL29028A_INTERRUPT_MODE
	free_irq(isl_data.irq_num, NULL);
	gpio_free(39);
#endif
	return 0;

}

/* Driver information sent to kernel*/
static struct i2c_driver isl29028A_sensor_driver = {
	.driver = {
		.name = "isl29028A",
		 .owner = THIS_MODULE,
	},
	.probe		= isl_sensor_probe,
	.remove		= isl_sensor_remove,
	.suspend 	= isl_sensor_suspend,
	.resume		= isl_sensor_resume,
	.id_table	= isl_device_table,
};

/*
 *  @fn          isl29028A_init
 *
 *  @brief       This function initializes the driver
 *
 *  @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __init isl29028A_init(void)
{
	return i2c_add_driver(&isl29028A_sensor_driver);

}

/*
 * @fn          isl29028A_exit
 *
 * @brief       This function is called to cleanup driver entry
 *
 * @return      Void
 *
 */

static void __exit isl29028A_exit(void)
{
	i2c_del_driver(&isl29028A_sensor_driver);

}

MODULE_AUTHOR	("sanoj.kumar@vvdntech.com");
MODULE_LICENSE	("GPLv2");
MODULE_DESCRIPTION ("device driver for ISL29028A Prox/ALS Sensor");

module_init(isl29028A_init);
module_exit(isl29028A_exit);

