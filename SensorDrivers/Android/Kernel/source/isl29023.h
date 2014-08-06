/******************************************************************************
        File            : isl29023.h

        Description     : Header file for the driver isl29023.c that 
			  services the ISL29023 ALS light sensor

        License         : GPLv2

        Copyright       : Intersil Corporation (c) 2013
*******************************************************************************/
#ifndef _ISL29023_H_
#define _ISL29023_H_
#define ISL29023_I2C_ADDR			0x44
#define MODULE_NAME				"isl29023"

/*  ISL29023 REGISTER SET */

/************************** COMMAND REGISTER 1 *********************************/
#define REG_CMD_1				0x00

/*Operating modes of register 1*/
#define ALS_OP_MODE_POS				5
#define ALS_OP_MODE_CLEAR			0x00 	
#define ALS_OP_MODE_PWDN_SET                    (0x00 << ALS_OP_MODE_POS)			
#define ALS_OP_MODE_ALS_CONT			(0x05 << ALS_OP_MODE_POS)	
#define ALS_OP_MODE_IR_CONT			(0x06 << ALS_OP_MODE_POS)	
#define ALS_OP_MODE_ALS_ONCE			(0x01 << ALS_OP_MODE_POS)
#define ALS_OP_MODE_IR_ONCE			(0x02 << ALS_OP_MODE_POS)
#define ALS_OP_MODE_RESERVERD			(0x04 << ALS_OP_MODE_POS)
#define ISL_OP_SET_MASK				0xFC

/* Interrupt persistency field */
#define INTR_PERSIST_BIT_POS                   0
#define INTR_PERSIST_BIT_CLEAR                 (0xfc << INTR_PERSIST_BIT_POS)
#define INTR_PERSIST_SET_1BIT                  0x00
#define INTR_PERSIST_SET_4BIT                  (0x01 << INTR_PERSIST_BIT_POS)
#define INTR_PERSIST_SET_8BIT                  (0x02 << INTR_PERSIST_BIT_POS)
#define INTR_PERSIST_SET_16BIT                 (0x03 << INTR_PERSIST_BIT_POS)
#define ISL_INTR_PERS_MASK 			0xFC

/************************* COMMAND REGISTER 2 *********************************/
#define REG_CMD_2				0x01

/* ADC Resolution Bit Field */
#define ADC_BIT_RES_POS                  	2
#define ADC_RES_16BIT_SET                	~(0x03 << ADC_BIT_RES_POS)
#define ADC_RES_12BIT_SET               	(0x01 << ADC_BIT_RES_POS)  
#define ADC_RES_8BIT_SET               		(0x02 << ADC_BIT_RES_POS) 
#define ADC_RES_4BIT_SET               		(0x03 << ADC_BIT_RES_POS) 
#define ISL_RES_MASK				0x0C
#define ISL_ADC_READ_MASK			0xF3

/* Range Select Bits */
#define ALS_RANGE_SELECT_POS			0
#define ALS_RANGE_1000_SET			~(0x03 << ALS_RANGE_SELECT_POS)
#define ALS_RANGE_4000_SET			(0x01 << ALS_RANGE_SELECT_POS)
#define ALS_RANGE_16000_SET			(0x02 << ALS_RANGE_SELECT_POS)
#define ALS_RANGE_64000_SET			(0x03 << ALS_RANGE_SELECT_POS)

/* Data Registers Bits*/
#define ALS_DATA_LSB				0x02
#define ALS_DATA_MSB				0x03

/*  ISL29023 SYSFS FILE PERMISSION */
#define ISL_SYSFS_PERM		0666

/*************** LOW AND HIGH INTERRUPT THRESHOLD REGISTERS ********************/
#define LOW_THRESHOLD_LBYTE_REG                 0x04
#define LOW_THRESHOLD_HBYTE_REG                 0x05
#define HIGH_THRESHOLD_LBYTE_REG                0x06
#define HIGH_THRESHOLD_HBYTE_REG                0x07

#define ISL_MOD_POWERDOWN       		0
#define ISL_MOD_ALS_ONCE        		1
#define ISL_MOD_IR_ONCE         		2
#define ISL_MOD_RESERVED        		4
#define ISL_MOD_ALS_CONT        		5
#define ISL_MOD_IR_CONT         		6
#define ALS_RANGE_MASK				0x03
#define ISL29023_INTR_GPIO			39

typedef unsigned int uint32;
typedef int int32;
typedef signed short int int16 ;
typedef unsigned short int uint16;
typedef unsigned char uchar ;
typedef unsigned long ulong ;

struct isl29023_platform_data {
	uint32 gpio_irq;
    };

#endif