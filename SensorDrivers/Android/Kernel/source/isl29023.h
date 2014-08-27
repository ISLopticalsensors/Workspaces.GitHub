/*
 *	File		: isl29023.h
 *	Desc		: Base sample driver header to illustrate sensor features of ISL29177 prox sensor
 *	Ver		: 1.0.1
 *	Copyright	: Intersil Inc. 2014
 *	License		: GPLv2
 */

#ifndef _ISL29177_H_
#define _ISL29177_H_

/* REGISTER ADDR */
enum ISL29177_REGS {
	COMMAND1_REG,			/* 0x00h */
	COMMAND2_REG,			/* 0x01h */
	DATA_LSB,			/* 0x02h */
	DATA_MSB,			/* 0x03h */
	INTR_LT_LSB,			/* 0x04h */
	INTR_LT_MSB,			/* 0x05h */
	INTR_HT_LSB,			/* 0x06h */
	INTR_HT_MSB,			/* 0x07h */
};

/* COMMAND1_REG */
#define INTR_PERSISTENCY_MASK	(0x3 << 0)
#define INTR_FLAG_MASK		(0x1 << 2)
#define OPERATION_MODE_MASK	(0x7 << 5)

/* OPERATION MODE CONFIGURE */
#define POWER_DOWN_MODE		(0 << 5)
#define ALS_ONCE_MODE		(1 << 5)
#define IR_ONCE_MODE		(2 << 5)
#define ALS_CONTINUOUS_MODE	(5 << 5)
#define IR_CONTINUOUS_MODE	(6 << 5)

/* INTERRUPT PERSISTENCY CONFIGURE */
#define PERSISTENCY_1		(0 << 0)
#define PERSISTENCY_4		(1 << 0)
#define PERSISTENCY_8		(2 << 0)
#define PERSISTENCY_16		(3 << 0)

/* COMMAND2_REG */
#define ALS_RANGE_MASK		(0x3 << 0)
#define ADC_RESOLUTION_MASK	(0x3 << 2)

/* RANGE CONFIGURE */
#define RANGE_1000		(0 << 0)
#define RANGE_4000		(1 << 0)
#define RANGE_16000		(2 << 0)
#define RANGE_64000		(3 << 0)

/* RESOLUTION CONFIGURE */
#define RESOLUTION_16		(0 << 2)
#define RESOLUTION_12		(1 << 2)
#define RESOLUTION_8		(2 << 2)
#define RESOLUTION_4		(3 << 2)


#define ISL29023_I2C_ADDR	0x44 /* 1000100b */
#define REG_ARRAY_SIZE		0x08

/* TIMER INTERRUPT */
#define ISL29023_POLL_DELAY_MS	100
#define ISL29023_GPIO_IRQ	39
#define SYSFS_FAIL		-ENOMEM
#define SYSFS_SUCCESS		0
#define MAX_BUFF_SIZE		64


/* INITIAL VALUES */
#define ISL_LSB_MASK		0x00FF
#define ISL_MSB_MASK		0xFF00
#define ISL_FULL_MASK		0xFF
#define ISL_CLEAR		0x00
#define ALS_LO_THRESHOLD	0x0CCC
#define ALS_HI_THRESHOLD	0xCCCC

struct isl29023_pdata {
	unsigned int gpio_irq;

};
#endif /* _ISL29177_H_ */
