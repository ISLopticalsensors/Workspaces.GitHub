/*
 *	File 		: isl29177.h
 *	Desc 		: Base sample driver header to illustrate sensor features of ISL29177 prox sensor
 *	Ver  		: 1.0.1
 * 	Copyright 	: Intersil Inc. 2014
 * 	License 	: GPLv2
 */

#ifndef _ISL29177_H_
#define _ISL29177_H_

/* REGISTER ADDR */
enum ISL29177_REGS {
	DEVICE_ID_REG,			/* 0x00h */
	CONFIG0_REG,			/* 0x01h */
	CONFIG1_REG,			/* 0x02h */
	INT_CONFIG_REG,			/* 0x03h */
	PROX_INT_TL_REG,		/* 0x04h */
	PROX_INT_TH_REG,		/* 0x05h */
	STATUS_REG,			/* 0x06h */
	PROX_DATA_REG,			/* 0x07h */
	PROX_AMBIR_REG,			/* 0x08h */
	CONFIG2_REG,			/* 0x09h */
	TEST_MODE_2,			/* 0x0Ah */
	TEST_MODE_3,			/* 0x0Bh */
	TEST_MODE_4,			/* 0x0Ch */
	TEST_MODE_5,			/* 0x0Dh */
	FUSE_REG,			/* 0x0Eh */
	FUSE_CONTROL,			/* 0x0Fh */

};

/* CONFIG0_REG */
#define PROX_EN_MASK		(0x1 << 7)
#define PROX_SLP_MASK		(0xF << 4)
#define SHRT_DIS_MASK		(0x1 << 3)
#define IRDR_DRV_MASK		(0x3 << 0)

/* CONFIG1_REG */
#define PROX_OFFSET_MASK  	(0x1F << 0)

/* INT_CONFIG_REG */
#define PROX_PRST_MASK	  	(0x3 << 4)
#define INT_PRX_EN_MASK	  	(0x1 << 3)
#define INT_CNV_DN_EN_MASK	(0x1 << 2)
#define INT_SHRT_EN_MASK	(0x1 << 1)
#define INT_WSH_EN_MASK	  	(0x1 << 0) 

/* STATUS_REG */
#define PWR_FAIL_MASK		(0x1 << 4)
#define PROX_INT_FLAG_MASK	(0x1 << 3)
#define CNV_DN_FLG_MASK		(0x1 << 2)
#define SHRT_FLG_MASK		(0x1 << 1)
#define WSH_FLG_MASK		(0x1 << 0)


#define ISL29177_I2C_ADDR	0x44 /* 1000100b */	
#define REG_ARRAY_SIZE		0x10 

/* TIMER INTERRUPT */
#define ISL29177_POLL_DELAY_MS	100
#define ISL29177_GPIO_IRQ      	39 
#define SYSFS_FAIL		-ENOMEM
#define SYSFS_SUCCESS		0
#define MAX_BUFF_SIZE 		128
#define ISL29177_PD_MODE 	0x00 
#define ISL_FULL_MASK		0xFF


/* IRDR Current values */
#define IRDR_CURR_3600uA	0	
#define IRDR_CURR_7100uA	1
#define IRDR_CURR_10700uA	2
#define IRDR_CURR_12500uA	3
#define IRDR_CURR_14300uA	4
#define IRDR_CURR_15000uA	5
#define IRDR_CURR_17500uA	6
#define IRDR_CURR_20000uA	7

/* Prox sleep values */
#define PROX_SLEEP_400ms	0
#define PROX_SLEEP_200ms	1
#define PROX_SLEEP_100ms	2
#define PROX_SLEEP_50ms		3
#define PROX_SLEEP_25ms		4

/* CALIBRATION PARAMETER */
#define IRDR_CURRENT		IRDR_CURR_12500uA
#define PROX_SLEEP_MS		PROX_SLEEP_25ms	
#define PROX_HI_THRESHOLD	25
#define PROX_LO_THRESHOLD	20

struct lut {
	long prox_offset;
	unsigned char range;
	long offset;
};
struct isl29177_pdata {
	unsigned int gpio_irq;

}; 
#endif /* _ISL29177_H_ */
