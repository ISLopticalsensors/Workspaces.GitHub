/*
 * isl29030.c - Intersil ISL29030  ALS & Proximity Driver
 *
 * By Intersil Corp
 * Michael DiGioia
 *
 * Based on isl29011.c
 *  by Michael DiGioia <mdigioia@intersil.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/input-polldev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/pm.h>
//#include <linux/pm_runtime.h>
#include <linux/i2c/isl29030.h>

static int isl29030_last_lmod;
static int isl29030_last_pmod;
static unsigned int isl29030_lux_factor = 1;
static unsigned short isl29030_default_proxht = ISL_DEFAULT_HT;
static unsigned short isl29030_default_proxlt = ISL_DEFAULT_LT;

static struct device      *isl29030_hwmon_dev;
static struct i2c_client  *isl29030_i2c_client;

struct isl29030_data {
    struct input_dev            *isl29030_idev;
    struct i2c_client           *isl29030_i2c_client;

    struct workqueue_struct     *isl29030_workqueue;
    struct work_struct          amb_prox_work;

    spinlock_t                  lock;
    int         	       		irq;

    int                         (*get_amb_prox_irq_state)(void);
    void                        (*clear_amb_prox_irq)(void);
} *isl_data;

static DEFINE_MUTEX(mutex);

/* --------------------------------------------------------------
 *
 *
 *   HELPER FUNCTIONS
 *
 *
 * -------------------------------------------------------------- */

//Set Mode of operation
static int isl_set_mod(struct i2c_client *client, int func, int mod)
{
    int ret, val;

    /* set operation mod */
    val = i2c_smbus_read_byte_data(client, REG_CMD_1);
    if (val < 0)
        return -EINVAL;

    if (func == ISL_FUNC_PROX)
    {
        if (mod == ISL_MOD_ENABLE)
            val |= PROX_EN_MASK;
        else if (mod == ISL_MOD_DISABLE)
            val &= ~PROX_EN_MASK;
        printk(KERN_INFO MODULE_NAME ": prox mode now %d\n", mod);
    }

    if (func == ISL_FUNC_ALS)
    {
        if (mod == ISL_MOD_ENABLE)
            val |= ALS_EN_MASK;
        else if (mod == ISL_MOD_DISABLE)
            val &= ~ALS_EN_MASK;
        printk(KERN_INFO MODULE_NAME ": als mode now %d\n", mod);
    }

    if (mod == ISL_MOD_POWERDOWN)
    {
        val &= ~ALS_EN_MASK;
        val &= ~PROX_EN_MASK;
    }

    ret = i2c_smbus_write_byte_data(client, REG_CMD_1, val);
    if (ret < 0)
        return -EINVAL;

    if (mod != ISL_MOD_POWERDOWN)
        if (func == ISL_FUNC_PROX)
            isl29030_last_pmod = mod;
        if (func == ISL_FUNC_ALS)
            isl29030_last_lmod = mod;

    return mod;
}

static int isl_get_mod(struct i2c_client *client, int func)
{
    int val, retval = ISL_MOD_DISABLE;

    val = i2c_smbus_read_byte_data(client, REG_CMD_1);
    if (val < 0)
        return -EINVAL;

    if (func == ISL_FUNC_PROX)
        if (val &= PROX_EN_MASK)
            retval = ISL_MOD_ENABLE;

    if (func == ISL_FUNC_ALS)
        if (val &= ALS_EN_MASK)
            retval = ISL_MOD_ENABLE;

    return retval;
}

static void isl_adjust_als_thresholds( unsigned int newval )
{
    int ret = 0;
    unsigned int adj_ht = 0, adj_lt = 0;
    unsigned char low_reg = 0, low_high_reg = 0, high_reg = 0;

    // Initialize to the high and low boundaries, otherwise key off the most
    // recent value; since ADC is 12-bit we will never see anything over 0x0FFF
    // organically, so 0xFFFF should be usable as a key
    if (newval == 0xFFFF) {
        adj_lt = ISL_INIT_BOUND_LO;
        adj_ht = ISL_INIT_BOUND_HI;
    } else {
        if (newval < ISL_ADJ_BOUND)
            adj_lt = 0x000;
        else
            adj_lt = newval - ISL_ADJ_BOUND;

        if ((newval + ISL_ADJ_BOUND) > 0x0FFF)  // 12-bit, so check for overflow of 0x0FFF
            adj_ht = 0xFFF;
        else
            adj_ht = newval + ISL_ADJ_BOUND;
    }

    low_reg = adj_lt & 0x00FF;							
    low_high_reg = (adj_lt & 0x0F00) >> 8;				
    low_high_reg |= (adj_ht & 0x000F) << 4;				
    high_reg = (adj_ht & 0x0FF0) >> 4;					
//    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_LOW_ALS, low_reg);
    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_LOW_ALS, 0xcc);
    if (ret < 0) {
        printk(KERN_ERR "error writing low als threshold\n");
        return;
    }
