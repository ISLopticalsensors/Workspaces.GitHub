/******************************************************************************

	file		: isl29125.c

	Description	: Driver for ISL29125 RGB light sensor

	License		: GPLv2

	Copyright	: Intersil Corporation (c) 2013	
******************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/gpio.h>			//vvdn change
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/io.h>
#include <linux/interrupt.h>		//vvdn change
#include <linux/kobject.h>		//vvdn change
#include <linux/sysfs.h>		//vvdn change
#include <linux/irq.h>			//vvdn change
#include <linux/ioctl.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/math64.h>
#include <mach/eint.h>
#include <mach/mt_gpio.h>
#include <mach/irqs.h>
#include <linux/isl29125.h>		//vvdn change
#include <linux/miscdevice.h>
#include <linux/sensors_io.h>
#include "cust_eint.h"
#include <linux/earlysuspend.h>

#define ISL29125_I2C_ADDR	0x44	

#define NEW_CCM	    0
#define MEIZU_CCM	1 
#define POLL_DELAY 400

#if MEIZU_CCM
#define DEFAULT_IR_COMP		91
#define DEFAULT_IR_INDICATOR_GREEN	4000 // >0.4
#define DEFAULT_KR			13354
#define DEFAULT_KB			7022
#define DEFAULT_LUX_COEF	432
#define DEFAULT_CONVERSION_TIME	100 // ms
enum work_status { 
		WORK_NONE, W1_CONVERSION_GREEN_RED_BLUE, W1_CONVERSION_GREEN_IRCOMP
};
#endif

static atomic_t isl_als_start = ATOMIC_INIT(0);

static struct i2c_board_info i2c_devs_info[] = {
	{
	    I2C_BOARD_INFO("isl29125", ISL29125_I2C_ADDR),
	}
};


       int isl29125_i2c_read_word16(struct i2c_client *client, 
                                    unsigned char reg_addr, unsigned short *buf);
       int get_optical_range(int *range);
       int get_adc_resolution_bits(int *res);



static struct isl29125_data_t {
	bool sensor_enable;
	int poll_delay;		/* poll delay set by hal */

	struct mutex rwlock_mutex;
	struct work_struct als_work;
	struct i2c_client *client_data;
	struct input_dev *sensor_input;
	struct delayed_work    sensor_dwork; /* for ALS polling */
	struct early_suspend early_suspend;

#ifdef NEW_CCM
	u8 adc_resolution;
	u8 als_range_using;		/* the als range using now */
	u16 last_r;
	u16 last_g;
	u16 last_b;
	u16 cct;
	u16 X;
	u16 Y;
	u16 Z;
	u16 x;
	u16 y;
#endif

#ifdef MEIZU_CCM
	u16 conversion_time;
	enum work_status wstatus;
	// read data
	u16 cache_red;
	u16 cache_green;
	u16 cache_blue;
	u16 cache_green_ircomp;
	u16 raw_red0; // R0
	u16 raw_green0; // G0
	u16 raw_blue0; // B0
	u16 raw_green_ircomp; // GN ex)G91
	// cct calculating data
	u8 ir_comp;
	u32 IR_indicator_green; // G0/G91-1 ; e-4
	s32	kr;
	s32 kb;
	s32 co_r; // gen_nr ; e-4
	s32 co_g; // gen_ng ; e-4
	s32 co_b; // gen_nb ; e-4
	s16 x; // gen_nx  ; e-4
	s16 y; // gen_nx  ; e-4
	//u16 z;
	u16 CCT;
	u8 FCCT;
	// lux
	s32 lux_coef; // lux eq = lux_coef * raw_green_ircomp / 2^8
	
#endif
};//isl_data;				//New gloabal variable creation 

#ifdef MEIZU_CCM
#define DEFAULT_LUX_COEF 
static s32 xyzCCM[3][3] = {
	{	1889,	5889,	5982},// x col
	{	2608,	4911,	4809 },// y col
	{	0,	0,	0}, // z col
};
#endif

static struct isl29125_data_t *isl29125_info = NULL;
#ifdef NEW_CCM
// louis add the below March, 13, 2014

enum als_range { 
			RangeLo = 0, 
			RangeHi, 
			RangeMax 
		     };
enum resolution { 
			Bit16 = 0, 
			Bit12, 
			BitMax 
		     };
// 14bit fixed point calc
static s32 CCM_Gain[RangeMax][BitMax] = {
	{35447L, 631511L},
	{46172L,       22650L},
};

static s32 CCM_RangeLo[3][3] ={
	{	-2980L,	16389L,	-11820L},// X col
	{	-4388L,	16383L,	-10653L },// Y col
	{	-8998L,	13667L,	-3900L}, // Z col
};

static s32 CCM_RangeHi[3][3] ={
	{	-715L,	14265L,	-9230L},// X col
	{	-3267L,	16383L,	-9969L },// Y col
	{	-7420L,	7032L,	7344L}, // Z col
};

#define LUX_COEF_RED 25 // x10 2:8 IR inked glass
#define LUX_COEF_BLUE 8 // x10 2:8 IR inked glass
#define LUX_COEF_GREEN 50 // x10 2:8 IR inked glass
// louis end of add
#endif


#if MEIZU_CCM
static short int set_config2(u8 reg)
{
	short int ret;

	ret = i2c_smbus_write_byte_data(client, CONFIG2_REG, (u8)reg);
	if (ret < 0) {
		return -1;
	}
	return ret;
}

static s32 cal_cct(struct isl29125_data_t *dat)
{
	s32 G0_GIR_1;
	s32 genR, genG, genB, genSum;
	s32 gen_nr, gen_ng, gen_nb;
	s32 ret;
	
	G0_GIR_1 = 10000 * dat->raw_green/dat->raw_green_ircomp;
	if(G0_GIR_1>dat->IR_indicator_green)
	{
		return -1;
	}
	
	genR = dat->raw_red - (G0_GIR_1*dat->kr)/10000;
	genG = dat->raw_green_ircomp;
	genB = dat->raw_blue - (G0_GIR_1*dat->kb)/10000;
	genSum = genR + genB + genB;
	gen_nr = genR * 10000/ genSum;
	gen_ng = genG * 10000/ genSum;
	gen_nb = genB * 10000/ genSum;
	
	dat->x = (xyzCCM[0][2]*gen_nr + xyzCCM[0][1]*gen_ng + xyzCCM[0][0]*gen_nb)/10000;
	dat->y = (xyzCCM[1][2]*gen_nr + xyzCCM[1][1]*gen_ng + xyzCCM[1][0]*gen_nb)/10000;
	
	// The x,y ranges are (0.25, 0.45) ~ (0.545, 0.245),If the current xy values are out of the #2 ranges, return 0 for CCT value
	if(( x<2500 || x>5450 )||( y>4500 || y<2450)){
		return 0;
	}
	n = (dat->x-3320)*10000/(dat->y-1858);
	dat->CCT = ((-449*n/10000+3525)*n/10000-6823)*n/10000+5520;
	return 1;
}

