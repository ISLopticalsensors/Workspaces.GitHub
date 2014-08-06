/******************************************************************************
        File            : isl29038.h

        Description     : Header file for the driver isl29038.c that services the ISL29038
                          light sensor

        License         : GPLv2

        Copyright       : Intersil Corporation (c) 2013
*******************************************************************************/

#ifndef _ISL29038_H_
#define _ISL29038_H_
#define ISL29038_I2C_ADDR                       0x44
#define ISL29038_NAME                           "isl29038"

/* ISL29038 REGISTER SET */
/******************** Device ID register **********************************/
#define DEVICE_ID_REG				0x00
#define ISL29038_ID				0xC0
#define ISL29038_ID_MASK			0xf8

/******************** PROX CONFIGURATION REGISTER *************************/
#define CONFIG_REG_0				0x01

/* IR LED Current Driver select */
#define ISL29038_IRDR_DRV_31250uA		0x00
#define ISL29038_IRDR_DRV_62500uA		0x01
#define ISL29038_IRDR_DRV_125mA			0x02
#define ISL29038_IRDR_DRV_250mA			0x03
#define W_IR_LED_CURR_MASK			0xfc

/* PROX Sleep time Select */
#define ISL29038_PROX_SLP_SHIFT			2
#define ISL29038_PROX_SLP_400ms			(0x00 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_100ms			(0x01 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_50ms			(0x02 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_25ms			(0x03 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_12500us		(0x04 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_6250us		(0x05 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_3125us		(0x06 << ISL29038_PROX_SLP_SHIFT)
#define ISL29038_PROX_SLP_0ms			(0x07 << ISL29038_PROX_SLP_SHIFT)
#define W_PROX_SLP_MASK				0xE3
#define R_PROX_SLP_MASK				0x1f

/* PROX_EN enable Proximity sensing */
#define ISL29038_PROX_EN_SHIFT			5
#define ISL29038_PROX_ENABLE			(0x01 << ISL29038_PROX_EN_SHIFT)
#define ISL29038_PROX_DISABLE			(0x00 << ISL29038_PROX_EN_SHIFT)
#define W_PROX_MODE_MASK			0xdf
#define R_PROX_MODE_MASK			0x20

/***************** PROX/ALS CONFIGURATION REGISTER ************************/

#define REG_CONFIG_1				0x02

/* ALS Range select */
#define ISL_ALS_RANGE_SHIFT			0
#define ISL_ALS_RANGE_125Lux			(0x00 << ISL_ALS_RANGE_SHIFT)
#define ISL_ALS_RANGE_250Lux			(0x01 << ISL_ALS_RANGE_SHIFT)
#define ISL_ALS_RANGE_2000Lux			(0x02 << ISL_ALS_RANGE_SHIFT)
#define ISL_ALS_RANGE_4000Lux			(0x03 << ISL_ALS_RANGE_SHIFT)
#define R_ALS_RANGE_MASK			0xfc

/* ALS_EN Enable ALS sensing */
#define ISL_ALS_EN_SHIFT			2
#define ISL_ALS_ENABLE				(0x01 << ISL_ALS_EN_SHIFT)
#define ISL_ALS_DISABLE				(0x00 << ISL_ALS_EN_SHIFT)
#define W_ALS_MODE_MASK				0xfb

/* Proximity offset compensation */
#define ISL_PROX_OFFSET_COMP_MASK		0x8D	
#define R_PROX_OFFST_MASK			0x78
#define W_PROX_OFFST_COMP_MASK			0x87

/* INT algorithm select */
#define ISL_INT_ALGO_SHIFT			7
#define ISL_INT_ALGO_WINDOW_COMP		(0x00 << ISL_INT_ALGO_SHIFT) 
#define ISL_INT_ALGO_HYSTER_WIND		(0x01 << ISL_INT_ALGO_SHIFT) 
#define W_INTR_ALGO_MASK			0x7f
#define R_INTR_ALGO_MASK			0x80

/******************* ALS IR COMPENSATION REGISTER *************************/
#define REG_CONFIG_2				0x03