//    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_LOW_HIGH_ALS, low_high_reg);
    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_LOW_HIGH_ALS, 0xc0);
    if (ret < 0) {
        printk(KERN_ERR "error writing highlow als threshold\n");
        return;
    }
//    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_HIGH_ALS, high_reg);
    ret = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_INT_HIGH_ALS, 0xcc);
    if (ret < 0) {
        printk(KERN_ERR "error writing high als threshold\n");
        return;
    }
}

static long isl_calculate_lux( unsigned int adc, unsigned int range )
{
    long retval = 0;
    if (range == 1) {
        retval = ALS_HIGH_FACTOR * adc;
    }
    else {
        retval = ALS_LOW_FACTOR * adc;
    }

    retval = isl29030_lux_factor * retval;

    return retval;
}

static unsigned short isl_read_lux( struct i2c_client *client, unsigned short *range )
{
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short msb_read = 0, lsb_read = 0;
    short val = 0;
    /*MSI VOLTRON END */
    unsigned short ret_val = 0, error = 0;

    val = i2c_smbus_read_byte_data(client, REG_CMD_1);
    if (val < 0) {
        printk(KERN_ERR "error reading reg cmd1\n");
        return -EINVAL;
    }

    if ((val & ALS_RANGE_HIGH_MASK) == ALS_RANGE_HIGH_MASK)
        *range = 1;
    else
        *range = 0;

    msb_read = i2c_smbus_read_byte_data(client, REG_DATA_MSB_ALS);
    if (msb_read < 0) {
        printk(KERN_ERR "error reading msb data\n");
        return -EINVAL;
    }
//    msb_read &= 0x000F;  // only four bits for 12-bit conversion

    lsb_read = i2c_smbus_read_byte_data(client, REG_DATA_LSB_ALS);
    if (lsb_read < 0) {
        printk(KERN_ERR "error reading lsb data\n");
        return -EINVAL;
    }
//    lsb_read &= 0x00FF;

    ret_val = (msb_read << 8) | lsb_read;

    /* we may need to clear this flag to get new values */
    val = i2c_smbus_read_byte_data(client, REG_CMD_2);
    if (val < 0) {
        printk(KERN_ERR "error reading reg cmd2\n");
        return -EINVAL;
    }
    val &= ~(ALS_INT_CLEAR);
    error = i2c_smbus_write_byte_data(client, REG_CMD_2, val);
    if (error < 0) {
        printk(KERN_ERR "error writing reg cmd2\n");
        return -EINVAL;
    }

    return ret_val;
}

/* --------------------------------------------------------------
 *
 *
 *   INTERRUPT FUNCTIONS
 *
 *
 * -------------------------------------------------------------- */

static int read_and_report_prox( struct isl29030_data *isl )
{
    int retval = 0;
    unsigned short prox_result;
    //unsigned short prox_ht, prox_lt;
    int val, error;

    mutex_lock(&mutex);

    val = i2c_smbus_read_byte_data(isl29030_i2c_client, REG_DATA_PROX);
    if (val < 0)
        goto prox_out;
    prox_result = val;

#if 0
    // commenting this all out because of how the FPGA processes interrupts
    // we can't really check high and low thresholds anyway, just report
    // the abs value

    val = i2c_smbus_read_byte_data(isl29030_i2c_client, REG_INT_LOW_PROX);
    if (val < 0)
        goto prox_out;
    prox_lt = val;

    val = i2c_smbus_read_byte_data(isl29030_i2c_client, REG_INT_HIGH_PROX);
    if (val < 0)
        goto prox_out;
    prox_ht = val;

    /* now check to see if we've hit a threshold */
    if (prox_result > prox_ht) {
        // printk(KERN_INFO "%02X higher than %02X\n", prox_result, prox_ht);
    }
    else if (prox_result < prox_lt) {
        // printk(KERN_INFO "%02X lower than %02X\n", prox_result, prox_lt);
    }
    else {
        // printk(KERN_INFO "%02X higher than %02X lower than %02X\n", prox_lt, prox_result, prox_ht);
    }
#endif
    //printk(KERN_INFO "read_and_report_prox prox result prox_result = %d \n", prox_result);
    input_report_abs(isl->isl29030_idev, ABS_DISTANCE, prox_result);
    input_sync(isl->isl29030_idev);

    /* we may need to clear this flag to get new values */
    val = i2c_smbus_read_byte_data(isl29030_i2c_client, REG_CMD_2);
    if (val < 0)
        goto prox_out;
    val &= ~(PROX_INT_CLEAR);
    error = i2c_smbus_write_byte_data(isl29030_i2c_client, REG_CMD_2, val);
    if (error < 0)
        goto prox_out;

prox_out:
    mutex_unlock(&mutex);
    return retval;
}