static s32 cal_lux(struct isl29125_data_t *dat)
{
		return (dat->raw_green_ircomp*dat->lux_coef)>>8;
}

ssize_t show_raw_adc(struct device *dev, struct device_attribute *attr, char *buf)
{
	u32 lux;
	//struct i2c_client *client = to_i2c_client(dev);
	struct isl29125_data_t *dat=dev_get_drvdata(dev);

	mutex_lock(&dat->rwlock_mutex);
	sprintf(buf, "R0=%d, G0=%d, B0=%d, GIR=%d\n", dat->raw_red, dat->raw_green, dat->raw_blue, dat->green_ircomp );	
	mutex_unlock(&dat->rwlock_mutex);
	return strlen(buf);
}

ssize_t show_lux(struct device *dev, struct device_attribute *attr, char *buf)
{
	u32 lux;
	//struct i2c_client *client = to_i2c_client(dev);
	struct isl29125_data_t *dat=dev_get_drvdata(dev);

	mutex_lock(&dat->rwlock_mutex);
	lux = cal_lux(dat);
	sprintf(buf, "%d\n", lux);	
	mutex_unlock(&dat->rwlock_mutex);
	return strlen(buf);
}

static ssize_t show_cct(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
	s32 cct;

	struct isl29125_data_t *isl29125=dev_get_drvdata(dev);
	//struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl29125->rwlock_mutex);
    cct = cal_cct(isl29125);
    mutex_unlock(&isl29125->rwlock_mutex);
	if(cct<0)
	{
		return sprintf(buf, "cct_err : high IR light source\n");
	}
	else if(cct==0)
	{
		return sprintf(buf, "cct_err : out of range\n");
	}
	else
	{
		return sprintf(buf,"%d\n", cct);
	}
}
#endif


#ifdef NEW_CCM
static u32 cal_cct(struct isl29125_data_t *dat)
{
        //s32 tmp;
        s32 cct;
        s64 X0, Y0, Z0, sum0;
        s64 x,y,n, xe, ye;
        u8 Range;
        u8 bits;
        u16 als_r, als_g, als_b;
	s64 tmp;

        als_r = dat->last_r;
        als_g = dat->last_g;
        als_b = dat->last_b;

	bits = 0;
        Range=dat->als_range_using;
        if(Range == 0)
        {
                X0 = ( CCM_RangeLo[0][0]*als_r + CCM_RangeLo[0][1]*als_g + CCM_RangeLo[0][2] * als_b );
                Y0 = ( CCM_RangeLo[1][0]*als_r + CCM_RangeLo[1][1]*als_g + CCM_RangeLo[1][2] * als_b );
                Z0 = ( CCM_RangeLo[2][0]*als_r + CCM_RangeLo[2][1]*als_g + CCM_RangeLo[2][2] * als_b );
        }
        else
        {
                X0 = ( CCM_RangeHi[0][0]*als_r + CCM_RangeHi[0][1]*als_g + CCM_RangeHi[0][2] * als_b );
                Y0 = ( CCM_RangeHi[1][0]*als_r + CCM_RangeHi[1][1]*als_g + CCM_RangeHi[1][2] * als_b );
                Z0 = ( CCM_RangeHi[2][0]*als_r + CCM_RangeHi[2][1]*als_g + CCM_RangeHi[2][2] * als_b );
        }


//      X=X0/CCM_Gain[Range][bits];
//      Y=Y0/CCM_Gain[Range][bits];
//      Z=Z0/CCM_Gain[Range][bits];
//      sum = X + Y + Z;
//      x = X*1000/sum; y = Y*1000/sum;

        sum0 = X0 + Y0 + Z0;
        if (sum0 == 0)
        {
                printk("sum0 value is 0");
                return -1;
        }
        //x = X0*1000*CCM_Gain[Range][bits]/sum0;
        x = div64_s64(X0*10000, sum0);
        //y = Y0*1000*CCM_Gain[Range][bits]/sum0;
        y = div64_s64(Y0*10000, sum0);
        xe=3320; // 0.3320
        ye=1858; // 0.1858
	dat->x = x;
	dat->y = y;
	
	// The x,y ranges are (0.25, 0.45) ~ (0.545, 0.245),If the current xy values are out of the #2 ranges, return 0 for CCT value
	if(( x<2500 || x>5450 )||( y>4500 || y<2450)){
		cct = 0; // cct 0 means "cct is not valid"
		dat->cct = 0;
		printk("CCT is not valid");
		return cct;
	}
      //  if (y == 1858)
      //  {
      //          printk("y-ye value is 0");
      //          return -1;
      //   }
        // n = (x-xe)/(y-ye)
        //n = ( x - xe )*1000/( y - ye );
        n = div64_s64(( x - xe )*10000,( y - ye ));
        //cct = n * (n * ((-449 * n) / 1000 + 3525) / 1000 - 6823) / 1000 + 5520;
	tmp = div64_s64(-449*n, 10000);
	tmp = div64_s64((tmp+3525)*n, 10000);
	tmp = div64_s64((tmp-6823)*n, 10000);
	cct = tmp + 5520;
        //n = (X<<31 - 712964572L *sum ) / ( Y<<17 - 24354L * sum);
        //cct = n * (n * ((-449*n)/16384 + 3525)/16384 - 6823)/16384 + 5520;
        dat->X = div64_s64( X0, CCM_Gain[Range][bits]);
        dat->Y = div64_s64( Y0, CCM_Gain[Range][bits]);
        dat->Z = div64_s64( Z0, CCM_Gain[Range][bits]);

        if(cct < 0) cct = 0;

        dat->cct = cct;
        return cct;
}
#endif // NEW_CCM


