/****************************************************************************
 *      
 *      File            : isl29035.c
 *      
 *      Description     : Device Driver for ISL29035 ALS-IR Sensor
 *
 *      License         : GPLv2
 *      
 *      Copyright       : Intersil Corporation (c) 2013 
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
#include <linux/isl29035.h>
#include <linux/delay.h>
#ifndef _PRINTK_H_
#include <linux/printk.h>
#endif
#define MAX_COUNT 10

#define ISL29035_INTERRUPT_MODE

static struct isl29035_data {
        struct i2c_client *client_data;
        struct work_struct work;
        struct mutex isl_mutex;
        int32_t last_mod;
        int16_t intr_flag;
	uint8_t count;
        uint16_t last_ir_lt;
        uint32_t irq_num;
        uint16_t last_ir_ht;
        uint16_t last_als_lt;
        uint16_t last_als_ht;

} isl_data;

/* Device Id table containing list of devices sharing this driver */
static struct i2c_device_id isl_device_table[] = {
	{"isl29035", ISL29035_I2C_ADDR},		/* Device slave address 0x44h*/
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

static int32_t get_adc_resolution_bits(uint16_t *res)
{
        int16_t ret;
        ret = i2c_smbus_read_byte_data(isl_data.client_data, ISL29035_CMD_REG_2);
        if(ret < 0)
                return -1;

        ret = (ret & ISL29035_ADC_RES_MASK) >> 2;
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

static int32_t set_adc_resolution_bits(uint16_t res)
{
        uint16_t reg;
        reg = i2c_smbus_read_byte_data(isl_data.client_data,
                                        ISL29035_CMD_REG_2);
        if(reg < 0)
                return -1;

        reg =  ( reg & ISL29035_ADC_READ_MASK ) | res;
        if(i2c_smbus_write_byte_data(isl_data.client_data,
                                        ISL29035_CMD_REG_2, reg) < 0)
                return -1;
        return 0;
}

/*
 * @fn          show_adc_res_bits
 *
 * @brief       This function shows the current adc resolution of sensor
 *              device (16/12/8/4 -bit)
 *
 * @return      Returns the length of data buffer on success otherwise 
 *              returns an error (-1)
 *
 */

static ssize_t show_adc_res_bits(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        uint16_t reg;

        mutex_lock(&isl_data.isl_mutex);
        if(get_adc_resolution_bits(&reg) < 0){
		__dbg_read_err("%s", __func__);
                mutex_unlock(&isl_data.isl_mutex);
                return -1;
        }
        mutex_unlock(&isl_data.isl_mutex);
        return sprintf(buf,"%d",reg);
}

/*
 * @fn          store_adc_res_bits
 *
 * @brief       This function sets the adc resolution of
 *              sensor device (16/12/8/4 -bit)
 *
 * @return      Returns the length of data buffer on success
 *              otherwise returns an error (-1)
 *
 */

static ssize_t store_adc_res_bits(struct device *dev,
                        struct device_attribute *attr, const char *buf,
                                size_t count)
{
        int16_t reg;

        mutex_lock(&isl_data.isl_mutex);
	reg = simple_strtoul(buf, NULL, 10);
        switch(reg)
        {
                case 16: reg = ISL29035_ADC_RES_16BIT_SET;break;
                case 12: reg = ISL29035_ADC_RES_12BIT_SET;break;
                case 8:  reg = ISL29035_ADC_RES_8BIT_SET;break;
                case 4:  reg = ISL29035_ADC_RES_4BIT_SET;break;
                default:
                         __dbg_invl_err("%s",__func__);
                         mutex_unlock(&isl_data.isl_mutex);
                         return -1;
        }
       if(set_adc_resolution_bits(reg) < 0){
		__dbg_write_err("%s",__func__);
                mutex_unlock(&isl_data.isl_mutex);
                return -1;
        }
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
}

#ifdef ISL29035_INTERRUPT_MODE
/*
 * @fn          show_intr_persistency
 *
 * @brief       This function displays the current interrupt
 *              persistency of sensor device
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_persistency(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        int16_t reg;
        int16_t intr_persist;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        reg = i2c_smbus_read_byte_data(client, ISL29035_CMD_REG_1);
        if(reg < 0){
                mutex_unlock(&isl_data.isl_mutex);
		__dbg_read_err("%s",__func__);
                return -1;
        }
        switch(reg & 0x03)
        {
                case 0: intr_persist = 1; break;
                case 1: intr_persist = 4; break;
                case 2: intr_persist = 8; break;
                case 3: intr_persist = 16;break;
        }
        sprintf(buf,"%d\n", intr_persist);
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
}

/*
 * @fn          store_intr_persistency
 *
 * @brief       This function sets the interrupt persistency of
 *              sensor device
 *
 * @return      Returns size of buffer data on success otherwise
 *              returns an error (-1)
 *
 */

static ssize_t store_intr_persistency(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
        int16_t intr_persist;
        int16_t reg;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        intr_persist = simple_strtoul(buf, NULL, 10);

        if (intr_persist == 16){
                intr_persist = ISL29035_PERSIST_SET_16BIT;
        }else if (intr_persist == 8){
                intr_persist = ISL29035_PERSIST_SET_8BIT;
        }else if (intr_persist == 4){
                intr_persist = ISL29035_PERSIST_SET_4BIT;
        }else if (intr_persist == 1){
                intr_persist = ISL29035_PERSIST_SET_1BIT;
        }else {
		__dbg_invl_err("%s",__func__);
                goto err_out;
        }

        reg = i2c_smbus_read_byte_data(client, ISL29035_CMD_REG_1);
        if (reg < 0) {
		__dbg_read_err("%s",__func__);
                goto err_out;
        }

        reg = (reg & ISL29035_PERSIST_BIT_CLEAR) | intr_persist;
        if(i2c_smbus_write_byte_data(client, ISL29035_CMD_REG_1, reg) < 0){
		__dbg_read_err("%s",__func__);
                goto err_out;
        }
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
err_out:
 	mutex_unlock(&isl_data.isl_mutex);
        return -1;
}

#endif

/*
 * @fn          isl29035_i2c_read_word16
 *
 * @brief       This wrapper function reads a word (16-bit) from 
 *              sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32_t isl29035_i2c_read_word16(struct i2c_client *client,
                        uint8_t reg_addr, uint16_t *buf)
{
        short int reg_h;
        short int reg_l;
        reg_l = i2c_smbus_read_byte_data(client, reg_addr);
        if (reg_l < 0)
                return -EINVAL;

        reg_h = i2c_smbus_read_byte_data(client, reg_addr + 1);
        if (reg_h < 0)
                return -EINVAL;
        *buf = (reg_h << 8) | reg_l;
	isl_data.count++;
        return 0;
}

/*
 * @fn          isl29035_i2c_write_word16
 *
 * @brief       This function writes a word (16-bit) to sensor device
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int32_t isl29035_i2c_write_word16(struct i2c_client *client,
                        uint8_t reg_addr, uint16_t *buf)
{
        uint8_t reg_h;
        uint8_t reg_l;

        /* Extract LSB and MSB bytes from data */
        reg_l = *buf & 0xFF;
        reg_h = (*buf & 0xFF00) >> 8;

        if(i2c_smbus_write_byte_data(client, reg_addr, reg_l) < 0)
                return -1;
        if(i2c_smbus_write_byte_data(client, reg_addr + 1, reg_h) < 0)
                return -1;
        return 0;
}

#ifdef ISL29035_INTERRUPT_MODE

/*
 * @fn          show_intr_threshold_high
 *
 * @brief       This function shows the high interrupt threshold
 *              value in adc dec code
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_high(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        uint16_t reg;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        if(isl_data.intr_flag){
                sprintf(buf, "%d\n",isl_data.last_ir_ht);
                goto end;
        }else{
                if(isl29035_i2c_read_word16(client,
                                ISL29035_HT_LBYTE, &reg) < 0){
			__dbg_read_err("%s",__func__);
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
 *              value in adc dec code
 *
 * @return      Returns length of data buffer on success otherwise
 *               returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_high(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
        uint16_t reg;
        struct i2c_client *client = to_i2c_client(dev);

        reg = simple_strtoul(buf, NULL, 10);
        if (reg <= 0 || reg > 65535){
		__dbg_invl_err("%s",__func__);
		goto err_out;
        }
        mutex_lock(&isl_data.isl_mutex);

        if(isl29035_i2c_write_word16(client,
                                ISL29035_HT_LBYTE, &reg) < 0){
		__dbg_read_err("%s",__func__);
		goto err_out;
        }
        if(isl_data.intr_flag)
                isl_data.last_ir_ht = reg;
        else
                isl_data.last_als_ht = reg;

        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
err_out:
        mutex_unlock(&isl_data.isl_mutex);
        return -1;
}

/*
 * @fn          show_intr_threshold_low
 *
 * @brief       This function shows the low interrupt threshold
 *              value in adc dec code
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_intr_threshold_low(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        uint16_t  reg;
        int32_t   ret;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        if(isl_data.intr_flag){
                sprintf(buf, "%d\n",isl_data.last_ir_lt);
                goto end;
        }
        else{
                ret = isl29035_i2c_read_word16(client,
                                ISL29035_LT_LBYTE, &reg);
                if (ret < 0) {
			__dbg_read_err("%s",__func__);
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
 *              adc dec code
 *
 * @return      Returns the length of data buffer on success otherwise 
 *              returns an error (-1)
 *
 */

static ssize_t store_intr_threshold_low(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
        uint16_t reg;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        reg = simple_strtoul(buf, NULL, 10);
        if (reg <= 0 || reg > 65535){
		__dbg_invl_err("%s", __func__);
                mutex_unlock(&isl_data.isl_mutex);
                return -1;
        }
        if(isl29035_i2c_write_word16(client,
                                ISL29035_LT_LBYTE, &reg) < 0){
		__dbg_write_err("%s",__func__);
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
        ret = i2c_smbus_read_byte_data(isl_data.client_data,ISL29035_CMD_REG_2);
        if(ret < 0){
                mutex_unlock(&isl_data.isl_mutex);
		__dbg_read_err("%s",__func__);
                return -1;
        }
        switch(ret & 0x03)
        {
                case 0x00: *als_range = 1000; break;
                case 0x01: *als_range = 4000; break;
                case 0x02: *als_range = 16000; break;
                case 0x03: *als_range = 64000; break;
        }
        return 0;
}

/*
 * @fn          isl_set_sensing_range
 *
 * @brief       This function sets the current optical sensing range of 
 *              sensor device
 *
 * @return      Returns 0 on success otherwise returns 
 *              an error (-EINVAL)
 *
 */
static int32_t isl_set_sensing_range(unsigned long val)
{
        int16_t reg;
        reg = i2c_smbus_read_byte_data(isl_data.client_data,
                                        ISL29035_CMD_REG_2);
        if(reg < 0)
                return -EINVAL;
        reg = ( reg & 0xfc) | val;
        if(i2c_smbus_write_byte_data(isl_data.client_data,
                                        ISL29035_CMD_REG_2, reg) < 0)
                return -EINVAL;
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

static int isl_set_sensing_mode(int mod)
{
        int reg;

        reg = i2c_smbus_read_byte_data(isl_data.client_data,
                                             ISL29035_CMD_REG_1);
        if(reg < 0)
                return -EINVAL;

        isl_data.last_mod = reg;
        reg = (reg & 0x1F) | mod;
        if(i2c_smbus_write_byte_data(isl_data.client_data,
                                       ISL29035_CMD_REG_1 ,reg) < 0)
                return -EINVAL;
        return 0;
}

/*
 * @fn          isl_get_sensing_mode
 *
 * @brief       This function gets the operating mode of sensor
 *              device
 *
 * @return      Returns the mode register value on success
 *              otherwise returns an error (-1)
 *
 */

static int32_t isl_get_sensing_mode(struct i2c_client *client)
{
        int32_t reg ;
        reg = i2c_smbus_read_byte_data(client , ISL29035_CMD_REG_1);
        if(reg < 0)
                return -1;
        return ((reg & 0xf0) >> 5);

}

/*
 * @fn          show_sensing_mode
 *
 * @brief       This function shows the current optical sensing mode
 *              of sensor device
 *
 * @return      Returns the mode on success otherwise returns an error
 *              (-1)
 *
 */

static ssize_t show_sensing_mode(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        int32_t reg;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        reg = isl_get_sensing_mode(client);
        if(reg < 0){
		__dbg_write_err("%s",__func__);
                mutex_unlock(&isl_data.isl_mutex);
                return -1;
        }
        switch(reg)
        {
                case ISL_MOD_POWERDOWN:
                        sprintf(buf, "%s", "pwdn");
                        break;
                case ISL_MOD_ALS_ONCE:
                        sprintf(buf, "%s", "ALS-Once");
                        break;
                case ISL_MOD_IR_ONCE:
                        sprintf(buf, "%s", "IR-Once");
                        break;
                case ISL_MOD_ALS_CONT:
                        sprintf(buf, "%s", "ALS-Continuous");
                        break;
                case ISL_MOD_IR_CONT:
                        sprintf(buf, "%s", "IR-Continuous");
                        break;
        }
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
}

/*
 * @fn          store_sensing_mode
 *
 * @brief       This function sets the current optical sensing mode 
 *              of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise
 *              returns an error (-1)
 *
 */

static ssize_t store_sensing_mode(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
        struct i2c_client *client = to_i2c_client(dev);
        ulong mode;

	if(strict_strtoul(buf, 10, &mode))
		return -EINVAL;
        switch(mode)
	{
	case 0x00:
	case 0x03:
	case 0x04:
	case 0x07: mode = ISL29035_OP_MODE_PWDN_SET;break;
	case 0x01: mode = ISL29035_OP_MODE_ALS_ONCE;
                   isl_data.intr_flag = 0;
		   break;
	case 0x02: mode = ISL29035_OP_MODE_IR_ONCE;break;
        case 0x05: mode = ISL29035_OP_MODE_ALS_CONT;
                   isl_data.intr_flag = 0;
		   break;
	case 0x06: mode = ISL29035_OP_MODE_IR_CONT;
                   isl_data.intr_flag = 1;
		   break;
	default:  __dbg_invl_err("%s",__func__);
		goto err_out;
        }
	
        mutex_lock(&isl_data.isl_mutex);
 	 if(isl_set_sensing_mode(mode) < 0){
		__dbg_write_err("%s",__func__);
		goto err_out;
        }
        if(isl_data.intr_flag){
                if(isl29035_i2c_write_word16(client, ISL29035_LT_LBYTE,
                                        &isl_data.last_ir_lt) < 0)
			__dbg_write_err("%s",__func__);
                if(isl29035_i2c_write_word16(client, ISL29035_HT_LBYTE,
                                        &isl_data.last_ir_ht) < 0)
			__dbg_write_err("%s",__func__);

        }else{
                if(isl29035_i2c_write_word16(client, ISL29035_LT_LBYTE,
                                        &isl_data.last_als_lt) < 0){
			__dbg_write_err("%s",__func__);
                }
                if(isl29035_i2c_write_word16(client, ISL29035_HT_LBYTE,
                                        &isl_data.last_als_ht) < 0 ){
			__dbg_write_err("%s",__func__);
                }
        }
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
err_out:
        mutex_unlock(&isl_data.isl_mutex);
        return -1;
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
		__dbg_read_err("%s",__func__);
                return;
        }
        if(isl_get_sensing_range(&als_range) < 0){
		__dbg_read_err("%s",__func__);
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
                                           else if(val < 0x2)
                                                   als_range = 0x00;
					   else   break;
                                           isl_set_sensing_range(als_range);
                                           break;
                                case 16000:
                                           if(val > 0xC)
                                                   als_range = 0x03;
                                           else if(val < 0x2)
                                                   als_range = 0x01;
                                           else break;
                                           isl_set_sensing_range(als_range);
                                           break;
                                case 64000:
                                           if(val < 0x2)
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
}

/*
 * @fn          show_sensing_range
 *
 * @brief       This function shows the current optical sensing range
 *              of sensor device
 *
 * @return      Returns the length of data written on buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_sensing_range(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        uint32_t als_range=0;

        mutex_lock(&isl_data.isl_mutex);
	if(isl_get_sensing_range(&als_range) < 0){
                __dbg_read_err("%s",__func__);
                return;
        }
        sprintf(buf ,"%d",als_range);
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
}

/*
 * @fn          store_sensing_range
 *
 * @brief       This function stores the current optical sensing range 
 *              of sensor device
 *
 * @return      Returns the length of data buffer on success otherwise 
 *              returns an error (-1)
 *
 */

static ssize_t store_sensing_range(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t count)
{
        ulong val;

        if(strict_strtoul(buf, 10, &val))
                return -EINVAL;
        switch(val)
        {
                case 1000:val = ISL29035_RANGE_1000_SET;break;
                case 4000:val = ISL29035_RANGE_4000_SET;break;
                case 16000:val = ISL29035_RANGE_16000_SET;break;
                case 64000:val = ISL29035_RANGE_64000_SET;break;
                default: mutex_unlock(&isl_data.isl_mutex);
			__dbg_invl_err("%s",__func__);
                         return -1;
        }
        mutex_lock(&isl_data.isl_mutex);
        if(isl_set_sensing_range(val) < 0){
                mutex_unlock(&isl_data.isl_mutex);
		__dbg_write_err("%s",__func__);
                return -1;
        }
        mutex_unlock(&isl_data.isl_mutex);
        return strlen(buf);
}

/*
 * @fn          show_als_data
 *
 * @brief       This function shows the current optical lux of sensor
 *              device
 *
 * @return      Returns data buffer length on success otherwise returns
 *              an error (-1)
 *
 */

static ssize_t show_als_data(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
        uint16_t val;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        if(isl29035_i2c_read_word16(client, ISL29035_DATA_LSB, &val) < 0){
                mutex_unlock(&isl_data.isl_mutex);
		__dbg_read_err("%s",__func__);
                return -1;
        }
#ifndef ISL29035_INTERRUPT_MODE
        autorange(val);
#endif
        mutex_unlock(&isl_data.isl_mutex);
        return sprintf(buf,"%d",val);
}

static uint16_t get_measure(void)
{
	uint16_t comp_off,comp_on,val,lux;
	uint32_t als_range=0;
	uint8_t R=0;
	msleep(100);
	isl_data.count = 0;
	isl_get_sensing_range(&als_range);
        if(isl29035_i2c_read_word16(isl_data.client_data, ISL29035_DATA_LSB, &val) < 0){
                __dbg_read_err("%s",__func__);
                return -1;
        }
	comp_off = (als_range * val)/65535;
	comp_on = val;
	if((isl_data.count == 1))
	goto calc;
	else{
		if(isl_data.count < MAX_COUNT){
		isl_data.count = 0;
		msleep(300);
        		if(isl29035_i2c_read_word16(isl_data.client_data, ISL29035_DATA_LSB, &val) < 0){
                	__dbg_read_err("%s",__func__);
                	return -1;
        		}
		comp_on = val;
		goto calc;
		}
		else
		goto calc;
	}
calc:
	R = (comp_off - comp_on)/comp_off;
	msleep(300);
	if(als_range == 1000 || als_range == 4000)
	lux = (3 * comp_off * (1 - 1 * R));
	else if(als_range == 16000 || als_range == 64000)
	lux = (2 * comp_off * (1 - 1 * R));

return lux;
}

/*
 * @fn          show_ir_data
 *
 * @brief       This function exports the IR Lux value in adc 
 *              code to sysfs
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_data(struct device *dev,
                struct device_attribute *attr, char *buf)
{	
        uint16_t val;
        struct i2c_client *client = to_i2c_client(dev);

        mutex_lock(&isl_data.isl_mutex);
        if(isl29035_i2c_read_word16(client, ISL29035_DATA_LSB, &val) < 0){
		__dbg_read_err("%s",__func__);
                mutex_unlock(&isl_data.isl_mutex);
                return -1;
        }
        mutex_unlock(&isl_data.isl_mutex);
       return sprintf(buf, "%d",val);
}

/*
 * @fn          show_ir_corr
 *
 * @brief       This function exports the IR Correction Lux value
 *              to sysfs
 *
 * @return      Returns the length of data buffer on success 
 *              otherwise returns an error (-1)
 *
 */

static ssize_t show_ir_corr(struct device *dev,
                struct device_attribute *attr, char *buf)
{
        uint16_t lux;
        mutex_lock(&isl_data.isl_mutex);
        lux = get_measure();
        mutex_unlock(&isl_data.isl_mutex);
        return sprintf(buf, "%d",lux);

}


/*******************Attributes of ISL29035 ALS Sensor*********/

/* Device attributes for adc resolution sysfs */
static DEVICE_ATTR( adc_res_bits, ISL29035_SYSFS_PERM,
                show_adc_res_bits, store_adc_res_bits);
/* Device attributes for sensor range sysfs */
static DEVICE_ATTR( sensing_range, ISL29035_SYSFS_PERM,
                show_sensing_range, store_sensing_range);
/* Device attributes for sensor operating mode sysfs */
static DEVICE_ATTR( sensing_mode, ISL29035_SYSFS_PERM,
                show_sensing_mode, store_sensing_mode);
/* Device attributes for latest IR data sysfs */
static DEVICE_ATTR( ir_data, ISL29035_SYSFS_PERM_RONL,show_ir_data,
                 NULL );
/* Device attributes for current ALS data sysfs */
static DEVICE_ATTR( als_data, ISL29035_SYSFS_PERM_RONL, show_als_data,
		 NULL);
/* Device attributes fot IR Offset sysfs */
static DEVICE_ATTR( ir_corr, ISL29035_SYSFS_PERM_RONL, show_ir_corr,
		 NULL);

#ifdef ISL29035_INTERRUPT_MODE 
/* Device attributes for high interrupt threshold sysfs */
static DEVICE_ATTR( intr_threshold_high, ISL29035_SYSFS_PERM ,
                show_intr_threshold_high, store_intr_threshold_high);
/* Device attributes for Low interrupt threshold sysfs */
static DEVICE_ATTR( intr_threshold_low, ISL29035_SYSFS_PERM ,
                show_intr_threshold_low, store_intr_threshold_low);
/* Device attributes for interrupt persistency sysfs */
static DEVICE_ATTR( intr_persistency, ISL29035_SYSFS_PERM ,
                show_intr_persistency, store_intr_persistency);
#endif


/* Structure attributes for all sysfs device files for isl29035 sensor */
static struct attribute *isl29035_attr[] = {

        &dev_attr_adc_res_bits.attr,
        &dev_attr_sensing_range.attr,
        &dev_attr_sensing_mode.attr,
        &dev_attr_ir_data.attr,
        &dev_attr_ir_corr.attr,
#ifdef ISL29035_INTERRUPT_MODE  
        &dev_attr_intr_threshold_high.attr,
        &dev_attr_intr_threshold_low.attr,
        &dev_attr_intr_persistency.attr,
#endif
        &dev_attr_als_data.attr,
        NULL
};

static struct attribute_group isl29035_attr_grp = {

        .attrs = isl29035_attr
};


#ifdef ISL29035_INTERRUPT_MODE  
static irqreturn_t isl_sensor_irq_handler(int irq, void *dev_id)
{
	disable_irq_nosync(isl_data.irq_num);
        schedule_work(&isl_data.work);
        return IRQ_HANDLED;
	
}
/*
 * @fn          sensor_irq_thread
 *
 * @brief       This thread is scheduled by sensor interrupt
 *
 * @return      void
 */


static void isl29035_irq_thread(struct work_struct *work)
{
	int16_t reg;
        uint16_t val;
        reg = i2c_smbus_read_byte_data(isl_data.client_data,
                                                ISL29035_CMD_REG_1);
	/* Read CMD_REG_1 to clear the interrupt */
        if(reg < 0){
                pr_err( "%s :%s :Failed to read ISL29035_CMD_REG_1"
                                        "\n",ISL29035_NAME, __func__);
                goto err;
        }
	
	/* Report the lux*/
        if (isl29035_i2c_read_word16(isl_data.client_data,
                                ISL29035_DATA_LSB, &val) < 0)
                goto err;

	autorange(val);
err:
        enable_irq(isl_data.irq_num);
}
#endif

/*
 * @fn          isl_set_default_config
 *
 * @brief       This function initializes the sensor device
 *              with default values
 *
 * @return      returns 0 on success otherwise returns an 
 *              error (-EINVAL)
 *
 */

static int32_t initialize_isl29035(struct i2c_client *client)
{

        /* Reset the device to avoid previous saturations */
        if(i2c_smbus_write_byte_data(client, ISL29035_CMD_REG_1, 0x00) < 0)
                return -EINVAL;
	
        /* Clear the brown-Out flag */
        if(i2c_smbus_write_byte_data(client, ISL29035_DEV_ID_REG, 0x28) < 0)
                return -EINVAL;
        
        /*   Set Operating Mode: ALS Continuous
             Set Interrupt Persistency: 16 cycles */

        if(i2c_smbus_write_byte_data(client, ISL29035_CMD_REG_1,
				    ISL29035_CMD_REG_1_DEF) < 0)
                return -EINVAL;

        /* Set ADC Resolution : 16-bit
           Set Sensing Range  : 16000 */

        if(i2c_smbus_write_byte_data(client , ISL29035_CMD_REG_2,
				    ISL29035_CMD_REG_2_DEF) < 0)
                return -EINVAL;

#ifdef ISL29035_INTERRUPT_MODE

        /* Writing interrupt low threshold as 0xCCC (5% of max range) */
        if(i2c_smbus_write_byte_data(client, ISL29035_LT_LBYTE,
                                    ISL29035_LT_LBYTE_DEF) < 0)
                return -EINVAL;

        if(i2c_smbus_write_byte_data(client, ISL29035_LT_HBYTE,
                                    ISL29035_LT_HBYTE_DEF) < 0)
                return -EINVAL;

        /* Writing interrupt high threshold as 0xCCCC (80% of max range)  */
        if(i2c_smbus_write_byte_data(client, ISL29035_HT_LBYTE,
                                    ISL29035_HT_LBYTE_DEF) < 0)
                return -EINVAL;

        if(i2c_smbus_write_byte_data(client, ISL29035_HT_HBYTE,
                                    ISL29035_HT_HBYTE_DEF) < 0)
                return -EINVAL;

        isl_data.last_ir_lt = 0xCC;
        isl_data.last_ir_ht = 0x0C;
        isl_data.last_als_lt = 0xCC;
        isl_data.last_als_ht = 0xCC;

#endif
        if(i2c_smbus_read_byte_data(client, ISL29035_CMD_REG_1) < 0)
                return -EINVAL;
        return 0;
}

#ifdef ISL29035_INTERRUPT_MODE
/*
 * @fn          isl_gpio_config
 *
 * @brief       This function is for enable the irq pin nunber and initialize the 
 * 		kernel work queue
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int isl_gpio_config(struct isl29035_platform_data *pdata)
{

        /* Request GPIO for sensor interrupt */
        if(gpio_request(pdata->gpio_irq, "isl29035") < 0)
                goto err;
        
         /*Configure interrupt GPIO direction */
        if(gpio_direction_input(pdata->gpio_irq) < 0)
                goto gpio_err;

        /* Configure the GPIO for interrupt */
        isl_data.irq_num = gpio_to_irq(pdata->gpio_irq);
        if (isl_data.irq_num < 0)
                goto gpio_err;

/*        if(irq_set_irq_type(isl_data.irq_num, IRQ_TYPE_EDGE_FALLING) < 0)
                return gpio_err;
*/
        /* Initialize the work queue */
        INIT_WORK(&isl_data.work,isl29035_irq_thread);

        /* Register irq handler for sensor */
        if(request_irq(isl_data.irq_num, isl_sensor_irq_handler,
                        IRQF_TRIGGER_FALLING , "isl29035", NULL) < 0){ 
                pr_err( "%s: Failed to register irq %d\n",
                                 __func__, isl_data.irq_num);
                goto gpio_err;
        }
	return 0;
gpio_err:
        gpio_free(pdata->gpio_irq);
err:
        return -1;
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
        struct isl29035_platform_data *pdata ;
	int16_t dev_id;
        pdata = client->dev.platform_data;

        if(pdata == NULL)
                return -ENOMEM;

	dev_id = i2c_smbus_read_byte_data( client, ISL29035_DEV_ID_REG );
	if((dev_id & ISL29035_DEV_ID_MASK) != ISL29035_DEVICE_ID){
		pr_err("%s: Failed to recognize the device\n", __func__);
		return -1;
	}
        isl_data.client_data = client;

        /* Initialise the sensor with default configuration */
        if(initialize_isl29035(client)){
		__dbg_write_err("%s: Device initialization failed",__func__);
                return -1;
        }

#ifdef ISL29035_INTERRUPT_MODE
	if(isl_gpio_config(pdata)){	
		pr_err("%s:Gpio configuration failed\n",__func__);
		return -1;
	}
#endif /* ISL29035_INTERRUPT_MODE */
        /* Create sysfs entry */
        if(sysfs_create_group(&client->dev.kobj, &isl29035_attr_grp) < 0){
                pr_err( "%s :%s : Failed to create sysfs"
                                "\n", ISL29035_NAME, __func__);
                return -1;
        }

        isl_data.last_mod = 0;
        mutex_init(&isl_data.isl_mutex);

        /* Clear any previous interrupt */
//        i2c_smbus_read_byte_data(client, ISL29035_CMD_REG_1);

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


static int isl_sensor_suspend(struct i2c_client *client, pm_message_t mesg) 
{
	pr_err("%s:Suspend\n",__func__);
	if(isl_set_sensing_mode(ISL29035_OP_MODE_PWDN_SET) < 0)	
        	return -1;
#ifdef ISL29035_INTERRUPT_MODE
        disable_irq_nosync(isl_data.irq_num);
#endif
        return 0;

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
	pr_err("%s:Resume\n",__func__);
	if(isl_set_sensing_mode(isl_data.last_mod) < 0)	
        	return -1;
	msleep(100);

#ifdef ISL29023_INTERRUPT_MODE  
        enable_irq(isl_data.irq_num);
#endif
        return 0;
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
	sysfs_remove_group(&client->dev.kobj, &isl29035_attr_grp);
#ifdef ISL29035_INTERRUPT_MODE
	free_irq(isl_data.irq_num, NULL);
	gpio_free(ISL29035_INTR_GPIO);
#endif
	return 0;

}

/* Driver information sent to kernel*/
static struct i2c_driver isl29035_sensor_driver = {
	.driver = {
		.name  = "isl29035",
		.owner = THIS_MODULE,
	},
	.probe		= isl_sensor_probe,
	.remove		= isl_sensor_remove,
	.suspend 	= isl_sensor_suspend,
	.resume		= isl_sensor_resume,
	.id_table	= isl_device_table,
};

/*
 *  @fn          isl29035_init
 *
 *  @brief       This function registers the driver to kernel
 *
 *  @return      Returns 0 on success otherwise returns an error (-1)
 *
 */

static int __init isl29035_init(void)
{
	return i2c_add_driver(&isl29035_sensor_driver);
}

/*
 * @fn          isl29035_exit
 *
 * @brief       This function is called to cleanup driver entry
 *
 * @return      Void
 *
 */

static void __exit isl29035_exit(void)
{
	i2c_del_driver(&isl29035_sensor_driver);
} 

MODULE_AUTHOR	("sanoj.kumar@vvdntech.com");
MODULE_LICENSE	("GPLv2");
MODULE_DESCRIPTION ("Device driver for ISL29035 ALS/IR Sensor");

module_init(isl29035_init);
module_exit(isl29035_exit);