static int read_and_report_lux( struct isl29030_data *isl )
{
    int retval = 0;
    long retlux = 0;
    unsigned short range = 0;

    mutex_lock(&mutex);

    retval = isl_read_lux( isl29030_i2c_client, &range );
    if (retval < 0)
        goto als_out;
    isl_adjust_als_thresholds( retval );

    retlux = isl_calculate_lux( retval, range );

    input_event(isl->isl29030_idev, EV_LED, LED_MISC, retlux);
    input_sync(isl->isl29030_idev);

    retval = 0;

als_out:
    mutex_unlock(&mutex);
    return retval;
}

static void isl29030_irq_worker( struct work_struct *work )
{
    struct isl29030_data *isl = container_of(work, struct isl29030_data, amb_prox_work);
    int result = 0;
    short reg_read = 0;
	short int ret;

	mutex_lock(&mutex);
    /* read to determine which interrupt has tripped (or both) */
    reg_read = i2c_smbus_read_byte_data(isl29030_i2c_client, REG_CMD_2);
	mutex_unlock(&mutex);
	//	printk("done!\n");
    if (reg_read < 0) {
        printk(KERN_ERR "failure to read status");
        goto work_out;
    }
/*    if(i2c_smbus_write_byte_data(isl29030_i2c_client,REG_CMD_2,
                                (ret & 0x66)) < 0){
                printk(KERN_ERR"%s", __func__);
                goto work_out;
        }*/


    if ((reg_read & ALS_INT_CLEAR) == ALS_INT_CLEAR) {
     //   result = read_and_report_lux(isl)
	i2c_smbus_write_byte_data(isl29030_i2c_client, REG_CMD_2,0x05 );
    }

    if ((reg_read & PROX_INT_CLEAR) == PROX_INT_CLEAR) {
	i2c_smbus_write_byte_data(isl29030_i2c_client, REG_CMD_2,0x41 );
     //   result = read_and_report_prox(isl);
    }

//    if (unlikely(result != 0)) {
//        printk(KERN_ERR "fail\n");
//    }
work_out:
    enable_irq(isl_data->irq);
}

static irqreturn_t isl29030_irq( int irq, void *handle )
{
    unsigned long flags;
//    struct isl29030_data *isl = handle;
    disable_irq_nosync(isl_data->irq);
    /* the reading of the samples can be time-consuming if using
     * a slow i2c, so the work is done in a queue */
    queue_work(isl_data->isl29030_workqueue, &isl_data->amb_prox_work);
#if 0  // We have done that in mask_irq
    if (isl->clear_amb_prox_irq) {
        isl->clear_amb_prox_irq();
    }
#endif
//	enable_irq(irq);
    return IRQ_HANDLED;
}


/* --------------------------------------------------------------
 *
 *
 *   SYSFS FUNCTIONS
 *
 *
 * -------------------------------------------------------------- */

static ssize_t isl_pmod_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    int ret_val;
    unsigned long val;

    if (strict_strtoul(buf, 10, &val))
        return -EINVAL;
    if (val > 2)
        return -EINVAL;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    ret_val = isl_set_mod(client, ISL_FUNC_PROX, val);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (ret_val < 0)
        return ret_val;
    return count;
}

static ssize_t isl_pmod_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    int val;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    val = isl_get_mod(client, ISL_FUNC_PROX);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    dev_dbg(dev, "%s: mod: 0x%.2x\n", __func__, val);

    if (val < 0)
        return val;

    return sprintf(buf, "%d\n", val);
}

static ssize_t isl_lmod_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    int ret_val;
    unsigned long val;

    if (strict_strtoul(buf, 10, &val))
        return -EINVAL;
    if (val > 2)
        return -EINVAL;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    ret_val = isl_set_mod(client, ISL_FUNC_ALS, val);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (ret_val < 0)
        return ret_val;
    return count;
}

static ssize_t isl_lmod_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    int val;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    val = isl_get_mod(client, ISL_FUNC_ALS);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    dev_dbg(dev, "%s: mod: 0x%.2x\n", __func__, val);

    if (val < 0)
        return val;

    return sprintf(buf, "%d\n", val);
}