#ifdef NEW_CCM
static u32 cal_lux(struct isl29125_data_t *dat, int *cct, u8 dbg)
{
	u32 lux;
	u8 bits;
	u16 r, g, b;

	r = dat->last_r;
	g = dat->last_g;
	b = dat->last_b;
	
	//bits = (dat->adc_resolution==0)? 1:0;a
	bits =0;
	if(dat->als_range_using == 0)
	{ // 375 lux
		lux = ( CCM_RangeLo[1][0]*r + CCM_RangeLo[1][1]*g + CCM_RangeLo[1][2] * b )/CCM_Gain[RangeLo][bits];
	}
	else
	{ // 10000 lux
		lux = ( CCM_RangeHi[1][0]*r + CCM_RangeHi[1][1]*g + CCM_RangeHi[1][2] * b )/CCM_Gain[RangeHi][bits];
	}

	*cct = cal_cct(dat);
	
	if(dbg)
	{
		printk(KERN_ERR "r=%d, g=%d, b=%d, "
			" lux=%d, cct=%d\n", r, g, b,
			lux, *cct);
	}
	
	return lux;
	
}
#endif 

int read_raw_RGB(u16 *regr, u16 *regg, u16 *regb)
{
	int ret;
	ret = isl29125_i2c_read_word16(client, RED_DATA_LBYTE_REG, regr);
	if (ret < 0) {
			return -1;
	}
	ret = isl29125_i2c_read_word16(client, GREEN_DATA_LBYTE_REG, regg);
	if (ret < 0) {
			return -1;
	}
	ret = isl29125_i2c_read_word16(client, BLUE_DATA_LBYTE_REG, regb);
	if (ret < 0) {
			return -1;
	}
	return ret;
}
#if NEW_CCM
ssize_t show_lux(struct device *dev, struct device_attribute *attr, char *buf)
{

        int ret;
        int range;
        int res;
        int cct;
	u8 dbg;
	u32 lux;
        unsigned short regr;
        unsigned short regg;
        unsigned short regb;
  //    unsigned short regg2;
        struct i2c_client *client = to_i2c_client(dev);
        struct isl29125_data_t *dat=dev_get_drvdata(dev);
	
        mutex_lock(&dat->rwlock_mutex);
		ret=read_raw_RGB(&regr, &regg, &regb);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&dat->rwlock_mutex);
                return -1;
        }

        ret = get_optical_range(&range);
        ret = get_adc_resolution_bits(&res);
        dbg = 1;   
        dat->last_r = regr;
        dat->last_g = regg;
        dat->last_b = regb;
        dat->adc_resolution = ((res == 16) ? 0:1);
        dat->als_range_using = ((range == 375)? 0:1);
	if (regg >= 0xffff) {
		lux = LUX_COEF_BLUE * regb; // or lux = LUX_COEF_RED * regr;
	} else if( regr >= 0xffff || regb >= 0xffff ){
		lux = LUX_COEF_GREEN * regg;
	} else {
	    lux = cal_lux(dat, &cct, dbg);
	}

     //   if(lux > 65535) lux = 65535;
        sprintf(buf, "R=%d, G=%d, B=%d   =====>   X=%d, Y=%d, Z=%d, CCT=%d, LUX=%d\n", regr, regg, regb, dat->X, dat->Y, dat->Z, cct, lux);	
        //sprintf(buf, "R=%d, G=%d, B=%d, CCT=%d, LUX=%d\n", regr, regg, regb, cct, lux);	
        mutex_unlock(&dat->rwlock_mutex);
        return strlen(buf);

}


static ssize_t show_cct(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
        u16 cct;
        int ret;
        int res;
	u8 dbg;
        int range;
	unsigned short regr;
        unsigned short regg;
        unsigned short regb;
        struct isl29125_data_t *isl29125=dev_get_drvdata(dev);
        struct i2c_client *client = to_i2c_client(dev);

	mutex_lock(&isl29125->rwlock_mutex);
        ret = isl29125_i2c_read_word16(client, RED_DATA_LBYTE_REG, &regr);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&isl29125->rwlock_mutex);
                return -1;
        }

        ret = isl29125_i2c_read_word16(client, GREEN_DATA_LBYTE_REG, &regg);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&isl29125->rwlock_mutex);
                return -1;
        }
        ret = isl29125_i2c_read_word16(client, BLUE_DATA_LBYTE_REG, &regb);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&isl29125->rwlock_mutex);
                return -1;
        }
	ret = get_optical_range(&range);
        ret = get_adc_resolution_bits(&res);
        dbg = 1;   
        isl29125->last_r = regr;
        isl29125->last_g = regg;
        isl29125->last_b = regb;
        isl29125->adc_resolution = ((res == 16)? 0:1);
        isl29125->als_range_using = ((range == 375)? 0:1);
        cal_cct(isl29125);
        cct = isl29125->cct;

	mutex_unlock(&isl29125->rwlock_mutex);

        return sprintf(buf,"%d\n", cct);
        //return snprintf(buf, PAGE_SIZE, "%d\n", cct);
}
#endif
/*
 * @fn          set_optical_range 
 *
 * @brief       This function sets the optical sensing range of sensor device 
 *
 * @return      Returns 0 on success otherwise returns an error (-1)
 *
 */
int set_optical_range(int *range)
{
        int ret;

        ret = i2c_smbus_read_byte_data(isl29125_info->client_data, CONFIG1_REG);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to get data\n", __FUNCTION__);
                return -1;
        }

        if(*range == 10000)
                ret |= RGB_SENSE_RANGE_10000_SET;
        else if (*range == 375)
                ret &= RGB_SENSE_RANGE_375_SET;
        else
                return -1;

        ret = i2c_smbus_write_byte_data(isl29125_info->client_data, CONFIG1_REG, ret);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
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

