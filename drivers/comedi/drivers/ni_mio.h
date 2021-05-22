#include <linux/interrupt.h>

#define NI_GPCT_SUBDEV(x)	(NI_GPCT0_SUBDEV + (x))

enum ni_common_subdevices {
	NI_AI_SUBDEV,
	NI_AO_SUBDEV,
	NI_DIO_SUBDEV,
	NI_8255_DIO_SUBDEV,
	NI_UNUSED_SUBDEV,
	NI_CALIBRATION_SUBDEV,
	NI_EEPROM_SUBDEV,
	NI_PFI_DIO_SUBDEV,
	NI_CS5529_CALIBRATION_SUBDEV,
	NI_SERIAL_SUBDEV,
	NI_RTSI_SUBDEV,
	NI_GPCT0_SUBDEV,
	NI_GPCT1_SUBDEV,
	NI_FREQ_OUT_SUBDEV,
	NI_NUM_SUBDEVICES
};

extern void ni_writel(struct comedi_device *dev, unsigned int data, int reg);
extern void ni_writew(struct comedi_device *dev, unsigned int data, int reg);
extern void ni_writeb(struct comedi_device *dev, unsigned int data, int reg);
extern unsigned int ni_readb(struct comedi_device *dev, int reg);
extern void ni_stc_writew(struct comedi_device *dev, unsigned int data, int reg);
extern int ni_read_eeprom(struct comedi_device *dev, int addr);
extern irqreturn_t ni_E_interrupt(int irq, void *d);
extern int ni_alloc_private(struct comedi_device *dev);
extern int ni_E_init(struct comedi_device *dev,
		     unsigned int interrupt_pin, unsigned int irq_polarity);
extern void mio_common_detach(struct comedi_device *dev);

extern const struct comedi_lrange range_ni_E_ao_ext;

MODULE_IMPORT_NS(COMEDI_NI);