static ssize_t isl_range_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short ret_val = 0;
    /*MSI VOLTRON END */
    unsigned long val = 0;
    int reg_read = 0;

    if (strict_strtoul(buf, 10, &val))
        return -EINVAL;

    switch (val) {
        case 0:
        case 1:
            break;
        default:
            return -EINVAL;
    }

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    reg_read = i2c_smbus_read_byte_data(client, REG_CMD_1);
    if (val)
        reg_read |= ALS_RANGE_HIGH_MASK;
    else
        reg_read &= ~ALS_RANGE_HIGH_MASK;
    ret_val = i2c_smbus_write_byte_data(client, REG_CMD_1, reg_read);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (ret_val < 0)
        return ret_val;
    return count;
}

static ssize_t isl_range_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    int val;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    val = i2c_smbus_read_byte_data(client, REG_CMD_1);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    val &= ALS_RANGE_HIGH_MASK;
    dev_dbg(dev, "%s: range: 0x%.2x\n", __func__, val);

    if (val < 0)
        return -EINVAL;
    if (val == ALS_RANGE_HIGH_MASK)
        return sprintf(buf, "high\n");
    else
        return sprintf(buf, "low\n");
}

static ssize_t isl_factor_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long val;

    if (strict_strtoul(buf, 10, &val))
        return -EINVAL;

    // check boundaries
    if ((val < 1) || (val > ALS_MAX_LUX_FACTOR))
        return -EINVAL;

    isl29030_lux_factor = val;

    return count;
}

static ssize_t isl_factor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", isl29030_lux_factor);
}

static ssize_t isl_proxlt_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short ret_val = 0;
    /*MSI VOLTRON BEGIN */
    unsigned long val = 0;

    if (strict_strtoul(buf, 16, &val))
        return -EINVAL;

//    if (val > 255)
	if(val > 0xFF || val < 0x00)
        return -EINVAL;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    ret_val = i2c_smbus_write_byte_data(client, REG_INT_LOW_PROX, val);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (ret_val < 0)
        return ret_val;
    return count;
}

static ssize_t isl_proxlt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short val = 0;
    /*MSI VOLTRON END */

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    val = i2c_smbus_read_byte_data(client, REG_INT_LOW_PROX);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (val < 0)
        return -EINVAL;
    else
        return sprintf(buf, "%02X\n", val);
}

static ssize_t isl_proxht_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short ret_val = 0;
    /*MSI VOLTRON END */
    unsigned long val = 0;

    if (strict_strtoul(buf, 16, &val))
        return -EINVAL;

    if (val > 255)
        return -EINVAL;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    ret_val = i2c_smbus_write_byte_data(client, REG_INT_HIGH_PROX, val);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (ret_val < 0)
        return ret_val;
    return count;
}

static ssize_t isl_proxht_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    /*MSI VOLTRON BEGIN */
    /*Comparison of unsigned value against 0 is always false,return value is -ve*/
    short val = 0;
    /*MSI VOLTRON END */

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);
    val = i2c_smbus_read_byte_data(client, REG_INT_HIGH_PROX);
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    if (val < 0)
        return -EINVAL;
    else
        return sprintf(buf, "%02X\n", val);
}

static ssize_t isl_lux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
//    int ret_val = 0;
    unsigned short ret_val = 0;
    unsigned short range = 0;
    long output = 0;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);

    ret_val = isl_read_lux( client, &range );
    if (ret_val < 0)
        goto err_exit;

    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);

    /* lux calculation depends on range value */
//    output = isl_calculate_lux( ret_val, range );

    return sprintf(buf, "%d\n", (ret_val & 0x0FFF));

err_exit:
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);
    return -EINVAL;
}

static ssize_t isl_distance_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    int val, error;
    int output = 0;

    mutex_lock(&mutex);
    //pm_runtime_get_sync(dev);

    val = i2c_smbus_read_byte_data(client, REG_DATA_PROX);
    if (val < 0)
        goto err_exit;
    output = val;

    /* we may need to clear this flag to get new values */
    val = i2c_smbus_read_byte_data(client, REG_CMD_2);
    if (val < 0)
        goto err_exit;
    val &= ~(PROX_INT_CLEAR);
    error = i2c_smbus_write_byte_data(client, REG_CMD_2, val);
    if (error < 0)
        goto err_exit;

    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);
    return sprintf(buf, "%02X\n", output);

err_exit:
    //pm_runtime_put_sync(dev);
    mutex_unlock(&mutex);
    return -EINVAL;
}

unsigned char last_isl29030_register = REG_CMD_1;
static ssize_t isl_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = isl29030_i2c_client;
    int reg_value = 0;

    mutex_lock(&mutex);
    reg_value = i2c_smbus_read_byte_data(client, last_isl29030_register);
    mutex_unlock(&mutex);
    return sprintf(buf, "%02X:%02X\n", last_isl29030_register, reg_value);
}

