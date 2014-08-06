/*
 * isl29030.h - Intersil ISL29030  ALS & Proximity kernel header
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

#ifndef __ISL29030_H
#define __ISL29030_H

#define MODULE_NAME "isl29030"

#define SPAM_AMB_PROX_SET_ADDR     0x38
#define SPAM_AMB_PROX_CLR_ADDR     0x3A

#define ISL29030_ALS_IRQ   0
#define ISL29030_PROX_IRQ  1

/* ICS932S401 registers */
#define ISL29030_REG_VENDOR_REV                 0x06
#define ISL29030_VENDOR                         1
#define ISL29030_VENDOR_MASK                    0x0F
#define ISL29030_REV                            4
#define ISL29030_REV_SHIFT                      4
#define ISL29030_REG_DEVICE                     0x22
#define ISL29030_DEVICE                         22

// Table 1: all i2c registers and bits per register
#define REG_CMD_1       0x01 // configure, range is reg 1 bit 1
#define REG_CMD_2       0x02 // interrupt control

#define REG_INT_LOW_PROX    0x03 // 8 bits intr low thresh for prox
#define REG_INT_HIGH_PROX   0x04 // 8 bits intr high thresh for prox
#define REG_INT_LOW_ALS     0x05 // 8 bits intr low thresh for ALS-IR
#define REG_INT_LOW_HIGH_ALS    0x06 // 8 bits(0-3,4-7) intr high/low thresh for ALS-IR
#define REG_INT_HIGH_ALS    0x07 // 8 bits intr high thresh for ALS-IR

#define ISL_DEFAULT_PULSE_RATE  0x03
//[msi begin]: set the default PROX conversion sample rate to 16
#define ISL_DEFAULT_PROX_PRST   0x03
//[msi begin]: set the default PROX conversion sample rate to 1
#define ISL_DEFAULT_ASL_PRST   0
//#define ISL_PULSE_RATE_CONT  7
//t.y@change the default sleep rate 12.5ms.
//I want to move this to sysfs then it's configurable for each product
#define ISL_PULSE_RATE_CONT  0x06
#define ISL_DEFAULT_HT  0x0    /* 0 */
//[msi begin] set the default prox HT and LT interrupt treshold to 0
//#define ISL_DEFAULT_LT  0x64    /* 100 */
#define ISL_DEFAULT_LT  0x0    /* 0 */

//[msi begin] set the default ALS HT and LT bound to 0 and 20 
//#define ISL_INIT_BOUND_HI  0x1F4  /* 500 */
//#define ISL_INIT_BOUND_LO  0x032  /* 50 */
#define ISL_INIT_BOUND_HI  0x00C  /* 20 */
#define ISL_INIT_BOUND_LO  0x000  /* 0 */
#define ISL_ADJ_BOUND      0x00A  /* 10 */

#define PROXIMITY_NEAR  30      /* prox close threshold is 22-70mm */
#define PROXIMITY_FAR   1000        /* 1 meter */

#define REG_DATA_PROX       0x08 // 8 bits of PROX data
#define REG_DATA_LSB_ALS    0x09 // 8 bits of ALS data
#define REG_DATA_MSB_ALS    0x0A // 4 bits of ALS MSB data

#define REG_TEST1           0x0E
#define REG_TEST2           0x0F

#define ISL_FUNC_ALS        0
#define ISL_FUNC_PROX       1
#define ISL_FUNC_BOTH       2

#define ISL_MOD_DISABLE     0
#define ISL_MOD_ENABLE      1
#define ISL_MOD_POWERDOWN   2

#define PROX_EN_MASK          0x80 // prox sense on mask, 1=on, 0=off
#define PROX_CONT_MASK        0x70 // prox sense contimnuous mask
#define ISL_CLEAR_PULSE_RATE  0x8F // 1000 1111
//IR_CURRENT_MASK is now PROX_DR_MASK with just 0 or 1 settings
#define PROX_DR_MASK          0x08 // prox drive pulse 220ma sink mask def=0 110ma
#define ALS_EN_MASK           0x04 // prox sense enabled contimnuous mask
#define ALS_RANGE_HIGH_MASK   0x02 // ALS range high LUX mask
#define ALSIR_MODE_SPECT_MASK 0x01 // prox sense contimnuous mask

#define PROX_INT_CLEAR      0x80
#define ALS_INT_CLEAR       0x08

#define IR_CURRENT_MASK     0xC0
#define IR_FREQ_MASK        0x30
#define SENSOR_RANGE_MASK   0x03
#define ISL_RES_MASK        0x0C

/* lux factors */
/* FIXME: Right now float libraries are not being pulled in, need to
   investigate but in the meantime using longs and x1000 factor */
//t.y MSI TODO:  We should investigate to use float libraries
//#define ALS_HIGH_FACTOR     0.522
//#define ALS_LOW_FACTOR      0.0326
#define ALS_HIGH_FACTOR     522
#define ALS_LOW_FACTOR      33
#define ALS_MAX_LUX_FACTOR  500

//msi [begin]: workaround the proximity sensor not working under direct sun
#define ALS_PROX_USERSPACE_REPORT_DISABLE   0
#define ALS_PROX_USERSPACE_REPORT_ENABLE    1



struct isl29030_platform_data {
	int	gpio_irq;
        int     (*get_amb_prox_irq_state)(void);
        void    (*clear_amb_prox_irq)(void);

        int     (*init_platform_hw)(void);
        void    (*exit_platform_hw)(void);

        void    (*enable_platform_hw)(void);
        void    (*disable_platform_hw)(void);
};


#endif  /* ifndef __ISL29030_H */
