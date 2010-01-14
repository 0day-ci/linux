/*
 * Xilinx PSS GPIO device driver
 *
 * 2009 (c) Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>


#define DRIVER_NAME "xilinx_gpiopss"

/* Register offsets for the GPIO device */

#define XGPIOPSS_DATA_LSW_OFFSET(BANK)	(0x000 + (8 * BANK)) /* LSW Mask &
								Data -WO */
#define XGPIOPSS_DATA_MSW_OFFSET(BANK)	(0x004 + (8 * BANK)) /* MSW Mask &
								Data -WO */
#define XGPIOPSS_DATA_OFFSET(BANK)	(0x040 + (4 * BANK)) /* Data Register
								-RW */
#define XGPIOPSS_BYPM_OFFSET(BANK)	(0x200 + (0x40 * BANK)) /* Bypass mode
								reg -RW */
#define XGPIOPSS_DIRM_OFFSET(BANK)	(0x204 + (0x40 * BANK)) /* Direction
								mode reg-RW */
#define XGPIOPSS_OUTEN_OFFSET(BANK)	(0x208 + (0x40 * BANK)) /* Output
								enable reg-RW
								 */
#define XGPIOPSS_INTMASK_OFFSET(BANK)	(0x20C + (0x40 * BANK)) /* Interrupt
								mask reg-RO */
#define XGPIOPSS_INTEN_OFFSET(BANK)	(0x210 + (0x40 * BANK)) /* Interrupt
								enable reg-WO
								 */
#define XGPIOPSS_INTDIS_OFFSET(BANK)	(0x214 + (0x40 * BANK)) /* Interrupt
								disable reg-WO
								 */
#define XGPIOPSS_INTSTS_OFFSET(BANK)	(0x218 + (0x40 * BANK)) /* Interrupt
								status reg-RO
								 */
#define XGPIOPSS_INTTYPE_OFFSET(BANK)	(0x21C + (0x40 * BANK)) /* Interrupt
								type reg-RW
								 */
#define XGPIOPSS_INTPOL_OFFSET(BANK)	(0x220 + (0x40 * BANK)) /* Interrupt
								polarity reg
								-RW */
#define XGPIOPSS_INTANY_OFFSET(BANK)	(0x224 + (0x40 * BANK)) /* Interrupt on
								any, reg-RW */

/* Read/Write access to the GPIO PSS registers */
#define xgpiopss_readreg(offset)	__raw_readl(offset)
#define xgpiopss_writereg(val, offset)	__raw_writel(val, offset)

static unsigned int xgpiopss_pin_table[] = {
	31, /* 0 - 31 */
	53, /* 32 - 53 */
	85, /* 54 - 85 */
	117 /* 86 - 117 */
};

/**
 * struct xgpiopss - gpio device private data structure
 * @chip:	instance of the gpio_chip
 * @base_addr:	base address of the GPIO device
 * @gpio_lock:	lock used for synchronization
 */
struct xgpiopss {
	struct gpio_chip chip;
	void __iomem *base_addr;
	spinlock_t gpio_lock;
};

/**
 * xgpiopss_get_bank_pin - Get the bank number and pin number within that bank
 * for a given pin in the GPIO device
 * @pin_num:	gpio pin number within the device
 * @bank_num:	an output parameter used to return the bank number of the gpio
 *		pin
 * @bank_pin_num: an output parameter used to return pin number within a bank
 *		  for the given gpio pin
 *
 * Returns the bank number.
 */
static inline void xgpiopss_get_bank_pin(unsigned int pin_num,
					 unsigned int *bank_num,
					 unsigned int *bank_pin_num)
{
	for (*bank_num = 0; *bank_num < 4; (*bank_num)++)
		if (pin_num <= xgpiopss_pin_table[*bank_num])
			break;

	if (*bank_num == 0)
		*bank_pin_num = pin_num;
	else
		*bank_pin_num = pin_num %
					(xgpiopss_pin_table[*bank_num - 1] + 1);
}

/**
 * xgpiopss_set_bypass_mode - Set the GPIO pin in bypass mode
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function sets the specified pin of the GPIO device in bypass mode.
 */