static ssize_t isl_reg_store(struct device *dev, struct device_attribute *attr,
                    const char *buf, size_t count)
{
    struct i2c_client *client = isl29030_i2c_client;
    int reg_value = 0;
    int len = 0;
    unsigned long conversion;
    char temp_buf[3] = { '\0','\0','\0' };
    unsigned char temp_val = 0, temp_mask = 0, read = 0;
    // position = simple_strtoul(buf, NULL, 10);
    len = strlen(buf) - 1;  // len is #bytes + the carriage return, so "strip" that off to determine length
    mutex_lock(&mutex);
    switch (len)
    {
        case 2:
            strncpy(&temp_buf[0], buf, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            last_isl29030_register = (unsigned char)conversion;
            break;
        case 4:
            strncpy(&temp_buf[0], buf, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            last_isl29030_register = (unsigned char)conversion;
            strncpy(&temp_buf[0], buf+2, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            temp_val = (unsigned char)conversion;

            i2c_smbus_write_byte_data(client, last_isl29030_register, temp_val);
            break;
        case 6:
            strncpy(&temp_buf[0], buf, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            last_isl29030_register = (unsigned char)conversion;
            strncpy(&temp_buf[0], buf+2, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            temp_val = (unsigned char)conversion;
            strncpy(&temp_buf[0], buf+4, 2);
            if (strict_strtoul( temp_buf, 16, &conversion ))
                return -EINVAL;
            temp_mask = (unsigned char)conversion;

            reg_value = i2c_smbus_read_byte_data(client, last_isl29030_register);
            read = (reg_value & ~temp_mask) | (temp_val & temp_mask);
            i2c_smbus_write_byte_data(client, last_isl29030_register, read);
            break;
        default:
            // bad, do not set anything
            last_isl29030_register = REG_CMD_1;
            break;
    }
    mutex_unlock(&mutex);
    return count;
}

static DEVICE_ATTR(pmod, S_IRUGO | S_IWUSR, isl_pmod_show, isl_pmod_store);
static DEVICE_ATTR(lmod, S_IRUGO | S_IWUSR, isl_lmod_show, isl_lmod_store);
static DEVICE_ATTR(range, S_IRUGO | S_IWUSR, isl_range_show, isl_range_store);
static DEVICE_ATTR(factor, S_IRUGO | S_IWUSR, isl_factor_show, isl_factor_store);
static DEVICE_ATTR(proxlt, S_IRUGO | S_IWUSR, isl_proxlt_show, isl_proxlt_store);
static DEVICE_ATTR(proxht, S_IRUGO | S_IWUSR, isl_proxht_show, isl_proxht_store);
static DEVICE_ATTR(reg, S_IRUGO | S_IWUSR, isl_reg_show, isl_reg_store);
static DEVICE_ATTR(lux, S_IRUGO, isl_lux_show, NULL);
static DEVICE_ATTR(distance, S_IRUGO, isl_distance_show, NULL);

static struct attribute *mid_att_isl[] = {
    &dev_attr_lmod.attr,
    &dev_attr_pmod.attr,
    &dev_attr_range.attr,
    &dev_attr_factor.attr,
    &dev_attr_proxlt.attr,
    &dev_attr_proxht.attr,
    &dev_attr_reg.attr,
    &dev_attr_lux.attr,
    &dev_attr_distance.attr,
    NULL
};

static struct attribute_group m_isl_gr = {
    .name = "isl29030",
    .attrs = mid_att_isl
};

/* --------------------------------------------------------------
 *
 *
 *   INITIALIZATION, PROBE, LINUX BASICS
 *
 *
 * -------------------------------------------------------------- */

static int isl_set_default_config(struct i2c_client *client)
{
    int ret=0;
    unsigned char reg_out = 0;

    // APP note to properly power on chip
    reg_out = 0x00;
    ret = i2c_smbus_write_byte_data(client, REG_CMD_1, reg_out);
    if (ret < 0)
        return -EINVAL;
    ret = i2c_smbus_write_byte_data(client, REG_TEST2, 0x29);
    if (ret < 0)
        return -EINVAL;
    ret = i2c_smbus_write_byte_data(client, REG_TEST1, 0x00);
    if (ret < 0)
        return -EINVAL;
    ret = i2c_smbus_write_byte_data(client, REG_TEST2, 0x00);
    if (ret < 0)
        return -EINVAL;

    // sleep after this app note functionality
    msleep(2);

    // Now go ahead and start initializing

    // default high, range = 1
    reg_out |= ALS_RANGE_HIGH_MASK;//0x02

    // default 7 pulse, continuous
    reg_out &= ISL_CLEAR_PULSE_RATE;//0x02
    reg_out |= (ISL_PULSE_RATE_CONT << 4);//0x62

//    ret = i2c_smbus_write_byte_data(client, REG_CMD_1, reg_out);
    ret = i2c_smbus_write_byte_data(client, REG_CMD_1, 0x66);
    if (ret < 0)
        return -EINVAL;

    // initialize the prox wait period
    reg_out = 0x00;
    reg_out |= (ISL_DEFAULT_PROX_PRST << 5);//0x60
    ret = i2c_smbus_write_byte_data(client, REG_CMD_2, reg_out);
    if (ret < 0)
        return -EINVAL;
/*        // Writing interrupt low threshold as 0xCCC (5% of max range) 
        if(i2c_smbus_write_byte_data(client, REG_INT_LOW_ALS, 0x0C) < 0)
                return -EINVAL;

        if(i2c_smbus_write_byte_data(client, REG_INT_LOW_HIGH_ALS, 0xC0) < 0 )
                return -EINVAL;

        // Writing interrupt high threshold as 0xCCCC (80% of max range)  
       if(i2c_smbus_write_byte_data(client, REG_INT_HIGH_ALS, 0xCC) < 0)

                return -EINVAL;*/

    // initialize the lux thresholds
    isl_adjust_als_thresholds( 0xFFFF );

    // initialize the prox thresholds
//    ret = i2c_smbus_write_byte_data(client, REG_INT_LOW_PROX, isl29030_default_proxlt);
    ret = i2c_smbus_write_byte_data(client, REG_INT_LOW_PROX, 0x0c);
    if (ret < 0)
        return -EINVAL;

//    ret = i2c_smbus_write_byte_data(client, REG_INT_HIGH_PROX, isl29030_default_proxht);
    ret = i2c_smbus_write_byte_data(client, REG_INT_HIGH_PROX, 0xcc);
    if (ret < 0)
        return -EINVAL;

    printk(KERN_INFO MODULE_NAME ": %s isl29030 set_default_config call, \n", __func__);

    return 0;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int isl29030_detect(struct i2c_client *client, int kind,
                          struct i2c_board_info *info)
{
    struct i2c_adapter *adapter = client->adapter;

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        return -ENODEV;

    printk(KERN_INFO MODULE_NAME ": %s isl29030 detact call, kind:%d type:%s addr:%x \n",
           __func__, kind, info->type, info->addr);

    if (kind <= 0) {
        int vendor, device, revision;

        vendor = i2c_smbus_read_word_data(client, ISL29030_REG_VENDOR_REV);
        vendor >>= 8;
        revision = vendor >> ISL29030_REV_SHIFT;
        vendor &= ISL29030_VENDOR_MASK;
        if (vendor != ISL29030_VENDOR)
            return -ENODEV;
		mutex_lock(&mutex);
        device = i2c_smbus_read_word_data(client, ISL29030_REG_DEVICE);
		mutex_unlock(&mutex);
        device >>= 8;
        if (device != ISL29030_DEVICE)
            return -ENODEV;

        if (revision != ISL29030_REV)
            dev_info(&adapter->dev, "Unknown revision %d\n", revision);
    }
    else
        dev_dbg(&adapter->dev, "detection forced\n");

    strlcpy(info->type, "isl29030", I2C_NAME_SIZE);

    return 0;
}

static int
isl29030_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int res = 0;
    unsigned char ret = 0;
//    struct isl29030_data *isl;
    struct isl29030_platform_data *pdata = client->dev.platform_data;

    isl29030_i2c_client = client;
	
    mutex_init(&mutex);

    dev_info(&client->dev, "%s: ISL 030 chip found\n", client->name);

    printk(KERN_INFO MODULE_NAME ": %s isl29030 probe call, ID= %s\n",__func__, id->name);
    res = isl_set_default_config(client);
    if (res < 0) {
        printk(KERN_INFO MODULE_NAME ": %s isl29030 set default config failed\n", __func__);
        return -EINVAL;
    }

    isl_data = kzalloc(sizeof(struct isl29030_data), GFP_KERNEL);
    if (!isl_data) {
        res = -ENOMEM;
        dev_err(&client->dev, "error allocating initial structure\n");
        goto err_out;
    }
    isl_data->isl29030_i2c_client = client;
    i2c_set_clientdata(client, isl_data);

    isl29030_hwmon_dev = hwmon_device_register(&client->dev);
    if (!isl29030_hwmon_dev) {
        res = -ENOMEM;
        dev_err(&client->dev, "error when register hwmon device\n");
        goto err_alloc_struct;
    }

    isl_data->isl29030_idev = input_allocate_device();
    if (!(isl_data->isl29030_idev)) {
        res = -ENOMEM;
        dev_err(&client->dev, "alloc device failed!\n");
        goto err_alloc_device;
    }

    /* start filling in all these structures with meaningful data */
    isl_data->get_amb_prox_irq_state = pdata->get_amb_prox_irq_state;
    isl_data->clear_amb_prox_irq     = pdata->clear_amb_prox_irq;

    isl_data->isl29030_idev->name = "isl29030";
    isl_data->isl29030_idev->phys = "isl29030/input0";
//    isl->isl29030_idev->id.bustype = BUS_I2C;
    isl_data->isl29030_idev->evbit[0] = BIT_MASK(EV_ABS) | BIT_MASK(EV_LED);


    // input_set_capability(isl->isl29030_idev, EV_ABS, ABS_DISTANCE);
    //t.y@0807: change the the FUZZ aand FLAT to 1
    //input_set_abs_params(isl->isl29030_idev, ABS_DISTANCE, 0, 255, 0, 0);
    input_set_abs_params(isl_data->isl29030_idev, ABS_DISTANCE, 0, 255, 1, 1);
    input_set_capability(isl_data->isl29030_idev, EV_LED, LED_MISC);

    isl_data->isl29030_workqueue = create_singlethread_workqueue("isl29030");
    if (isl_data->isl29030_workqueue == NULL) {
        dev_err(&client->dev, "failed to create workqueue\n");
        goto err_register_device;
    }
/* Request GPIO for sensor interrupt */
        if(gpio_request(pdata->gpio_irq, "isl29030") < 0){
                pr_err( "%s :%s :Failed to request GPIO\n"
                            "\n","ISL29030", __func__);
                return -1;
        }

        /* Configure interrupt GPIO as input pin */
        if(gpio_direction_input(pdata->gpio_irq) < 0){
                pr_err("%s : %s:Failed to set gpio direction\n",
                                        "ISL29030", __func__);
                return -1;
        }

        /* Configure the GPIO for interrupt */
        isl_data->irq = gpio_to_irq(pdata->gpio_irq);
        if (isl_data->irq < 0){
                pr_err( "%s : %s:Failed to get IRQ number for"
                                        , "ISL29030", __func__);
                return -1;
        }

        if(irq_set_irq_type(isl_data->irq, IRQ_TYPE_EDGE_FALLING) < 0){
                pr_err("%s:%s:Failed to set irq type\n", "ISL29030", __func__);
                return -1;
        }


    res = input_register_device(isl_data->isl29030_idev);
    if (res) {
        dev_err(&client->dev, "register device failed!\n");
        res = -EINVAL;
        goto err_irq;
    }

    INIT_WORK(&isl_data->amb_prox_work, isl29030_irq_worker);

//    isl->irq = client->irq;

    res = request_irq(isl_data->irq, isl29030_irq, IRQF_TRIGGER_FALLING, client->dev.driver->name, NULL);
    if (res < 0) {
        dev_err(&client->dev, "irq %d busy?\n", isl_data->irq);
        goto err_workqueue;
    }
    printk("isl register irq %d success \n", isl_data->irq);
    res = sysfs_create_group(&isl_data->isl29030_idev->dev.kobj, &m_isl_gr);
    if (res) {
        printk(KERN_INFO MODULE_NAME ": %s isl29030 device create file failed\n", __func__);
        res = -EINVAL;
        goto err_register_input;
    }

/*MSI: VOLTRON :BEGIN*/
/* proximity enabled by default*/
/*	mutex_lock(&mutex);
    isl_set_mod(client, ISL_FUNC_ALS, ISL_MOD_DISABLE);
    isl_set_mod(client, ISL_FUNC_PROX, ISL_MOD_ENABLE);
	mutex_unlock(&mutex);*/
    //pm_runtime_enable(&client->dev);
/*MSI: VOLTRON :END*/

    dev_dbg(&client->dev, "isl29030 probe succeed!\n");

/*
        mutex_lock(&mutex);
        ret = i2c_smbus_read_byte_data(client, REG_CMD_1);
        printk("CONFIG_REG_1 0x01 0x66 %x %d\n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_CMD_2);
        printk("CONFIG_REG_2 0x02 0x60 %x %d\n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_TEST1);
        printk("test1 0x0E 0x00 %x %d \n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_TEST2);
        printk("test2 0x0F 0x00 %x %d\n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_INT_LOW_PROX);
        printk("PROX_REG_low 0x03 0x00 %x %d\n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_INT_HIGH_PROX);
        printk("PROX_REG_high 0x04 0x00%x %d\n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_INT_LOW_ALS);
        printk("ALSIR_TH1 0x05 0x0000 %x %d \n",ret,ret);
        ret = i2c_smbus_read_byte_data(client, REG_INT_LOW_HIGH_ALS);
        printk("ALSIR_TH2 0x06 0x00C0 %x %d\n",ret,ret);
	ret = i2c_smbus_read_byte_data(client,REG_INT_HIGH_ALS );
	printk("ALSIR_TH3 0x07 0x0000 %x %d\n",ret,ret);

        mutex_unlock(&mutex);*/
	i2c_smbus_write_byte_data(client,REG_CMD_2,0x04);

    return 0;

err_register_input:
    input_unregister_device(isl_data->isl29030_idev);
err_irq:
    free_irq(isl_data->irq, isl_data);
err_workqueue:
    destroy_workqueue(isl_data->isl29030_workqueue);
err_register_device:
    input_free_device(isl_data->isl29030_idev);
err_alloc_device:
    hwmon_device_unregister(&client->dev);
err_alloc_struct:
    kfree(isl_data);
err_out:
    return res;
}

static int isl29030_remove(struct i2c_client *client)
{
    struct isl29030_data *priv = i2c_get_clientdata(client);
    //__pm_runtime_disable(&client->dev, false);

    sysfs_remove_group(&priv->isl29030_idev->dev.kobj, &m_isl_gr);
    input_unregister_device(priv->isl29030_idev);
    free_irq(client->irq, priv);
    destroy_workqueue(priv->isl29030_workqueue);
    input_free_device(priv->isl29030_idev);
    hwmon_device_unregister(isl29030_hwmon_dev);
    kfree(priv);
    kfree(isl_data);

    printk(KERN_INFO MODULE_NAME ": %s isl29030 remove call, \n", __func__);
    return 0;
}

static struct i2c_device_id isl29030_id[] = {
    {"isl29030", 0},
    {}
};

static int isl29030_runtime_suspend(struct device *dev)
{
    struct i2c_client *client = isl29030_i2c_client;
    dev_dbg(dev, "suspend\n");
    isl_set_mod(client, ISL_FUNC_BOTH, ISL_MOD_POWERDOWN);
    return 0;
}

static int isl29030_runtime_resume(struct device *dev)
{
    struct i2c_client *client = isl29030_i2c_client;
    dev_dbg(dev, "resume\n");
    isl_set_mod(client, ISL_FUNC_PROX, isl29030_last_pmod);
    isl_set_mod(client, ISL_FUNC_ALS, isl29030_last_lmod);
    msleep(100);
    return 0;
}

static int isl29030_suspend(struct device *dev)
{
    struct i2c_client *client = isl29030_i2c_client;
    dev_dbg(dev, "suspend\n");
    //printk(KERN_INFO MODULE_NAME ":suspend\n");
	mutex_lock(&mutex);
    isl_set_mod(client, ISL_FUNC_BOTH, ISL_MOD_POWERDOWN);
	mutex_unlock(&mutex);
    return 0;
}

static int isl29030_resume(struct device *dev)
{
    struct i2c_client *client = isl29030_i2c_client;
    dev_dbg(dev, "resume\n");
    //printk(KERN_INFO MODULE_NAME ":resume\n");
	mutex_lock(&mutex);
    isl_set_default_config(client);
    isl_set_mod(client, ISL_FUNC_PROX, isl29030_last_pmod);
    isl_set_mod(client, ISL_FUNC_ALS, isl29030_last_lmod);
	mutex_unlock(&mutex);
    msleep(100);
    return 0;
}

MODULE_DEVICE_TABLE(i2c, isl29030_id);

#if 0
static const struct dev_pm_ops isl29030_pm_ops = {
    .runtime_suspend = isl29030_runtime_suspend,
    .runtime_resume = isl29030_runtime_resume,
};
#endif

static const struct dev_pm_ops isl29030_pm_ops = {
    .suspend = isl29030_suspend,
    .resume = isl29030_resume,
};

static struct i2c_driver isl29030_driver = {
    .driver = {
        .name = "isl29030",
#if 1
        .pm = &isl29030_pm_ops,
#endif        
              },
    .probe = isl29030_probe,
    .remove = isl29030_remove,
    .id_table = isl29030_id,
    .detect         = isl29030_detect,
};

static int __init sensor_isl29030_init(void)
{
    printk(KERN_INFO MODULE_NAME ": %s isl29030 init call, \n", __func__);
    return i2c_add_driver(&isl29030_driver);
}

static void __exit sensor_isl29030_exit(void)
{
    printk(KERN_INFO MODULE_NAME ": %s isl29030 exit call \n", __func__);
    i2c_del_driver(&isl29030_driver);
}

module_init(sensor_isl29030_init);
module_exit(sensor_isl29030_exit);

MODULE_AUTHOR("joe.shidle");
MODULE_ALIAS("isl29030 ALS/PS");
MODULE_DESCRIPTION("Intersil isl29030 ALS/PS Driver");
MODULE_LICENSE("GPL v2");