#define ISL_ALSIR_COMP_MASK			0x1F

/******************* INTERRUPT CONFIGURATION REGISTER *********************/
#define CONFIG_REG_3				0x04

/* Interrupt output configuration */
#define ISL_INT_CFG_SHIFT			0
#define ISL_INT_CFG_ALS_and_PROX		(0x01 << ISL_INT_CFG_SHIFT) 
#define ISL_INT_CFG_ALS_or_PROX			(0x00 << ISL_INT_CFG_SHIFT) 

/* ALS INTERRUPT PERSISTENCY */
#define ISL_ALS_INTR_PERSIST_SHIFT		1
#define ISL_ALS_INTR_PERSIST_SET_1		(0x00 << ISL_ALS_INTR_PERSIST_SHIFT)
#define ISL_ALS_INTR_PERSIST_SET_2		(0x01 << ISL_ALS_INTR_PERSIST_SHIFT)
#define ISL_ALS_INTR_PERSIST_SET_4		(0x02 << ISL_ALS_INTR_PERSIST_SHIFT)
#define ISL_ALS_INTR_PERSIST_SET_8		(0x03 << ISL_ALS_INTR_PERSIST_SHIFT)
#define R_ALS_PERST_MASK			0x06
#define W_ALS_PERST_MASK			0x61

/* PROX INTERRUPT PERSISTENCY */
#define ISL_PROX_INTR_PERSIST_SHIFT		5
#define ISL_PROX_INTR_PERSIST_SET_1		(0x00 << ISL_PROX_INTR_PERSIST_SHIFT)
#define ISL_PROX_INTR_PERSIST_SET_2		(0x01 << ISL_PROX_INTR_PERSIST_SHIFT)
#define ISL_PROX_INTR_PERSIST_SET_4		(0x02 << ISL_PROX_INTR_PERSIST_SHIFT)
#define ISL_PROX_INTR_PERSIST_SET_8		(0x03 << ISL_PROX_INTR_PERSIST_SHIFT)
#define R_PROX_PERSIST_MASK			0x60
#define W_PROX_PERSIST_MASK			0x07

/******************** PROX INT THRESHOLD **********************************/
#define ISL_PROX_INT_TL				0x05 
#define ISL_PROX_INT_TH				0x06

/******************* ALS INT THRESHOLD ************************************/
#define ISL_ALS_INT_TL0				0x08
#define ISL_ALS_INT_TL1				0x07
#define ISL_ALS_INT_TH0				0x09
#define ISL_ALS_INT_TH1				0x08
#define ISL_HT_MASK				0x0f
#define ISL_LT_MASK				0xf0

/******************* PROXIMITY DATA REGISTER ******************************/
#define ISL_PROX_DATA_REG			0x0A


/******************* ALS DATA REGISTER ***********************************/
#define ISL_ALS_DATA_LBYTE			0x0C
#define ISL_ALS_DATA_HBYTE			0x0B

/******************* PROX_AMBIR REGISTER **********************************/
#define ISL_PROX_AMBIR_REG 			0x0D
#define R_PROX_AMBIR_DATA_MASK			0xfe

/******************* RESET CONFIGURATION REGISTER *************************/
#define CONFIG_REG_4				0x0E

#define ISL_SOFT_RESET				0x38
#define ISL29038_GPIO_IRQ			39
#define ISL29038_SYSFS_PERM			0666

#ifndef __dbg_read_err
#define __dbg_read_err(fmt, var) printk(KERN_ERR \
			       "isl29038:"fmt" :i2c read error\n", var)
#endif

#ifndef __dbg_write_err
#define __dbg_write_err(fmt, var) printk(KERN_ERR \
			       "isl29038:"fmt" :i2c write error\n", var)
#endif

#ifndef __dbg_invl_err
#define __dbg_invl_err(fmt, var) printk(KERN_ERR \
			       "isl29038:"fmt" :Invalid input\n", var)
#endif

#ifndef TYPEDEF_UCHAR
typedef unsigned char uchar;
#endif
typedef short int bytes_r;

struct isl29038_platform_data {
        unsigned int gpio_irq;
};

#endif