void xgpiopss_set_bypass_mode(struct gpio_chip *chip, unsigned int pin)
{
	unsigned long flags;
	unsigned int bypm_reg, bank_num, bank_pin_num;
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);

	spin_lock_irqsave(&gpio->gpio_lock, flags);

	bypm_reg = xgpiopss_readreg(gpio->base_addr +
				    XGPIOPSS_BYPM_OFFSET(bank_num));
	bypm_reg |= 1 << bank_pin_num;
	xgpiopss_writereg(bypm_reg,
			  gpio->base_addr + XGPIOPSS_BYPM_OFFSET(bank_num));

	spin_unlock_irqrestore(&gpio->gpio_lock, flags);
}
EXPORT_SYMBOL(xgpiopss_set_bypass_mode);

/**
 * xgpiopss_set_normal_mode - Set the GPIO pin in normal mode
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function sets the specified pin of the GPIO device in normal (i,e)
 * software controlled mode.
 */
void xgpiopss_set_normal_mode(struct gpio_chip *chip, unsigned int pin)
{
	unsigned long flags;
	unsigned int bypm_reg, bank_num, bank_pin_num;
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);

	spin_lock_irqsave(&gpio->gpio_lock, flags);

	bypm_reg = xgpiopss_readreg(gpio->base_addr +
				    XGPIOPSS_BYPM_OFFSET(bank_num));
	bypm_reg &= ~(1 << bank_pin_num);
	xgpiopss_writereg(bypm_reg,
			  gpio->base_addr + XGPIOPSS_BYPM_OFFSET(bank_num));

	spin_unlock_irqrestore(&gpio->gpio_lock, flags);
}
EXPORT_SYMBOL(xgpiopss_set_normal_mode);

/**
 * xgpiopss_get_value - Get the state of the specified pin of GPIO device
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function reads the state of the specified pin of the GPIO device.
 * It returns 0 if the pin is low, 1 if pin is high.
 */
static int xgpiopss_get_value(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int bank_num, bank_pin_num;
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);

	return (xgpiopss_readreg(gpio->base_addr +
				 XGPIOPSS_DATA_OFFSET(bank_num)) >>
		bank_pin_num) & 1;
}

/**
 * xgpiopss_set_value - Modify the state of the pin with specified value
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value used to modify the state of the specified pin
 *
 * This function calculates the register offset (i.e to lower 16 bits or
 * upper 16 bits) based on the given pin number and sets the state of a
 * gpio pin to the specified value. The state is either 0 or 1.
 */
static void xgpiopss_set_value(struct gpio_chip *chip, unsigned int pin,
			       int state)
{
	unsigned long flags;
	unsigned int reg_offset;
	unsigned int bank_num, bank_pin_num;
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);

	if (bank_pin_num > 16) {
		bank_pin_num -= 16; /* only 16 data bits in bit maskable reg */
		reg_offset = XGPIOPSS_DATA_MSW_OFFSET(bank_num);
	} else
		reg_offset = XGPIOPSS_DATA_LSW_OFFSET(bank_num);

	/*
	 * get the 32 bit value to be written to the mask/data register where
	 * the upper 16 bits is the mask and lower 16 bits is the data
	 */
	state &= 0x01;
	state = ~(1 << (bank_pin_num + 16)) & ((state << bank_pin_num) |
					       0xFFFF0000);

	spin_lock_irqsave(&gpio->gpio_lock, flags);
	xgpiopss_writereg(state, gpio->base_addr + reg_offset);
	spin_unlock_irqrestore(&gpio->gpio_lock, flags);
}

/**
 * xgpiopss_dir_in - Set the direction of the specified GPIO pin as input
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function uses the read-modify-write sequence to set the direction of
 * the gpio pin as input. Returns 0 always.
 */
static int xgpiopss_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int reg, bank_num, bank_pin_num;
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);
	/* clear the bit in direction mode reg to set the pin as input */
	reg = xgpiopss_readreg(gpio->base_addr +
			       XGPIOPSS_DIRM_OFFSET(bank_num));
	reg &= ~(1 << bank_pin_num);
	xgpiopss_writereg(reg,
			  gpio->base_addr + XGPIOPSS_DIRM_OFFSET(bank_num));

	return 0;
}

/**
 * xgpiopss_dir_out - Set the direction of the specified GPIO pin as output
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value to be written to specified pin
 *
 * This function sets the direction of specified GPIO pin as output, configures
 * the Output Enable register for the pin and uses xgpiopss_set to set the state
 * of the pin to the value specified. Returns 0 always.
 */
