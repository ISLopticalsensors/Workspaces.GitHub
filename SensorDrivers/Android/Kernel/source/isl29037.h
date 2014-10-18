/*
 *	File		: isl29037.h
 *	Desc		: Base sample driver header to illustrate sensor features of ISL29037 prox sensor
 *	Copyright	: Intersil Inc. 2014
 *	License		: GPLv2
 */

#ifndef _ISL29037_H_
#define _ISL29037_H_

/* REGISTER ADDR */
enum ISL29037_REGS {
	DEVICE_ID_REG,			/* 0x00h */
	CONFIG0_REG,			/* 0x01h */
	CONFIG1_REG,			/* 0x02h */
	CONFIG2_REG,			/* 0x03h */
	INT_CONFIG_REG,			/* 0x04h */
	PROX_INT_TL_REG,		/* 0x05h */
	PROX_INT_TH_REG,		/* 0x06h */
	ALS_INT_TL_REG,			/* 0x07h */
	ALS_INT_TLH_REG,		/* 0x08h */
	ALS_INT_TH_REG,			/* 0x09h */
	PROX_DATA_REG,			/* 0x0Ah */
	PROX_DATA_HB,			/* 0x0Bh */
	PROX_DATA_LB,			/* 0x0Ch */
	PROX_AMBIR_REG,			/* 0x0Dh */
	CONFIG3_REG,			/* 0x0Eh */
};

/* CONFIG0_REG */
#define PROX_EN_MASK		(0x1 << 5)

/* CONFIG1_REG */
#define ALS_EN_MASK		(0x1 << 2)
#define WSH_FLG_MASK		(0x1 << 0)


#define ISL29037_I2C_ADDR	0x44 /* 1000100b */
#define REG_ARRAY_SIZE		0x0F

/* TIMER INTERRUPT */
#define ISL29037_POLL_DELAY_MS	100
#define SYSFS_FAIL		-ENOMEM
#define SYSFS_SUCCESS		0
#define MAX_BUFF_SIZE		120
#define ISL29037_PD_MODE	0x00	
#define ISL_FULL_MASK		0xFF
#define ISL_LSB_MASK		0x0F
#define ISL_MSB_MASK		0xF0
#define WSHOUT_MASK		(0x1 << 0)
#define PROX_AMBIR_MASK		0xFE

/* IRDR Current values */
#define IRDR_CURR_31250uA	0
#define IRDR_CURR_62500uA	1
#define IRDR_CURR_125000uA	2
#define IRDR_CURR_250000uA	3

/* Prox sleep values */	
#define PROX_SLEEP_400ms	0
#define PROX_SLEEP_100ms	1
#define PROX_SLEEP_50ms		2
#define PROX_SLEEP_25ms		3
#define PROX_SLEEP_12500us	4
#define PROX_SLEEP_6250us	5
#define PROX_SLEEP_3125us	6
#define PROX_SLEEP_0ms		7

/* Als Range values */

#define ALS_RANGE_125lux	0
#define ALS_RANGE_250lux	1
#define ALS_RANGE_2000lux	2
#define ALS_RANGE_4000lux	3

#define PROX_HI_THRESHOLD	25
#define PROX_LO_THRESHOLD	20

#define ALS_LO_THRESHOLD	0x0C0
#define ALS_HI_THRESHOLD	0x0CC

struct isl29037_pdata {
	unsigned int gpio_irq;
};
#endif /* _ISL29037_H_ */