int get_optical_range(int *range)
{
        int ret;

        ret = i2c_smbus_read_byte_data(isl29125_info->client_data, CONFIG1_REG);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
                return -1;
        }
	
        *range = (ret & RGB_SENSE_RANGE_10000_SET)?10000:375;

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
#define ADC_BIT_RESOLUTION_POS 4
int get_adc_resolution_bits(int *res)
{
	int ret;

	ret = i2c_smbus_read_byte_data(isl29125_info->client_data, CONFIG1_REG); 
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		return -1;	
	}

	*res = (ret & (1 << ADC_BIT_RESOLUTION_POS ))?12:16; 

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
int set_adc_resolution_bits(u8 *res)
{
	int ret;
	int reg;

	reg = i2c_smbus_read_byte_data(isl29125_info->client_data, CONFIG1_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		return -1;
	}	

	if(*res)
		reg |= ADC_RESOLUTION_12BIT_SET;
	else 
		reg &= ADC_RESOLUTION_16BIT_SET;
	ret = i2c_smbus_write_byte_data(isl29125_info->client_data, CONFIG1_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
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
int set_mode(int mode)
{
	int ret;
	short int reg;

	reg = i2c_smbus_read_byte_data(isl29125_info->client_data, CONFIG1_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		return -1;
	}

	reg &= RGB_OP_MODE_CLEAR;
	reg |= mode;

	ret = i2c_smbus_write_byte_data(isl29125_info->client_data, CONFIG1_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__); 
		return -1;
	}

	return 0;
}

static ssize_t show_xy_value(struct device *dev,
                          struct device_attribute *attr, char *buf)
{
	struct isl29125_data_t *dat=dev_get_drvdata(dev);
	//cal_cct(isl29124);
	return sprintf(buf,"x= %d y= %d\n",dat->x,dat->y);
}


/*
 * @fn          autorange 
 *
 * @brief       This function processes autoranging of sensor device 
 *
 * @return      void 
 *
 */
void autorange(int green)
{
	int ret;
	unsigned int adc_resolution, optical_range;		

	ret = get_adc_resolution_bits(&adc_resolution);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to get adc resolution\n", __FUNCTION__);
		return;
	}

	ret = get_optical_range(&optical_range);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to get optical range\n", __FUNCTION__);
		return;
	}

	switch (adc_resolution) {
		case 12:
			switch(optical_range) {
				case 375:
					/* Switch to 10000 lux */
					if(green > 0xCCC) {
						optical_range = 10000;
						set_optical_range(&optical_range);
					}
					break;
				case 10000:
					/* Switch to 375 lux */
					if(green < 0xCC) {
						optical_range = 375;
						set_optical_range(&optical_range);
					}
					break;
			}
			break;
		case 16:
			switch(optical_range) {
				case 375:
					/* Switch to 10000 lux */
					if(green > 0xCCCC) {
						optical_range = 10000;
						set_optical_range(&optical_range);
					}

					break;
				case 10000:
					/* Switch to 375 lux */
					if(green < 0xCCC) {
						optical_range = 375;
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
int isl29125_i2c_read_word16(struct i2c_client *client, unsigned char reg_addr, unsigned short *buf)
{
	u8 dat[4];
	int ret;
	
	ret = i2c_smbus_read_i2c_block_data(client, reg_addr, 2, dat);
	if(ret != 2)
	{
		printk(KERN_ERR "%s: Failed to read block data\n", __FUNCTION__);
		return -1;
	}
	*buf = ((u16)dat[1] << 8) | (u16)dat[0];

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
int isl29125_i2c_write_word16(struct i2c_client *client, unsigned char reg_addr, unsigned short *buf)
{
	int ret;
	unsigned char reg_h;
	unsigned char reg_l;

	/* Extract LSB and MSB bytes from data */
	reg_l = *buf & 0xFF;
	reg_h = (*buf & 0xFF00) >> 8;

	ret = i2c_smbus_write_byte_data(client, reg_addr, reg_l); 
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		return -1;	
	}

	ret = i2c_smbus_write_byte_data(client, reg_addr + 1, reg_h); 
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		return -1;	
	}

	return 0;
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

	mutex_lock(&isl29125_info->rwlock_mutex);

	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}
	printk("reg is %d\n",reg);
	switch (reg & 0x7) {
		case 0:
			sprintf(buf,"%s\n","pwdn"); 
			break;
		case 1:
			sprintf(buf,"%s\n","green"); 
			break;
		case 2:
			sprintf(buf, "%s\n","red"); 
			break;
		case 3:
			sprintf(buf, "%s\n","blue"); 
			break;
		case 4:
			sprintf(buf, "%s\n","standby"); 
			break;
		case 5:
			sprintf(buf, "%s\n","green.red.blue"); 
			break;
		case 6:
			sprintf(buf, "%s\n","green.red"); 
			break;
		case 7:
			sprintf(buf, "%s\n","green.blue"); 
			break;
	}

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        int ret;
        int mode;
        int val;

	mutex_lock(&isl29125_info->rwlock_mutex);

        val = simple_strtoul(buf, NULL, 10);
        if(val == 0) {
                mode = RGB_OP_PWDN_MODE_SET;
        } else if(val == 1) {
                mode = RGB_OP_GREEN_MODE_SET;
        } else if(val == 2) {
                mode = RGB_OP_RED_MODE_SET;
        } else if(val == 3) {
                mode = RGB_OP_BLUE_MODE_SET;
        } else if(val == 4) {
                mode = RGB_OP_STANDBY_MODE_SET;
        } else if(val == 5) {
                mode = RGB_OP_GRB_MODE_SET;
        } else if(val == 6) {
                mode = RGB_OP_GR_MODE_SET;
        } else if(val == 7) {
                mode = RGB_OP_GB_MODE_SET;
        } else {
                printk(KERN_ERR "%s: Invalid value\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
                return -1;
        }

        ret = set_mode(mode);
        if (ret < 0) {
                printk(KERN_ERR "%s: Failed to set operating mode\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
                return -1;
        }

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
	int ret;
	int reg;

	mutex_lock(&isl29125_info->rwlock_mutex);

	ret = get_optical_range(&reg);
	if(ret < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	sprintf(buf, "%d", reg); 

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
	int ret;
	int reg;

	mutex_lock(&isl29125_info->rwlock_mutex);
	ret = get_adc_resolution_bits(&reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__); 
		return -1;
	}

	sprintf(buf, "%d", reg); 
	mutex_unlock(&isl29125_info->rwlock_mutex);

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
static ssize_t store_adc_resolution_bits(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	u8 reg;
	int val;

	mutex_lock(&isl29125_info->rwlock_mutex);
	val = simple_strtoul(buf, NULL, 10);
	if(val == 0)
		reg = 0;
	else if(val == 1)
		reg = 1;
	else {
		printk(KERN_ERR "%s: Invalid input\n",__FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return count;
	}
	ret = set_adc_resolution_bits(&reg);	
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to set adc resolution\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return count;

	}
	mutex_unlock(&isl29125_info->rwlock_mutex);

	return strlen(buf);	
}

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

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = isl29125_i2c_read_word16(client, HIGH_THRESHOLD_LBYTE_REG, &reg); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	sprintf(buf, "%d", reg);

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_intr_threshold_high(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned short reg;
	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg == 0 || reg > 65535) {
		printk(KERN_ERR "%s: Invalid input value\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}
	ret = isl29125_i2c_write_word16(client, HIGH_THRESHOLD_LBYTE_REG, &reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write word\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}	

	mutex_unlock(&isl29125_info->rwlock_mutex);
	return strlen(buf);	

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

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = isl29125_i2c_read_word16(client, LOW_THRESHOLD_LBYTE_REG, &reg); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read word\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	sprintf(buf, "%d", reg); 

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_intr_threshold_low(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned short reg;

	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = simple_strtoul(buf, NULL, 10);
	if (reg == 0 || reg > 65536) {
		mutex_unlock(&isl29125_info->rwlock_mutex);
		printk(KERN_ERR "%s: Invalid input value\n", __FUNCTION__);
		return -1;
	}

	ret = isl29125_i2c_write_word16(client, LOW_THRESHOLD_LBYTE_REG, &reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write word\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}	
	mutex_unlock(&isl29125_info->rwlock_mutex);

	return strlen(buf);	
}

/*
 * @fn          show_intr_threshold_assign
 *
 * @brief       This function displays the color component for which the interrupts are generated
 *
 * @return      Returns the length of data buffer on success otherwise returns an error (-1)
 *
 */
static ssize_t show_intr_threshold_assign(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;

	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	/* Extract interrupt threshold assign value */
	reg = (reg & ((0x3) << INTR_THRESHOLD_ASSIGN_POS)) >> INTR_THRESHOLD_ASSIGN_POS;	

	switch(reg) {
		case 0:
			sprintf(buf, "%s", "none"); 
			break;
		case 1:
			sprintf(buf, "%s", "green"); 
			break;
		case 2:
			sprintf(buf, "%s", "red"); 
			break;
		case 3:
			sprintf(buf, "%s", "blue"); 
			break;
	}

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_intr_threshold_assign(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	short int reg;
	short int threshold_assign;

	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	if(!strcmp(buf, "none")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_CLEAR;				  	
	} else if(!strcmp(buf, "green")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_GREEN;
	} else if(!strcmp(buf, "red")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_RED;
	} else if(!strcmp(buf, "blue")) {
		threshold_assign = INTR_THRESHOLD_ASSIGN_BLUE;
	} else {
		printk(KERN_ERR "%s: Invalid value\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	reg &= INTR_THRESHOLD_ASSIGN_CLEAR;
	reg |= threshold_assign;	

	ret = i2c_smbus_write_byte_data(client, CONFIG3_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	mutex_unlock(&isl29125_info->rwlock_mutex);
	return strlen(buf);
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

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	reg = (reg & (0x3 << INTR_PERSIST_CTRL_POS)) >> INTR_PERSIST_CTRL_POS;

	switch(reg) {
		case 0:
			intr_persist = 1;
			break;
		case 1:
			intr_persist = 2;
			break;
		case 2:
			intr_persist = 4;
			break;
		case 3:
			intr_persist = 8;
			break; 	

	}

	sprintf(buf, "%d", intr_persist); 
	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_intr_persistency(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	short int reg;
	short int intr_persist;
	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);

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
		printk(KERN_ERR "%s: Invalid value\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	reg &= INTR_PERSIST_CTRL_CLEAR;
	reg |= intr_persist << INTR_PERSIST_CTRL_POS; 	

	ret = i2c_smbus_write_byte_data(client, CONFIG3_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	mutex_unlock(&isl29125_info->rwlock_mutex);
	return strlen(buf);
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

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	reg = (reg & (1 << RGB_CONV_TO_INTB_CTRL_POS)) >> RGB_CONV_TO_INTB_CTRL_POS;

	sprintf(buf, "%s", reg?"disable":"enable"); 

	mutex_unlock(&isl29125_info->rwlock_mutex);
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
static ssize_t store_rgb_conv_intr(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, rgb_conv_intr;
	short int reg;
	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	ret = simple_strtoul(buf, NULL, 10);
	if(ret == 1)
		rgb_conv_intr = 0;
	else if(ret == 0)
		rgb_conv_intr = 1;
	else {
		printk(KERN_ERR "%s: Invalid input for rgb conversion interrupt [0-1]\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	reg = i2c_smbus_read_byte_data(client, CONFIG3_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}	

	reg &= RGB_CONV_TO_INTB_CLEAR;
	reg |= rgb_conv_intr;

	ret = i2c_smbus_write_byte_data(client, CONFIG3_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}

	mutex_unlock(&isl29125_info->rwlock_mutex);
	return strlen(buf);	
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

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG); 
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;	
	}

	reg = (reg & (1 << RGB_START_SYNC_AT_INTB_POS)) >> RGB_START_SYNC_AT_INTB_POS;

	sprintf(buf, "%s", reg?"risingIntb":"i2cwrite"); 
	mutex_unlock(&isl29125_info->rwlock_mutex);

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
static ssize_t store_adc_start_sync(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	short int reg, adc_start_sync;

	struct i2c_client *client = to_i2c_client(dev);                             
	ret = simple_strtoul(buf, NULL, 10);
	if(ret == 0)
		adc_start_sync = 0;
	else if(ret == 1)
		adc_start_sync = 1;
	else {
		printk(KERN_ERR "%s: Invalid value for adc start sync\n", __FUNCTION__);
		return -1;
	}

	mutex_lock(&isl29125_info->rwlock_mutex);
	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if (reg < 0) {
		printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}	

	if(adc_start_sync)
		reg |= ADC_START_AT_RISING_INTB;
	else 
		reg &= ADC_START_AT_I2C_WRITE;


	ret = i2c_smbus_write_byte_data(client, CONFIG1_REG, reg);
	if (ret < 0) {
		printk(KERN_ERR "%s: Failed to write data\n", __FUNCTION__);
		mutex_unlock(&isl29125_info->rwlock_mutex);
		return -1;
	}
	mutex_unlock(&isl29125_info->rwlock_mutex);

	return strlen(buf);	
}


static ssize_t isl29125_show_enable_sensor(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n",isl29125_info->sensor_enable);
}

static ssize_t isl29125_store_enable_sensor(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = simple_strtoul(buf, NULL, 10);
	bool sensor_enable;
	
	if ((val != 0) && (val != 1))
	{
		return count;
	}
	
	if(val == 1) {
	    sensor_enable = 1;
//	    mt_eint_unmask(GPIO_ALS_EINT_PIN);	//vvdn change
	    mutex_lock(&isl29125_info->rwlock_mutex);
	    atomic_set(&isl_als_start, 1);
	    __cancel_delayed_work(&isl29125_info->sensor_dwork);
		isl29125_info->wstatus = W1_CONVERSION_GREEN_RED_BLUE;
	    schedule_delayed_work(&isl29125_info->sensor_dwork, msecs_to_jiffies(POLL_DELAY));	// 125ms
	    mutex_unlock(&isl29125_info->rwlock_mutex);
	} else {
	    sensor_enable = 0;
//	    mt_eint_mask(GPIO_ALS_EINT_PIN);	//vvdn change
	    mutex_lock(&isl29125_info->rwlock_mutex);
	    __cancel_delayed_work(&isl29125_info->sensor_dwork);
		isl29125_info->wstatus = W;
	    mutex_unlock(&isl29125_info->rwlock_mutex);
	}

	isl29125_info->sensor_enable = sensor_enable;

	return count;
}

static ssize_t isl29125_show_delay(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	
	return sprintf(buf, "%d\n",POLL_DELAY);
}

static ssize_t show_reg_dump(struct device *dev, struct device_attribute *attr, char *buf)
{
	short int reg;
	int i;
	struct i2c_client *client = to_i2c_client(dev);                             

	mutex_lock(&isl29125_info->rwlock_mutex);
	*buf = 0;
	for(i=0; i<15 ; i++)
	{
		reg = i2c_smbus_read_byte_data(client, (u8)i); 
		if (reg < 0) {
			printk(KERN_ERR "%s: Failed to read data\n", __FUNCTION__);
			mutex_unlock(&isl29125_info->rwlock_mutex);
			return -1;	
		}

		sprintf(buf, "%sreg%02x(%02x)\n", buf, i, reg); 
	}
	mutex_unlock(&isl29125_info->rwlock_mutex);
	return strlen(buf);
}

static ssize_t store_reg_dump(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	//unsigned long val = simple_strtoul(buf, NULL, 10);
	unsigned int reg, dat;
	int ret;	
	struct i2c_client *client = to_i2c_client(dev);                             

	sscanf(buf,"%02x %02x", &reg, &dat);

	mutex_lock(&isl29125_info->rwlock_mutex);
 
	ret = i2c_smbus_write_byte_data(client, (u8)reg, (u8)dat);

	mutex_unlock(&isl29125_info->rwlock_mutex);

	return count;
}

// debugging and testing
static DEVICE_ATTR(raw_adc, ISL29125_SYSFS_PERMISSIONS, show_raw_adc, NULL); 
static DEVICE_ATTR(reg_dump, ISL29125_SYSFS_PERMISSIONS, show_reg_dump, store_reg_dump); 
/* Attributes of ISL29125 RGB light sensor */

// main sysfs

static DEVICE_ATTR(cct, ISL29125_SYSFS_PERMISSIONS , show_cct, NULL);
static DEVICE_ATTR(lux, ISL29125_SYSFS_PERMISSIONS , show_lux, NULL);
static DEVICE_ATTR(xy_value, ISL29125_SYSFS_PERMISSIONS , show_xy_value, NULL);
 

// optional

static DEVICE_ATTR(mode, ISL29125_SYSFS_PERMISSIONS , show_mode, store_mode);
static DEVICE_ATTR(optical_range, ISL29125_SYSFS_PERMISSIONS , show_optical_range, NULL);
static DEVICE_ATTR(adc_resolution_bits, ISL29125_SYSFS_PERMISSIONS , show_adc_resolution_bits, store_adc_resolution_bits);

static DEVICE_ATTR(intr_threshold_high , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_high, store_intr_threshold_high);
static DEVICE_ATTR(intr_threshold_low , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_low, store_intr_threshold_low);

static DEVICE_ATTR(intr_threshold_assign , ISL29125_SYSFS_PERMISSIONS , show_intr_threshold_assign, store_intr_threshold_assign);
static DEVICE_ATTR(intr_persistency, ISL29125_SYSFS_PERMISSIONS , show_intr_persistency, store_intr_persistency);
static DEVICE_ATTR(rgb_conv_intr, ISL29125_SYSFS_PERMISSIONS , show_rgb_conv_intr, store_rgb_conv_intr);

static DEVICE_ATTR(adc_start_sync, ISL29125_SYSFS_PERMISSIONS , show_adc_start_sync, store_adc_start_sync);

// mandatory for android
static DEVICE_ATTR(sensor_enable, ISL29125_SYSFS_PERMISSIONS ,isl29125_show_enable_sensor, isl29125_store_enable_sensor);
static DEVICE_ATTR(poll_delay, ISL29125_SYSFS_PERMISSIONS ,isl29125_show_delay, NULL);

static struct attribute *isl29125_attributes[] = {
	/* read RGB value attributes */

    &dev_attr_cct.attr,
    &dev_attr_lux.attr,

	&dev_attr_xy_value.attr,
	/* Device operating mode */ 
	&dev_attr_mode.attr,

	/* Current optical sensing range */
	&dev_attr_optical_range.attr,

	/* Current adc resolution */
	&dev_attr_adc_resolution_bits.attr,
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
	&dev_attr_sensor_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_reg_dump.attr,
	&dev_attr_raw_adc.attr,
	NULL
};

static struct attribute_group isl29125_attr_group = {
	.attrs = isl29125_attributes
};

/*
 * @fn          initialize_isl29125
 *
 * @brief       This function initializes the sensor device with default values
 *
 * @return      void
 *
 */



void initialize_isl29125(struct i2c_client *client)
{
	unsigned char reg;
	
	isl29125_info->ir_comp = DEFAULT_IR_COMP;
	isl29125_info->IR_indicator_green = DEFAULT_IR_INDICATOR_GREEN; 
	isl29125_info->kr = DEFAULT_KR;
	isl29125_info->kb = DEFAULT_KB;
	isl29125_info->lux_coef = DEFAULT_LUX_COEF; 
	isl29125_info->conversion_time = DEFAULT_CONVERSION_TIME;
	isl29125_info->wstatus = WORK_NONE;
	
	/* Set device mode to RGB ,RGB Data sensing range 10000 Lux,
	   ADC resolution 16-bit, ADC start at intb start(is SYNC set 0, 
	   ADC start by i2c write 1)*/
	i2c_smbus_write_byte_data(client, CONFIG1_REG, 0x0D); 

	/* Default IR Active compenstation,
	   Disable IR compensation control */
	i2c_smbus_write_byte_data(client, CONFIG2_REG, isl29125_info->ir_comp);

	/* Interrupt threshold assignment for Green,G:01/R:10/B:11;
	   Interrupt persistency as 8 conversion data out of windows */
	i2c_smbus_write_byte_data(client, CONFIG3_REG, 0x1D); 

	/* Writing interrupt low threshold as 0xCCC (5% of max range) */
	i2c_smbus_write_byte_data(client, LOW_THRESHOLD_LBYTE_REG, 0xCC);	
	i2c_smbus_write_byte_data(client, LOW_THRESHOLD_HBYTE_REG, 0x0C);	

	/* Writing interrupt high threshold as 0xF333 (80% of max range)  */
	i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_LBYTE_REG, 0xCC);	
	i2c_smbus_write_byte_data(client, HIGH_THRESHOLD_HBYTE_REG, 0xCC);	

	/* Clear the brownout status flag */
	reg = i2c_smbus_read_byte_data(client, STATUS_FLAGS_REG);
	reg &= ~(1 << BOUTF_FLAG_POS);
	i2c_smbus_write_byte_data(client, STATUS_FLAGS_REG, reg);		
}

//W1_GRBG_INIT, W1_GREEN, W1_RED, W1_BLUE, W1_GREEN_IRCOMP, W1_GOTO_GRBG_INIT, 
#ifdef MEIZU_CCM
static void isl29125_work_handler(struct work_struct *work)
{
	struct isl29125_data_t *isl29125 =
	   	 container_of(work, struct isl29125_data_t, sensor_dwork.work);
	struct input_dev *sensor_input = isl29125->sensor_input;
	unsigned short regg, regb, regr;
	int ret;
	u8 dbg;
	int cct, res, range;
	unsigned long lux;
	
	switch(isl29125->wstatus)
	{
	case W1_CONVERSION_GREEN_RED_BLUE:
		ret = isl29125_i2c_read_word16(isl29125->client_data, GREEN_DATA_LBYTE_REG, &(isl29125->cache_green));
		ret = isl29125_i2c_read_word16(isl29125->client_data, RED_DATA_LBYTE_REG, &(isl29125->cache_red));
		ret = isl29125_i2c_read_word16(isl29125->client_data, BLUE_DATA_LBYTE_REG, &(isl29125->cache_blue));
		set_config2(isl29125->ir_comp); // set IR Comp #
		set_mode(5); // set GRB mode for adc re-start
		ISL29125->wstatus = W1_CONVERSION_GREEN_IRCOMP;
		schedule_delayed_work(&isl29125->sensor_dwork, msecs_to_jiffies(isl29125->conversion_time));	// restart timer
		break;
	case W1_CONVERSION_GREEN_IRCOMP:
		ret = isl29125_i2c_read_word16(isl29125->client_data, GREEN_DATA_LBYTE_REG, &&(isl29125->cache_green_ir));
		set_config2(0); // set IR Comp 0
		set_mode(5); // set GRB mode for adc re-start
		ISL29125->wstatus = W1_CONVERSION_GREEN_RED_BLUE;
		
		// load the RGB data to variables to calculate
		isl29125->raw_red0 = isl29125->cache_red;
		isl29125->raw_green0 = isl29125->cache_green;
		isl29125->raw_blue0 = isl29125->cache_blue;
		ISL29125->raw_green_ircomp = isl29125->cache_green_ir;
		
		lux = cal_lux(isl29125, &cct, dbg);

		if (atomic_read(&isl_als_start)) {
			lux += 1;
			atomic_set(&isl_als_start, 0);
		}

		input_report_abs(sensor_input, ABS_MISC, lux);
		input_sync(sensor_input);
		
		schedule_delayed_work(&isl29125->sensor_dwork, msecs_to_jiffies(isl29125->conversion_time*3));	// restart timer
		break;
	}
}
#endif

#if NEW_CCM
static void isl29125_work_handler(struct work_struct *work)
{
	struct isl29125_data_t *isl29125 =
	   	 container_of(work, struct isl29125_data_t, sensor_dwork.work);
	struct input_dev *sensor_input = isl29125->sensor_input;
	unsigned short regg, regb, regr;
	int ret;
	u8 dbg;
	int cct, res, range;
	unsigned long lux;

	ret = isl29125_i2c_read_word16(isl29125->client_data, RED_DATA_LBYTE_REG, &regr);

	ret = isl29125_i2c_read_word16(isl29125->client_data, GREEN_DATA_LBYTE_REG, &regg);

	ret = isl29125_i2c_read_word16(isl29125->client_data, BLUE_DATA_LBYTE_REG, &regb);

	autorange(regg);
	ret = get_optical_range(&range);
        ret = get_adc_resolution_bits(&res);
        dbg = 1;   
        isl29125->last_r = regr;
        isl29125->last_g = regg;
        isl29125->last_b = regb;
        isl29125->adc_resolution = ((res == 16)? 0:1);
        isl29125->als_range_using = ((range == 375)? 0:1);
        lux = cal_lux(isl29125, &cct, dbg);

        if(lux > 65535) lux = 65535;
	if (atomic_read(&isl_als_start)) {
		lux += 1;
		atomic_set(&isl_als_start, 0);
    	}

	input_report_abs(sensor_input, ABS_MISC, lux);
	input_sync(sensor_input);
#if 0
	input_report_abs(sensor_input, ABS_R, regr);
	input_report_abs(sensor_input, ABS_G, regg);
	input_report_abs(sensor_input, ABS_B, regb);
#endif
	schedule_delayed_work(&isl29125->sensor_dwork, msecs_to_jiffies(POLL_DELAY));	// restart timer
}
#endif


static void isl29125_input_create(struct isl29125_data_t *isl29125)
{
    	int ret;

	isl29125->sensor_input = input_allocate_device();
	if (!isl29125->sensor_input) {
		printk("%s: Failed to allocate input device als\n", __func__);
	}
	set_bit(EV_ABS, isl29125->sensor_input->evbit);
	input_set_capability(isl29125->sensor_input, EV_ABS, ABS_MISC);
	input_set_abs_params(isl29125->sensor_input, ABS_MISC, 0, 0xFFFF, 0, 0);
#if 0
	input_set_abs_params(isl29125->sensor_input, ABS_R, 0, 0xFFFF, 0, 0);
	input_set_abs_params(isl29125->sensor_input, ABS_G, 0, 0xFFFF, 0, 0);
	input_set_abs_params(isl29125->sensor_input, ABS_B, 0, 0xFFFF, 0, 0);
#endif
	isl29125->sensor_input->name = "isl29125";
	isl29125->sensor_input->dev.parent = &isl29125->client_data->dev;

	ret = input_register_device(isl29125->sensor_input);
	if (ret) {
		printk("%s: Unable to register input device als: %s\n",
		       __func__,isl29125->sensor_input->name);
	}
	input_set_drvdata(isl29125->sensor_input, isl29125);
}

static int isl29125_open(struct inode *inode, struct file *file)
{
	file->private_data = isl29125_info->client_data;

	if (!file->private_data)
	{
		printk("null pointer!!\n");
		return -EINVAL;
	}
	
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int isl29125_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long isl29125_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	switch (cmd){
	case ALSPS_SET_ALS_MODE:
	case ALSPS_GET_ALS_RAW_DATA:
		break;

	default:
		printk("%s not supported = 0x%04x", __FUNCTION__, cmd);
		err = -ENOIOCTLCMD;
		break;
	}
	return err;    
}

static struct file_operations isl29125_fops = {
	.open = isl29125_open,
	.release = isl29125_release,
	.unlocked_ioctl = isl29125_unlocked_ioctl,
};
/*----------------------------------------------------------------------------*/
static struct miscdevice isl29125_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "isl29125",
	.fops = &isl29125_fops,
};

static int isl_early_suspend(struct early_suspend *h)
{
	int ret;
	short int reg;
	struct i2c_client *client = isl29125_info->client_data;

	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if(reg < 0) {
		printk(KERN_ALERT "%s: Failed to read CONFIG1_REG\n", __FUNCTION__); 
		goto err;
	}		

	reg &= RGB_OP_MODE_CLEAR;
	reg |= RGB_OP_STANDBY_MODE_SET; 

	/* Put the sensor device in standby mode */
	ret = i2c_smbus_write_byte_data(client, CONFIG1_REG, reg);
	if (ret < 0) {
		printk(KERN_ALERT "%s: Failed to write to CONFIG1_REG\n", __FUNCTION__);
		goto err;
	}	 	

//	mt_eint_mask(CUST_EINT_INT29125_NUM);	//vvdn
	if (isl29125_info->sensor_enable) {
	    __cancel_delayed_work(&isl29125_info->sensor_dwork);
    	}

	return 0;
err:
	return -1;
}

static int isl_late_resume(struct early_suspend *h)
{

	int ret;
	short int reg;
	struct i2c_client *client = isl29125_info->client_data;

	reg = i2c_smbus_read_byte_data(client, CONFIG1_REG);
	if(reg < 0) {
		printk(KERN_ALERT "%s: Failed to read CONFIG1_REG\n", __FUNCTION__); 
		goto err;
	}		

	reg &= RGB_OP_MODE_CLEAR;
	reg |= RGB_OP_GRB_MODE_SET; 

	/* Put the sensor device in active conversion mode */
	ret = i2c_smbus_write_byte_data(client, CONFIG1_REG, reg);
	if (ret < 0) {
		printk(KERN_ALERT "%s: Failed to write to CONFIG1_REG\n", __FUNCTION__);
		goto err;
	}	 	
//	mt_eint_unmask(CUST_EINT_INT29125_NUM);	//vvdn change
	if (isl29125_info->sensor_enable)
	    schedule_delayed_work(&isl29125_info->sensor_dwork, msecs_to_jiffies(POLL_DELAY));

	return 0;
err:
	return -1;
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
static int isl_sensor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
        int i = 0;
	short int reg;
	int ret;
	struct isl29125_data_t *isl29125; 

	isl29125 = kzalloc(sizeof(struct isl29125_data_t), GFP_KERNEL);
	if(!isl29125)
	{
		printk(KERN_ERR "%s: failed to alloc memory for module data\n", __FUNCTION__);
		return -ENOMEM;
	}

	i2c_set_clientdata(client, isl29125);
	isl29125->client_data = client;
	isl29125_info = isl29125;
 
	printk("+++%s\n",__func__);

	/* Initialize a mutex for synchronization in sysfs file access */
	mutex_init(&isl29125->rwlock_mutex);

	/* Read the device id register from isl29125 sensor device */
	mdelay(10);
	for(i = 0;i<10;i++)
	    reg = i2c_smbus_read_byte_data(client, DEVICE_ID_REG);
	printk("%s,i2c client address is 0x%x,chip id is 0x%x",__func__,client->addr,reg);
	/* Verify whether we have a valid sensor */
	if( reg != ISL29125_DEV_ID) {
		printk(KERN_ERR "%s: Invalid device id for isl29125 sensor device\n", __FUNCTION__);  
		goto err;
	}

	if(misc_register(&isl29125_device))
	{
		printk("ISL29125_device register failed\n");
		goto err;
	}
	/* Initialize the sensor interrupt thread that would be scheduled by sensor
	   interrupt handler */

	isl29125_input_create(isl29125);

	INIT_DELAYED_WORK(&isl29125->sensor_dwork, isl29125_work_handler); 

	/* Initialize the default configurations for isl29125 sensor device */ 
	initialize_isl29125(client);

	/* Register sysfs hooks */                                                  
	ret = sysfs_create_group(&client->dev.kobj, &isl29125_attr_group);          
	if(ret) {                                                                   
		printk(KERN_ERR "%s: Failed to create sysfs\n", __FUNCTION__);                    
		goto err;                                                           
	}                                                                           

//	isl29125_irq_init(client);	//vvdn change

#ifdef CONFIG_HAS_EARLYSUSPEND
	isl29125->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	isl29125->early_suspend.suspend = isl_early_suspend;
	isl29125->early_suspend.resume = isl_late_resume;
	register_early_suspend(&isl29125->early_suspend);
#endif

	return 0;
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
static int isl_sensor_remove(struct i2c_client *client)
{
	struct isl29125_data_t *isl29125 = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&isl29125->early_suspend);
#endif
	misc_deregister(&isl29125_device);
	kfree(isl29125);	
	return 0;
}

struct i2c_device_id isl_sensor_device_table[] = {
	{"isl29125", 0},
	{}
};

/* i2c device driver information */
static struct i2c_driver isl_sensor_driver = {
	.driver = {
		.name = "isl29125",
		.owner = THIS_MODULE,
	},
	.probe    = isl_sensor_probe	   ,
	.remove   = isl_sensor_remove	   ,
	.id_table = isl_sensor_device_table,
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
//	i2c_register_board_info(4, i2c_devs_info, 1);	//vvdn fix
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

module_init(isl29125_init);
module_exit(isl29125_exit);

MODULE_AUTHOR("Intersil Corporation; Meizu");
MODULE_LICENSE("GPLv2");
MODULE_DESCRIPTION("Driver for ISL29125 RGB light sensor");
