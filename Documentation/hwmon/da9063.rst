Kernel driver da9063-hwmon
==========================

Supported chips:

  * Dialog Semiconductor DA9063 PMIC

    Prefix: 'da9063'

    Datasheet: Publicly available at the Dialog Semiconductor website:

	http://www.dialog-semiconductor.com/products/power-management/DA9063

Authors:
	- S Twiss <stwiss.opensource@diasemi.com>
	- Vincent Pelletier <plr.vincent@gmail.com>

Description
-----------

The DA9063 PMIC provides a general purpose ADC with 10 bits of resolution.
It uses track and hold circuitry with an analogue input multiplexer which
allows the conversion of up to 9 different inputs:

- Channel  0: VSYS_RES	measurement of the system VDD (2.5 - 5.5V)
- Channel  1: ADCIN1_RES	high impedance input (0 - 2.5V)
- Channel  2: ADCIN2_RES	high impedance input (0 - 2.5V)
- Channel  3: ADCIN3_RES	high impedance input (0 - 2.5V)
- Channel  4: Tjunc	measurement of internal temperature sensor
- Channel  5: VBBAT	measurement of the backup battery voltage (0 - 5.0V)
- Channel  6: N/A	Reserved
- Channel  7: N/A	Reserved
- Channel  8: MON1_RES	group 1 internal regulators voltage (0 - 5.0V)
- Channel  9: MON2_RES	group 2 internal regulators voltage (0 - 5.0V)
- Channel 10: MON3_RES	group 3 internal regulators voltage (0 - 5.0V)

The MUX selects from and isolates the 9 inputs and presents the channel to
be measured to the ADC input. When selected, an input amplifier on the VSYS
channel subtracts the VDDCORE reference voltage and scales the signal to the
correct value for the ADC.

The analog ADC includes current sources at ADC_IN1, ADC_IN2 and ADC_IN3 to
support resistive measurements.

Channels 1, 2 and 3 current source capability can be set through the ADC
thresholds ADC_CFG register and values for ADCIN1_CUR, ADCIN2_CUR and
ADCIN3_CUR. Settings for ADCIN1_CUR and ADCIN2_CUR are 1.0, 2.0, 10 and
40 micro Amps. The setting for ADCIN3_CUR is 10 micro Amps.

Voltage Monitoring
------------------

The manual measurement allows monitoring of the system voltage VSYS, the
auxiliary channels ADCIN1, ADCIN2 and ADCIN3, and a VBBAT measurement of
the backup battery voltage (0 - 5.0V). The manual measurements store 10
bits of ADC resolution.

The manual ADC measurements attributes described above are supported by
the driver.

The automatic ADC measurement is not supported by the driver.

Temperature Monitoring
----------------------

Temperatures are sampled by a 10 bit ADC.  Junction temperatures
are monitored by the ADC channels.

The junction temperature is calculated:

	Degrees celsius = -0.398 * (ADC_RES - T_OFFSET) + 330;

The junction temperature attribute is supported by the driver.