static int xgpiopss_dir_out(struct gpio_chip *chip, unsigned int pin, int state)
{
	struct xgpiopss *gpio = container_of(chip, struct xgpiopss, chip);
	unsigned int reg, bank_num, bank_pin_num;

	xgpiopss_get_bank_pin(pin, &bank_num, &bank_pin_num);

	/* set the GPIO pin as output */
	reg = xgpiopss_readreg(gpio->base_addr +
			       XGPIOPSS_DIRM_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	xgpiopss_writereg(reg,
			  gpio->base_addr + XGPIOPSS_DIRM_OFFSET(bank_num));

	/* configure the output enable reg for the pin */
	reg = xgpiopss_readreg(gpio->base_addr +
			       XGPIOPSS_OUTEN_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	xgpiopss_writereg(reg,
			  gpio->base_addr + XGPIOPSS_OUTEN_OFFSET(bank_num));

	/* set the state of the pin */
	xgpiopss_set_value(chip, pin, state);
	return 0;
}

/**
 * xgpiopss_irq_ack - Acknowledge the interrupt of a gpio pin
 * @irq:	irq number of gpio pin for which interrupt is to be ACKed
 *
 * This function calculates gpio pin number from irq number and sets the bit
 * in the Interrupt Status Register of the corresponding bank, to ACK the irq.
 */
static void xgpiopss_irq_ack(unsigned int irq)
{
	struct xgpiopss *gpio = (struct xgpiopss *)get_irq_chip_data(irq);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int irq_sts;

	device_pin_num = irq_to_gpio(irq); /* get pin num within the device */
	xgpiopss_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	irq_sts = xgpiopss_readreg(gpio->base_addr +
				   XGPIOPSS_INTSTS_OFFSET(bank_num)) |
				   (1 << bank_pin_num);
	xgpiopss_writereg(irq_sts,
			  gpio->base_addr + (XGPIOPSS_INTSTS_OFFSET(bank_num)));
}

/**
 * xgpiopss_irq_mask - Disable the interrupts for a gpio pin
 * @irq:	irq number of gpio pin for which interrupt is to be disabled
 *
 * This function calculates gpio pin number from irq number and sets the
 * bit in the Interrupt Disable register of the corresponding bank to disable
 * interrupts for that pin.
 */
static void xgpiopss_irq_mask(unsigned int irq)
{
	struct xgpiopss *gpio = (struct xgpiopss *)get_irq_chip_data(irq);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int irq_dis;

	device_pin_num = irq_to_gpio(irq); /* get pin num within the device */
	xgpiopss_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	irq_dis = xgpiopss_readreg(gpio->base_addr +
				   XGPIOPSS_INTDIS_OFFSET(bank_num)) |
				   (1 << bank_pin_num);
	xgpiopss_writereg(irq_dis,
			  gpio->base_addr + XGPIOPSS_INTDIS_OFFSET(bank_num));
}

/**
 * xgpiopss_irq_unmask - Enable the interrupts for a gpio pin
 * @irq:	irq number of gpio pin for which interrupt is to be enabled
 *
 * This function calculates the gpio pin number from irq number and sets the
 * bit in the Interrupt Enable register of the corresponding bank to enable
 * interrupts for that pin.
 */
static void xgpiopss_irq_unmask(unsigned int irq)
{
	struct xgpiopss *gpio = (struct xgpiopss *)get_irq_chip_data(irq);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int irq_en;

	device_pin_num = irq_to_gpio(irq); /* get pin num within the device */
	xgpiopss_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	irq_en = xgpiopss_readreg(gpio->base_addr +
				  XGPIOPSS_INTEN_OFFSET(bank_num)) |
				  (1 << bank_pin_num);
	xgpiopss_writereg(irq_en,
			  gpio->base_addr + XGPIOPSS_INTEN_OFFSET(bank_num));
}

/**
 * xgpiopss_set_irq_type - Set the irq type for a gpio pin
 * @irq:	irq number of gpio pin for which interrupt type is to be set
 * @type:	interrupt type that is to be set for the gpio pin
 *
 * This function gets the gpio pin number and its bank from the gpio pin number
 * and configures the INT_TYPE, INT_POLARITY and INT_ANY registers. Returns 0,
 * negative error otherwise.
 * TYPE-EDGE_RISING,  INT_TYPE - 1, INT_POLARITY - 1,  INT_ANY - 0;
 * TYPE-EDGE_FALLING, INT_TYPE - 1, INT_POLARITY - 0,  INT_ANY - 0;
 * TYPE-EDGE_BOTH,    INT_TYPE - 1, INT_POLARITY - NA, INT_ANY - 1;
 * TYPE-LEVEL_HIGH,   INT_TYPE - 0, INT_POLARITY - 1,  INT_ANY - NA;
 * TYPE-LEVEL_LOW,    INT_TYPE - 0, INT_POLARITY - 0,  INT_ANY - NA
 */
static int xgpiopss_set_irq_type(unsigned int irq, unsigned int type)
{
	struct xgpiopss *gpio = (struct xgpiopss *)get_irq_chip_data(irq);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int int_type, int_pol, int_any;

	device_pin_num = irq_to_gpio(irq); /* get pin num within the device */
	xgpiopss_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);

	int_type = xgpiopss_readreg(gpio->base_addr +
				    XGPIOPSS_INTTYPE_OFFSET(bank_num));
	int_pol = xgpiopss_readreg(gpio->base_addr +
				   XGPIOPSS_INTPOL_OFFSET(bank_num));
	int_any = xgpiopss_readreg(gpio->base_addr +
				   XGPIOPSS_INTANY_OFFSET(bank_num));

	/*
	 * based on the type requested, configure the INT_TYPE, INT_POLARITY
	 * and INT_ANY registers
	 */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		int_type |= (1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_type |= (1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		int_type |= (1 << bank_pin_num);
		int_any |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_type &= ~(1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_type &= ~(1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		break;
	default:
		return -EINVAL;
	}

	xgpiopss_writereg(int_type,
			  gpio->base_addr + XGPIOPSS_INTTYPE_OFFSET(bank_num));
	xgpiopss_writereg(int_pol,
			  gpio->base_addr + XGPIOPSS_INTPOL_OFFSET(bank_num));
	xgpiopss_writereg(int_any,
			  gpio->base_addr + XGPIOPSS_INTANY_OFFSET(bank_num));
	return 0;
}

/* irq chip descriptor */
static struct irq_chip xgpiopss_irqchip = {
	.name		= DRIVER_NAME,
	.ack		= xgpiopss_irq_ack,
	.mask		= xgpiopss_irq_mask,
	.unmask		= xgpiopss_irq_unmask,
	.set_type	= xgpiopss_set_irq_type,
};

/**
 * xgpiopss_irqhandler - IRQ handler for the gpio banks of a gpio device
 * @irq:	irq number of the gpio bank where interrupt has occurred
 * @desc:	irq descriptor instance of the 'irq'
 *
 * This function reads the Interrupt Status Register of each bank to get the
 * gpio pin number which has triggered an interrupt. It then acks the triggered
 * interrupt and calls the pin specific handler set by the higher layer
 * application for that pin.
 * Note: A bug is reported if no handler is set for the gpio pin.
 */
void xgpiopss_irqhandler(unsigned int irq, struct irq_desc *desc)
{
	int gpio_irq = (int) get_irq_data(irq);
	struct xgpiopss *gpio = (struct xgpiopss *)get_irq_chip_data(gpio_irq);
	unsigned int int_sts, int_enb, bank_num;
	struct irq_desc *gpio_irq_desc;

	desc->chip->ack(irq);
	for (bank_num = 0; bank_num < 4; bank_num++) {
		int_sts = xgpiopss_readreg(gpio->base_addr +
					   XGPIOPSS_INTSTS_OFFSET(bank_num));
		int_enb = xgpiopss_readreg(gpio->base_addr +
					   XGPIOPSS_INTMASK_OFFSET(bank_num));
		/*
		 * handle only the interrupts which are enabled in interrupt
		 * mask register
		 */
		int_sts &= ~int_enb;
		for (; int_sts != 0; int_sts >>= 1, gpio_irq++) {
			if ((int_sts & 1) == 0)
				continue;
			BUG_ON(!(irq_desc[gpio_irq].handle_irq));
			gpio_irq_desc = irq_to_desc(gpio_irq);
			gpio_irq_desc->chip->ack(gpio_irq);

			/* call the pin specific handler */
			irq_desc[gpio_irq].handle_irq(gpio_irq,
						      &irq_desc[gpio_irq]);
		}
		/* shift to first virtual irq of next bank */
		gpio_irq = (int) get_irq_data(irq) +
				(xgpiopss_pin_table[bank_num] + 1);
	}
	desc->chip->unmask(irq);
}

/**
 * xgpiopss_probe - Initialization method for a xgpiopss device
 * @pdev:	platform device instance
 *
 * This function allocates memory resources for the gpio device and registers
 * all the banks of the device. It will also set up interrupts for the gpio
 * pins.
 * Note: Interrupts are disabled for all the banks during initialization.
 * Returns 0 on success, negative error otherwise.
 */
static int __init xgpiopss_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int irq_num;
	struct xgpiopss *gpio;
	struct gpio_chip *chip;
	resource_size_t remap_size;
	struct resource *mem_res = NULL;
	int pin_num, bank_num, gpio_irq;

	gpio = kzalloc(sizeof(struct xgpiopss), GFP_KERNEL);
	if (!gpio) {
		dev_err(&pdev->dev, "couldn't allocate memory for gpio private "
			"data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, gpio);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_free_gpio;
	}

	remap_size = mem_res->end - mem_res->start + 1;
	if (!request_mem_region(mem_res->start, remap_size, pdev->name)) {
		dev_err(&pdev->dev, "Cannot request IO\n");
		ret = -ENXIO;
		goto err_free_gpio;
	}

	gpio->base_addr = ioremap(mem_res->start, remap_size);
	if (gpio->base_addr == NULL) {
		dev_err(&pdev->dev, "Couldn't ioremap memory at 0x%08lx\n",
			(unsigned long)mem_res->start);
		ret = -ENOMEM;
		goto err_release_region;
	}

	irq_num = platform_get_irq(pdev, 0);

	/* configure the gpio chip */
	chip = &gpio->chip;
	chip->label = "xgpiopss";
	chip->owner = THIS_MODULE;
	chip->dev = &pdev->dev;
	chip->get = xgpiopss_get_value;
	chip->set = xgpiopss_set_value;
	chip->direction_input = xgpiopss_dir_in;
	chip->direction_output = xgpiopss_dir_out;
	chip->dbg_show = NULL;
	chip->base = 0;		/* default pin base */
	chip->ngpio = ARCH_NR_GPIOS;
	chip->can_sleep = 0;

	/* report a bug if gpio chip registration fails */
	ret = gpiochip_add(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio chip registration failed\n");
		goto err_iounmap;
	} else
		dev_info(&pdev->dev, "gpio at 0x%08lx mapped to 0x%08lx\n",
			 (unsigned long)mem_res->start,
			 (unsigned long)gpio->base_addr);

	/* disable interrupts for all banks */
	for (bank_num = 0; bank_num < 4; bank_num++) {
		xgpiopss_writereg(0xffffffff, gpio->base_addr +
				  XGPIOPSS_INTDIS_OFFSET(bank_num));
	}

	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	gpio_irq = XGPIOPSS_IRQBASE;
	for (pin_num = 0; pin_num < ARCH_NR_GPIOS; pin_num++, gpio_irq++) {
		set_irq_chip(gpio_irq, &xgpiopss_irqchip);
		set_irq_chip_data(gpio_irq, (void *)gpio);
		set_irq_handler(gpio_irq, handle_simple_irq);
		set_irq_flags(gpio_irq, IRQF_VALID);
	}

	set_irq_data(irq_num, (void *)(XGPIOPSS_IRQBASE));
	set_irq_chained_handler(irq_num, xgpiopss_irqhandler);

	return 0;

err_iounmap:
	iounmap(gpio->base_addr);
err_release_region:
	release_mem_region(mem_res->start, remap_size);
err_free_gpio:
	platform_set_drvdata(pdev, NULL);
	kfree(gpio);

	return ret;
}

static struct platform_driver xgpiopss_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= xgpiopss_probe,
};

/**
 * xgpiopss_init - Initial driver registration call
 */
static int __init xgpiopss_init(void)
{
	return platform_driver_register(&xgpiopss_driver);
}

subsys_initcall(xgpiopss_init);
