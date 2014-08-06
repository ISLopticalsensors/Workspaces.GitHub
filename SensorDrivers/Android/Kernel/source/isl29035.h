/******************************************************************************
        File            : isl29035.h

        Description     : Header file for the driver isl29035.c that services the ISL29035
                          light sensor

        License         : GPLv2

        Copyright       : Intersil Corporation (c) 2013
*******************************************************************************/


#ifndef _ISL29035_H_
#define _ISL29035_H_
#define ISL29035_I2C_ADDR			0x44
#define ISL29035_NAME				"isl29035"

/* ISL29035 REGISTER SET */

/*********************** COMMAND REGISTER 1 *****************/
#define ISL29035_CMD_REG_1			0x00

/* Operating modes of register 1 */
#define ISL29035_OP_MODE_POS			5
#define ISL29035_OP_MODE_CLEAR			0x00
#define ISL29035_OP_MODE_PWDN_SET		(0x00 << ISL29035_OP_MODE_POS)
#define ISL29035_OP_MODE_ALS_CONT		(0x05 << ISL29035_OP_MODE_POS)
#define ISL29035_OP_MODE_IR_CONT		(0x06 << ISL29035_OP_MODE_POS)
#define ISL29035_OP_MODE_ALS_ONCE		(0x01 << ISL29035_OP_MODE_POS)
#define ISL29035_OP_MODE_IR_ONCE		(0x02 << ISL29035_OP_MODE_POS)

/* Interrupt Persistence field */
#define ISL29035_PERSIST_BIT_POS                0
#define ISL29035_PERSIST_BIT_CLEAR              (0xfc << ISL29035_PERSIST_BIT_POS)
#define ISL29035_PERSIST_SET_1BIT               0x00
#define ISL29035_PERSIST_SET_4BIT               (0x01 << ISL29035_PERSIST_BIT_POS)
#define ISL29035_PERSIST_SET_8BIT               (0x02 << ISL29035_PERSIST_BIT_POS)
#define ISL29035_PERSIST_SET_16BIT              (0x03 << ISL29035_PERSIST_BIT_POS)
#define ISL29035_PERS_MASK 			0xFC

/* COMMAND REGISTER 2 */
#define ISL29035_CMD_REG_2			0x01

/* ADC Resolution bits */
#define ISL29035_ADC_BIT_RES_POS                2
#define ISL29035_ADC_RES_16BIT_SET              (0x00 << ISL29035_ADC_BIT_RES_POS)
#define ISL29035_ADC_RES_12BIT_SET              (0x01 << ISL29035_ADC_BIT_RES_POS)  
#define ISL29035_ADC_RES_8BIT_SET               (0x02 << ISL29035_ADC_BIT_RES_POS) 
#define ISL29035_ADC_RES_4BIT_SET               (0x03 << ISL29035_ADC_BIT_RES_POS) 
#define ISL29035_ADC_RES_MASK			0x0C
#define ISL29035_ADC_READ_MASK			0xF3

/* Range Select Bits */
#define ISL29035_RANGE_SELECT_POS		0
#define ISL29035_RANGE_1000_SET			(0x00 << ISL29035_RANGE_SELECT_POS)
#define ISL29035_RANGE_4000_SET			(0x01 << ISL29035_RANGE_SELECT_POS)
#define ISL29035_RANGE_16000_SET		(0x02 << ISL29035_RANGE_SELECT_POS)
#define ISL29035_RANGE_64000_SET		(0x03 << ISL29035_RANGE_SELECT_POS)

/* Data Registers Bits */
#define ISL29035_DATA_LSB			0x02
#define ISL29035_DATA_MSB			0x03

/*  ISL29035 SYSFS FILE PERMISSION */
#define ISL29035_SYSFS_PERM			0666
#define ISL29035_SYSFS_PERM_RONL		0444

/*************** LOW AND HIGH INTERRUPT THRESHOLD REGISTERS ********************/
#define ISL29035_LT_LBYTE                 	0x04
#define ISL29035_LT_HBYTE                 	0x05
#define ISL29035_HT_LBYTE                	0x06
#define ISL29035_HT_HBYTE                	0x07

#define ISL_MOD_POWERDOWN       		0
#define ISL_MOD_ALS_ONCE        		1
#define ISL_MOD_IR_ONCE         		2
#define ISL_MOD_ALS_CONT        		5
#define ISL_MOD_IR_CONT         		6

#define ISL29035_RANGE_MASK			0x03
#define ISL29035_INTR_GPIO			39

/* Device id register */
#define ISL29035_DEV_ID_REG			0x0f
#define ISL29035_DEVICE_ID			0x28
#define ISL29035_DEV_ID_MASK			0x38

/* Default values of registers */
#define ISL29035_CMD_REG_1_DEF			0xa3
#define ISL29035_CMD_REG_2_DEF			0x03
#define ISL29035_LT_LBYTE_DEF			0xCC
#define ISL29035_LT_HBYTE_DEF			0x0C
#define ISL29035_HT_LBYTE_DEF			0xCC
#define ISL29035_HT_HBYTE_DEF			0xCC

#ifndef __dbg_read_err
#define __dbg_read_err(fmt, var) printk(KERN_ERR \
                              ISL29035_NAME fmt" :i2c read error\n", var)
#endif

#ifndef __dbg_write_err
#define __dbg_write_err(fmt, var) printk(KERN_ERR \
                               ISL29035_NAME fmt" :i2c write error\n", var)
#endif

#ifndef __dbg_invl_err
#define __dbg_invl_err(fmt, var) printk(KERN_ERR \
                               ISL29035_NAME fmt" :Invalid input\n", var)
#endif


struct isl29035_platform_data {
	uint32_t gpio_irq;
    };

#endif
