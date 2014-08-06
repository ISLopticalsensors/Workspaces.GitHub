/******************************************************************************
        File            : isl29028A.h

        Description     : Header file for the driver isl29028A.c that services the ISL29028A
                          light/prox sensor

        License         : GPLv2

        Copyright       : Intersil Corporation (c) 2013
	Author 		: sanoj.kumar@vvdntech.com
*******************************************************************************/

#ifndef _ISL29028A_H_
#define _ISL29028A_H_
#define ISL29028A_I2C_ADDR			0x45
#define ISL29028_NAME 				"isl29028A"

/* ISL29028A REGISTER SET */

/************************** PROX/ALS CONFIGURATION REGISTER *************************/
#define CONFIG_REG_1				0x01

/* Operating Mode Control bits */
#define ISL_OP_MODE_ALS_SENSING			(1 << 2)
#define ISL_OP_MODE_IR_SENSING			0x05				
#define ISL_OP_MODE_PROX			(1 << 7)
#define ISL_OP_POWERDOWN			0x00
/* ALS Range control bits */
#define ISL_ALS_RANGE_POS			1
#define ISL_ALS_LOW_RANGE			(0 << ISL_ALS_RANGE_POS)
#define ISL_ALS_HIGH_RANGE			(1 << ISL_ALS_RANGE_POS)

/* ALSIR Enable bits */
#define ISL_ALSIR_EN_POS			2
#define ISL_ALSIR_ENABLE			(0 << ISL_ALSIR_EN_POS)
#define ISL_ALSIR_DISABLE			(1 << ISL_ALSIR_EN_POS)

/* Proximity Drive bits */
#define ISL_PROX_DR_POS				3
#define ISL_PROX_DR_110mA			(0 << ISL_PROX_DR_POS)		
#define ISL_PROX_DR_220mA			(1 << ISL_PROX_DR_POS)			

/* Proximity Sleep bits*/
#define ISL_PROX_SLP_POS			4
#define ISL_PROX_SLP_800ms			(0x00 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_400ms			(0x01 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_200ms			(0x02 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_100ms			(0x03 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_75ms			(0x04 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_50ms 			(0x05 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_12500us 			(0x06 << ISL_PROX_SLP_POS)
#define ISL_PROX_SLP_0ms 			(0x07 << ISL_PROX_SLP_POS)

/* Proximity Enable bits*/
#define ISL_PROX_EN_POS				7
#define ISL_PROX_DISABLE			(0x00 << ISL_PROX_EN_POS)				
#define ISL_PROX_ENABLE				(0x01 << ISL_PROX_EN_POS)

/*********************** PROX/ALS INTERRUPT CONTROL REGISTER *************************/
#define CONFIG_REG_2				0x02

/* ALS Persistency Bits */
#define ISL_ALS_PERST_POS			1
#define ISL_ALS_PERST_SET_1			(0x00 << ISL_ALS_PERST_POS)
#define ISL_ALS_PERST_SET_4			(0x01 << ISL_ALS_PERST_POS)
#define ISL_ALS_PERST_SET_8 			(0x02 << ISL_ALS_PERST_POS)
#define ISL_ALS_PERST_SET_16			(0x03 << ISL_ALS_PERST_POS)

/* PROX Persistency bits */
#define ISL_PROX_PERST_POS			5
#define ISL_PROX_PERST_SET_1			(0x00 << ISL_PROX_PERST_POS)
#define ISL_PROX_PERST_SET_4			(0x01 << ISL_PROX_PERST_POS)
#define ISL_PROX_PERST_SET_8			(0x02 << ISL_PROX_PERST_POS)
#define ISL_PROX_PERST_SET_16			(0x03 << ISL_PROX_PERST_POS)

/************************************ THRESHOLD REGISTERS ********************************/
/* PROX Threshold registers */
#define ISL_PROX_LT_BYTE			0x03
#define ISL_PROX_HT_BYTE			0x04

/* ALS/IR Threshold registers */
#define ISL_ALSIR_TH1				0x05
#define ISL_ALSIR_TH2				0x06
#define ISL_ALSIR_TH3				0x07

/*********************************** DATA REGISTERS **************************************/
/* PROXIMITY Data Register */
#define ISL_PROX_DATA				0x08

/* ALSIR Data Register */
#define ISL_ALSIR_DT1				0x09
#define ISL_ALSIR_DT2				0x0A

#define CONFIG_REG_TEST1			0x0E
#define CONFIG_REG_TEST2			0x0F

/********************************** DEFAULT GLOBAL REGISTER MASK ***************************************/
#define ISL_12BIT_THRES_MASK			0x0FFF
#define ISL_INT_CLEAR_MASK			0x66
#define ISL_REG_CLEAR				0x00
#define ISL_REG_TEST_1				0x0E
#define ISL_REG_TEST_2				0x29
#define ISL_REG_1_DEF				0x06
#define ISL_REG_2_DEF				0x44
#define ISL_PROX_LT_DEF				0x0C
#define ISL_PROX_HT_DEF				0xCC
#define ISL_ALSIR_TH1_DEF			0xCC
#define ISL_ALSIR_TH2_DEF			0xC0
#define ISL_ALSIR_TH3_DEF			0xCC

#ifndef __dbg_read_err
#define __dbg_read_err(fmt, var) printk(KERN_ERR \
                               "isl29028A:"fmt" :i2c read error\n", var)
#endif

#ifndef __dbg_write_err
#define __dbg_write_err(fmt, var) printk(KERN_ERR \
                               "isl29028A:"fmt" :i2c write error\n", var)
#endif

#ifndef __dbg_invl_err
#define __dbg_invl_err(fmt, var) printk(KERN_INFO \
                               "isl29028A:"fmt" :Invalid input\n", var)
#endif

#ifndef TYPEDEF_UCHAR
typedef unsigned char uchar;
#endif
typedef short int bytes_r;
enum isl{ 
	ALS_MODE , 
	PROX_MODE 
   };

struct isl29028A_platform_data {
	unsigned int gpio_irq;
};

#endif
