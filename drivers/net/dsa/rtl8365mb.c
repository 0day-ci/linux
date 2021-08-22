// SPDX-License-Identifier: GPL-2.0
/* Realtek SMI subdriver for the Realtek RTL8365MB-VC Ethernet switch.
 *
 * Copyright (C) 2021 Alvin Šipraga <alsi@bang-olufsen.dk>
 * Copyright (C) 2021 Michael Rasmussen <mir@bang-olufsen.dk>
 *
 * The RTL8365MB-VC is a 4+1 port 10/100/1000M switch controller. It includes 4
 * integrated PHYs for the user facing ports, and an extension interface which
 * can be connected to the CPU - or another PHY - via either MII, RMII, or
 * RGMII. The switch is configured via the Realtek Simple Management Interface
 * (SMI), which uses the MDIO/MDC lines.
 *
 * Below is a simplified block diagram of the chip and its relevant interfaces.
 *
 *                          .-----------------------------------.
 *                          |                                   |
 *         UTP <---------------> Giga PHY <-> PCS <-> P0 GMAC   |
 *         UTP <---------------> Giga PHY <-> PCS <-> P1 GMAC   |
 *         UTP <---------------> Giga PHY <-> PCS <-> P2 GMAC   |
 *         UTP <---------------> Giga PHY <-> PCS <-> P3 GMAC   |
 *                          |                                   |
 *     CPU/PHY <-MII/RMII/RGMII--->  Extension  <---> Extension |
 *                          |       interface 1        GMAC 1   |
 *                          |                                   |
 *     SMI driver/ <-MDC/SCL---> Management    ~~~~~~~~~~~~~~   |
 *        EEPROM   <-MDIO/SDA--> interface     ~REALTEK ~~~~~   |
 *                          |                  ~RTL8365MB ~~~   |
 *                          |                  ~GXXXC TAIWAN~   |
 *        GPIO <--------------> Reset          ~~~~~~~~~~~~~~   |
 *                          |                                   |
 *      Interrupt  <----------> Link UP/DOWN events             |
 *      controller          |                                   |
 *                          '-----------------------------------'
 *
 * The driver uses DSA to integrate the 4 user and 1 extension ports into the
 * kernel. Netdevices are created for the user ports, as are PHY devices for
 * their integrated PHYs. The device tree firmware should also specify the link
 * partner of the extension port - either via a fixed-link or other phy-handle.
 * See the device tree bindings for more detailed information. Note that the
 * driver has only been tested with a fixed-link, but in principle it should not
 * matter.
 *
 * NOTE: Currently, only the RGMII interface is implemented in this driver.
 *
 * The interrupt line is asserted on link UP/DOWN events. The driver creates a
 * custom irqchip to handle this interrupt and demultiplex the events by reading
 * the status registers via SMI. Interrupts are then propagated to the relevant
 * PHY device.
 *
 * The EEPROM contains initial register values which the chip will read over I2C
 * upon hardware reset. It is also possible to omit the EEPROM. In both cases,
 * the driver will manually reprogram some registers using jam tables to reach
 * an initial state defined by the vendor driver.
 *
 * This Linux driver is written based on an OS-agnostic vendor driver from
 * Realtek. The reference GPL-licensed sources can be found in the OpenWrt
 * source tree under the name rtl8367c. The vendor driver claims to support a
 * number of similar switch controllers from Realtek, but the only hardware we
 * have is the RTL8365MB-VC. Moreover, there does not seem to be any chip under
 * the name RTL8367C. Although one wishes that the 'C' stood for some kind of
 * common hardware revision, there exist examples of chips with the suffix -VC
 * which are explicitly not supported by the rtl8367c driver and which instead
 * require the rtl8367d vendor driver. With all this uncertainty, the driver has
 * been modestly named rtl8365mb. Future implementors may wish to rename things
 * accordingly.
 *
 * In the same family of chips, some carry up to 8 user ports and up to 2
 * extension ports. Where possible this driver tries to make things generic, but
 * more work must be done to support these configurations. According to
 * documentation from Realtek, the family should include the following chips:
 *
 *  - RTL8363NB
 *  - RTL8363NB-VB
 *  - RTL8363SC
 *  - RTL8363SC-VB
 *  - RTL8364NB
 *  - RTL8364NB-VB
 *  - RTL8365MB-VC
 *  - RTL8366SC
 *  - RTL8367RB-VB
 *  - RTL8367SB
 *  - RTL8367S
 *  - RTL8370MB
 *  - RTL8310SR
 *
 * Some of the register logic for these additional chips has been skipped over
 * while implementing this driver. It is therefore not possible to assume that
 * things will work out-of-the-box for other chips, and a careful review of the
 * vendor driver may be needed to expand support. The RTL8365MB-VC seems to be
 * one of the simpler chips.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include "realtek-smi-core.h"

/* Port mapping macros
 *
 * PORT_NUM_x2y: map a port number from domain x to domain y
 * PORT_MASK_x2y: map a port mask from domain x to domain y
 *
 * L = logical port domain, i.e. dsa_port.index
 * P = physical port domain, used by the Realtek ASIC for port indexing;
 *     for ports with internal PHYs, this is also the PHY index
 * E = extension port domain, used by the Realtek ASIC for managing EXT ports
 *
 * The terminology is borrowed from the vendor driver. The extension port domain
 * is mostly used to navigate the labyrinthine layout of EXT port configuration
 * registers and is not considered intuitive by the author.
 *
 * Unless a function is accessing chip registers, it should be using the logical
 * port domain. Moreover, function arguments for port numbers and port masks
 * must always be in the logical domain. The conversion must be done as close as
 * possible to the register access to avoid chaos.
 *
 * The mappings vary between chips in the family supported by this driver. Here
 * is an example of the mapping for the RTL8365MB-VC:
 *
 *    L | P | E | remark
 *   ---+---+---+--------
 *    0 | 0 |   | user port
 *    1 | 1 |   | user port
 *    2 | 2 |   | user port
 *    3 | 3 |   | user port
 *    4 | 6 | 1 | extension (CPU) port
 *
 * NOTE: Currently this is hardcoded for the RTL8365MB-VC. This will probably
 * require a rework when adding support for other chips.
 */
#define CPU_PORT_LOGICAL_NUM	4
#define CPU_PORT_LOGICAL_MASK	BIT(CPU_PORT_LOGICAL_NUM)
#define CPU_PORT_PHYSICAL_NUM	6
#define CPU_PORT_PHYSICAL_MASK	BIT(CPU_PORT_PHYSICAL_NUM)
#define CPU_PORT_EXTENSION_NUM	1

#define PORT_NUM_L2P(_p)  \
		((_p) == CPU_PORT_LOGICAL_NUM ? CPU_PORT_PHYSICAL_NUM : (_p))
#define PORT_NUM_P2L(_p)  \
		((_p) == CPU_PORT_PHYSICAL_NUM ? CPU_PORT_LOGICAL_NUM : (_p))
#define PORT_NUM_L2E(_p) (CPU_PORT_EXTENSION_NUM)

#define PORT_MASK_L2P(_m)                          \
		(((_m) & ~CPU_PORT_LOGICAL_MASK) | \
		 (((_m) & CPU_PORT_LOGICAL_MASK) ? CPU_PORT_PHYSICAL_MASK : 0))
#define PORT_MASK_P2L(_m)                           \
		(((_m) & ~CPU_PORT_PHYSICAL_MASK) | \
		 (((_m) & CPU_PORT_PHYSICAL_MASK) ? CPU_PORT_LOGICAL_MASK : 0))

/* Chip-specific data and limits */
#define RTL8365MB_CHIP_ID_8365MB_VC		0x6367
#define RTL8365MB_NUM_PORTS_8365MB_VC		5
#define RTL8365MB_NUM_PHYS_8365MB_VC		4
#define RTL8365MB_CPU_PORT_NUM_8365MB_VC	4
#define RTL8365MB_CPU_PORT_MASK_8365MB_VC       BIT(4)
#define RTL8365MB_PHY_PORT_MASK_8365MB_VC	GENMASK(3, 0)
#define RTL8365MB_PORT_MASK_8365MB_VC                \
		(RTL8365MB_CPU_PORT_MASK_8365MB_VC | \
		 RTL8365MB_PHY_PORT_MASK_8365MB_VC)

/* Family-specific data and limits */
#define RTL8365MB_NUM_PHYREGS	32
#define RTL8365MB_PHYREGMAX	(RTL8365MB_NUM_PHYREGS - 1)
#define RTL8365MB_NUM_VLANS	32
#define RTL8365MB_NUM_VIDS	4096
#define RTL8365MB_VIDMAX	(RTL8365MB_NUM_VIDS - 1)
#define RTL8365MB_NUM_EVIDS	8192
#define RTL8365MB_EVIDMAX	(RTL8365MB_NUM_EVIDS - 1)
#define RTL8365MB_PRIORITYMAX	7
#define RTL8365MB_FIDMAX	15
#define RTL8365MB_NUM_METERS	64
#define RTL8365MB_METERMAX	(RTL8365MB_NUM_METERS - 1)

/* Chip identification registers */
#define RTL8365MB_CHIP_ID_REG		0x1300

#define RTL8365MB_CHIP_VER_REG		0x1301

#define RTL8365MB_MAGIC_REG		0x13C2
#define   RTL8365MB_MAGIC_VALUE		0x0249

/* Chip reset register */
#define RTL8365MB_CHIP_RESET_REG	0x1322
#define RTL8365MB_CHIP_RESET_SW_MASK	0x0002
#define RTL8365MB_CHIP_RESET_HW_MASK	0x0001

/* Interrupt polarity register */
#define RTL8365MB_INTR_POLARITY_REG	0x1100
#define   RTL8365MB_INTR_POLARITY_MASK	0x0001
#define   RTL8365MB_INTR_POLARITY_HIGH	0
#define   RTL8365MB_INTR_POLARITY_LOW	1

/* Interrupt control register - enable or disable specific interrupt types */
#define RTL8365MB_INTR_CTRL				0x1101
#define   RTL8365MB_INTR_CTRL_SLIENT_START_2_MASK	0x1000
#define   RTL8365MB_INTR_CTRL_SLIENT_START_MASK		0x800
#define   RTL8365MB_INTR_CTRL_ACL_ACTION_MASK		0x200
#define   RTL8365MB_INTR_CTRL_CABLE_DIAG_FIN_MASK	0x100
#define   RTL8365MB_INTR_CTRL_INTERRUPT_8051_MASK	0x80
#define   RTL8365MB_INTR_CTRL_LOOP_DETECTION_MASK	0x40
#define   RTL8365MB_INTR_CTRL_GREEN_TIMER_MASK		0x20
#define   RTL8365MB_INTR_CTRL_SPECIAL_CONGEST_MASK	0x10
#define   RTL8365MB_INTR_CTRL_SPEED_CHANGE_MASK		0x8
#define   RTL8365MB_INTR_CTRL_LEARN_OVER_MASK		0x4
#define   RTL8365MB_INTR_CTRL_METER_EXCEEDED_MASK	0x2
#define   RTL8365MB_INTR_CTRL_LINK_CHANGE_MASK		0x1


/* Interrupt status register */
#define RTL8365MB_INTR_STATUS_REG			0x1102
#define   RTL8365MB_INTR_STATUS_SLIENT_START_2_MASK	0x1000
#define   RTL8365MB_INTR_STATUS_SLIENT_START_MASK	0x800
#define   RTL8365MB_INTR_STATUS_ACL_ACTION_MASK		0x200
#define   RTL8365MB_INTR_STATUS_CABLE_DIAG_FIN_MASK	0x100
#define   RTL8365MB_INTR_STATUS_INTERRUPT_8051_MASK	0x80
#define   RTL8365MB_INTR_STATUS_LOOP_DETECTION_MASK	0x40
#define   RTL8365MB_INTR_STATUS_GREEN_TIMER_MASK	0x20
#define   RTL8365MB_INTR_STATUS_SPECIAL_CONGEST_MASK	0x10
#define   RTL8365MB_INTR_STATUS_SPEED_CHANGE_MASK	0x8
#define   RTL8365MB_INTR_STATUS_LEARN_OVER_MASK		0x4
#define   RTL8365MB_INTR_STATUS_METER_EXCEEDED_MASK	0x2
#define   RTL8365MB_INTR_STATUS_LINK_CHANGE_MASK	0x1
#define   RTL8365MB_INTR_STATUS_ALL_MASK                      \
		(RTL8365MB_INTR_STATUS_SLIENT_START_2_MASK |  \
		 RTL8365MB_INTR_STATUS_SLIENT_START_MASK |    \
		 RTL8365MB_INTR_STATUS_ACL_ACTION_MASK |      \
		 RTL8365MB_INTR_STATUS_CABLE_DIAG_FIN_MASK |  \
		 RTL8365MB_INTR_STATUS_INTERRUPT_8051_MASK |  \
		 RTL8365MB_INTR_STATUS_LOOP_DETECTION_MASK |  \
		 RTL8365MB_INTR_STATUS_GREEN_TIMER_MASK |     \
		 RTL8365MB_INTR_STATUS_SPECIAL_CONGEST_MASK | \
		 RTL8365MB_INTR_STATUS_SPEED_CHANGE_MASK |    \
		 RTL8365MB_INTR_STATUS_LEARN_OVER_MASK |      \
		 RTL8365MB_INTR_STATUS_METER_EXCEEDED_MASK |  \
		 RTL8365MB_INTR_STATUS_LINK_CHANGE_MASK)

/* Per-port interrupt type status registers */
#define RTL8365MB_PORT_LINKDOWN_IND_REG		0x1106
#define   RTL8365MB_PORT_LINKDOWN_IND_MASK	0x7FF

#define RTL8365MB_PORT_LINKUP_IND_REG		0x1107
#define   RTL8365MB_PORT_LINKUP_IND_MASK	0x7FF

/* PHY indirect access registers */
#define RTL8365MB_INDIRECT_ACCESS_CTRL_REG			0x1F00
#define   RTL8365MB_INDIRECT_ACCESS_CTRL_RW_MASK		0x0002
#define   RTL8365MB_INDIRECT_ACCESS_CTRL_RW_READ		0
#define   RTL8365MB_INDIRECT_ACCESS_CTRL_RW_WRITE		1
#define   RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_MASK		0x1
#define   RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_VALUE		1
#define RTL8365MB_INDIRECT_ACCESS_STATUS_REG			0x1F01
#define RTL8365MB_INDIRECT_ACCESS_ADDRESS_REG			0x1F02
#define   RTL8365MB_INDIRECT_ACCESS_ADDRESS_OCPADR_5_1_MASK	GENMASK(4, 0)
#define   RTL8365MB_INDIRECT_ACCESS_ADDRESS_PHYNUM_MASK		GENMASK(6, 5)
#define   RTL8365MB_INDIRECT_ACCESS_ADDRESS_OCPADR_9_6_MASK	GENMASK(11, 8)
#define   RTL8365MB_PHY_BASE					0x2000
#define RTL8365MB_INDIRECT_ACCESS_WRITE_DATA_REG		0x1F03
#define RTL8365MB_INDIRECT_ACCESS_READ_DATA_REG			0x1F04

/* PHY OCP address prefix register */
#define RTL8365MB_GPHY_OCP_MSB_0_REG			0x1D15
#define   RTL8365MB_GPHY_OCP_MSB_0_CFG_CPU_OCPADR_MASK	0x0FC0
#define RTL8365MB_PHY_OCP_ADDR_PREFIX_MASK		0xFC00

/* The PHY OCP addresses of PHY registers 0~31 start here */
#define RTL8365MB_PHY_OCP_ADDR_PHYREG_BASE		0xA400

/* EXT port interface mode values - used in DIGITAL_INTERFACE_SELECT */
#define RTL8365MB_EXT_PORT_MODE_DISABLE		0
#define RTL8365MB_EXT_PORT_MODE_RGMII		1
#define RTL8365MB_EXT_PORT_MODE_MII_MAC		2
#define RTL8365MB_EXT_PORT_MODE_MII_PHY		3
#define RTL8365MB_EXT_PORT_MODE_TMII_MAC	4
#define RTL8365MB_EXT_PORT_MODE_TMII_PHY	5
#define RTL8365MB_EXT_PORT_MODE_GMII		6
#define RTL8365MB_EXT_PORT_MODE_RMII_MAC	7
#define RTL8365MB_EXT_PORT_MODE_RMII_PHY	8
#define RTL8365MB_EXT_PORT_MODE_SGMII		9
#define RTL8365MB_EXT_PORT_MODE_HSGMII		10
#define RTL8365MB_EXT_PORT_MODE_1000X_100FX	11
#define RTL8365MB_EXT_PORT_MODE_1000X		12
#define RTL8365MB_EXT_PORT_MODE_100FX		13

/* EXT port interface mode configuration registers 0~1 */
#define RTL8365MB_DIGITAL_INTERFACE_SELECT_REG0		0x1305
#define RTL8365MB_DIGITAL_INTERFACE_SELECT_REG1		0x13C3
#define RTL8365MB_DIGITAL_INTERFACE_SELECT_REG(_extport)   \
		(RTL8365MB_DIGITAL_INTERFACE_SELECT_REG0 + \
		 ((_extport) >> 1) * (0x13C3 - 0x1305))
#define   RTL8365MB_DIGITAL_INTERFACE_SELECT_MODE_MASK(_extport) \
		(0xF << (((_extport) % 2)))
#define   RTL8365MB_DIGITAL_INTERFACE_SELECT_MODE_OFFSET(_extport) \
		(((_extport) % 2) * 4)

/* EXT port RGMII TX/RX delay configuration registers 1~2 */
#define RTL8365MB_EXT_RGMXF_REG1		0x1307
#define RTL8365MB_EXT_RGMXF_REG2		0x13C5
#define RTL8365MB_EXT_RGMXF_REG(_extport)   \
		(RTL8365MB_EXT_RGMXF_REG1 + \
		 (((_extport) >> 1) * (0x13C5 - 0x1307)))
#define   RTL8365MB_EXT_RGMXF_RXDELAY_MASK	0x0007
#define   RTL8365MB_EXT_RGMXF_TXDELAY_MASK	0x0008

/* External port speed values - used in DIGITAL_INTERFACE_FORCE */
#define RTL8365MB_PORT_SPEED_10M	0
#define RTL8365MB_PORT_SPEED_100M	1
#define RTL8365MB_PORT_SPEED_1000M	2

/* EXT port force configuration registers 0~2 */
#define RTL8365MB_DIGITAL_INTERFACE_FORCE_REG0			0x1310
#define RTL8365MB_DIGITAL_INTERFACE_FORCE_REG1			0x1311
#define RTL8365MB_DIGITAL_INTERFACE_FORCE_REG2			0x13c4
#define RTL8365MB_DIGITAL_INTERFACE_FORCE_REG(_extport)   \
		(RTL8365MB_DIGITAL_INTERFACE_FORCE_REG0 + \
		 ((_extport) & 0x1) +                     \
		 ((((_extport) >> 1) & 0x1) * (0x13C4 - 0x1310)))
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_EN_MASK		0x1000
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_NWAY_MASK		0x0080
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_TXPAUSE_MASK	0x0040
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_RXPAUSE_MASK	0x0020
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_LINK_MASK		0x0010
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_DUPLEX_MASK		0x0004
#define   RTL8365MB_DIGITAL_INTERFACE_FORCE_SPEED_MASK		0x0003

/* CPU port mask register - controls which ports are treated as CPU ports */
#define RTL8365MB_CPU_PORT_MASK_REG	0x1219
#define   RTL8365MB_CPU_PORT_MASK_MASK	0x07FF

/* CPU control register */
#define RTL8365MB_CPU_CTRL_REG			0x121a
#define   RTL8365MB_CPU_CTRL_TRAP_PORT_EXT_MASK	0x400
#define   RTL8365MB_CPU_CTRL_TAG_FORMAT_MASK	0x200
#define   RTL8365MB_CPU_CTRL_RXBYTECOUNT_MASK	0x80
#define   RTL8365MB_CPU_CTRL_TAG_POSITION_MASK	0x40
#define   RTL8365MB_CPU_CTRL_TRAP_PORT_MASK	0x38
#define   RTL8365MB_CPU_CTRL_INSERTMODE_MASK	0x6
#define   RTL8365MB_CPU_CTRL_EN_MASK		0x1

/* Maximum packet length register */
#define RTL8365MB_CFG0_MAX_LEN_REG	0x088c
#define   RTL8365MB_CFG0_MAX_LEN_MASK	0x3FFF

/* Table read/write registers */
#define RTL8365MB_TABLE_READ_BASE	0x0520
#define RTL8365MB_TABLE_READ_REG(_x) \
		(RTL8365MB_TABLE_READ_BASE + (_x))
#define RTL8365MB_TABLE_WRITE_BASE	0x0510
#define RTL8365MB_TABLE_WRITE_REG(_x) \
		(RTL8365MB_TABLE_WRITE_BASE + (_x))
/* The generic table size is actually 10, but the uppermost table register has
 * space for only half a word (1 byte). There doesn't seem to be any table with
 * such large entries, so cap it at 9 to simplify the read/write logic below.
 */
#define RTL8365MB_TABLE_ENTRY_SIZE	9

/* Table access control register
 * NOTE: Some tables seem to support alternative access methods. For now the
 * method field is not used by rtl8365mb_table_{read,write}.
 */
#define RTL8365MB_TABLE_ACCESS_CTRL_REG				0x0500
#define   RTL8365MB_TABLE_ACCESS_CTRL_TARGET_MASK		0x0007
#define     RTL8365MB_TABLE_ACCESS_CTRL_TARGET_ACLRULE		1
#define     RTL8365MB_TABLE_ACCESS_CTRL_TARGET_ACLACT		2
#define     RTL8365MB_TABLE_ACCESS_CTRL_TARGET_CVLAN		3
#define     RTL8365MB_TABLE_ACCESS_CTRL_TARGET_L2		4
#define     RTL8365MB_TABLE_ACCESS_CTRL_TARGET_IGMP_GROUP	5
#define   RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_MASK		0x0008
#define     RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_READ		0
#define     RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_WRITE		1
#define   RTL8365MB_TABLE_ACCESS_CTRL_METHOD_MASK		0x0070

/* Table access address register */
#define RTL8365MB_TABLE_ACCESS_ADDR_REG			0x0501
#define   RTL8365MB_TABLE_ACCESS_ADDR_MASK		0x1FFF

/* Table LUT (look-up-table) address register */
#define RTL8365MB_TABLE_LUT_ADDR_REG			0x0502
#define   RTL8365MB_TABLE_LUT_ADDR_ADDRESS2_MASK	0x4000
#define   RTL8365MB_TABLE_LUT_ADDR_BUSY_FLAG_MASK	0x2000
#define   RTL8365MB_TABLE_LUT_ADDR_HIT_STATUS_MASK	0x1000
#define   RTL8365MB_TABLE_LUT_ADDR_TYPE_MASK		0x0800
#define   RTL8365MB_TABLE_LUT_ADDR_ADDRESS_MASK		0x07FF

/* CVLAN (i.e. vlan4k) table entry layout, u16[4] */
#define RTL8365MB_CVLAN_ENTRY_D0_MBR_MASK		0x00FF
#define RTL8365MB_CVLAN_ENTRY_D0_UNTAG_MASK		0xFF00
#define RTL8365MB_CVLAN_ENTRY_D1_FID_MASK		0x000F
#define RTL8365MB_CVLAN_ENTRY_D1_VBPEN_MASK		0x0010
#define RTL8365MB_CVLAN_ENTRY_D1_VBPRI_MASK		0x00E0
#define RTL8365MB_CVLAN_ENTRY_D1_ENVLANPOL_MASK		0x0100
#define RTL8365MB_CVLAN_ENTRY_D1_METERIDX_MASK		0x3E00
#define RTL8365MB_CVLAN_ENTRY_D1_IVL_SVL_MASK		0x4000
#define RTL8365MB_CVLAN_ENTRY_D2_MBR_EXT_MASK		0x0007
#define RTL8365MB_CVLAN_ENTRY_D2_UNTAG_EXT_MASK		0x0038
#define RTL8365MB_CVLAN_ENTRY_D2_METERIDX_EXT_MASK	0x0040

/* Port isolation (forwarding mask) registers */
#define RTL8365MB_PORT_ISOLATION_REG_BASE		0x08A2
#define RTL8365MB_PORT_ISOLATION_REG(_physport) \
		(RTL8365MB_PORT_ISOLATION_REG_BASE + (_physport))
#define   RTL8365MB_PORT_ISOLATION_MASK			0x07FF

/* Port-based VID registers 0~5 - each one holds an MC index for two ports */
#define RTL8365MB_VLAN_PVID_CTRL_BASE			0x0700

#define RTL8365MB_VLAN_PVID_CTRL_REG(_physport) \
		(RTL8365MB_VLAN_PVID_CTRL_BASE + ((_physport) >> 1))
#define   RTL8365MB_VLAN_PVID_CTRL_PORT0_VIDX_MASK	0x001F
#define   RTL8365MB_VLAN_PVID_CTRL_PORT1_VIDX_MASK	0x1F00
#define   RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_OFFSET(_physport) \
		(((_physport) & 1) << 3)
#define   RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_MASK(_physport) \
		(0x1F << RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_OFFSET(_physport))

/* VLAN control register */
#define RTL8365MB_VLAN_CTRL_REG			0x07A8
#define   RTL8365MB_VLAN_CTRL_EN_MASK		0x0001

/* VLAN ingress filter register - enable ingress filter on masked ports */
#define RTL8365MB_VLAN_INGRESS_REG		0x07A9
#define   RTL8365MB_VLAN_INGRESS_FILTER_PORT_EN_OFFSET(_physport) (_physport)
#define   RTL8365MB_VLAN_INGRESS_FILTER_PORT_EN_MASK(_physport) BIT(_physport)

/* VLAN member configuration registers 0~31, u16[3] */
#define RTL8365MB_VLAN_MC_BASE			0x0728
#define RTL8365MB_VLAN_MC_REG(_x)  \
		(RTL8365MB_VLAN_MC_BASE + (_x) * 4)
#define   RTL8365MB_VLAN_MC_D0_MBR_MASK			0x07FF
#define   RTL8365MB_VLAN_MC_D1_FID_MASK			0x000F
#define   RTL8365MB_VLAN_MC_D2_METERIDX_MASK		0x07E0
#define   RTL8365MB_VLAN_MC_D2_ENVLANPOL_MASK		0x0010
#define   RTL8365MB_VLAN_MC_D2_VBPRI_MASK		0x000E
#define   RTL8365MB_VLAN_MC_D2_VBPEN_MASK		0x0001
#define   RTL8365MB_VLAN_MC_D3_EVID_MASK		0x1FFF

enum rtl8365mb_table {
	RTL8365MB_TABLE_ACLRULE = RTL8365MB_TABLE_ACCESS_CTRL_TARGET_ACLRULE,
	RTL8365MB_TABLE_ACLACT = RTL8365MB_TABLE_ACCESS_CTRL_TARGET_ACLACT,
	RTL8365MB_TABLE_CVLAN = RTL8365MB_TABLE_ACCESS_CTRL_TARGET_CVLAN,
	RTL8365MB_TABLE_L2 = RTL8365MB_TABLE_ACCESS_CTRL_TARGET_L2,
	RTL8365MB_TABLE_IGMP_GROUP =
		RTL8365MB_TABLE_ACCESS_CTRL_TARGET_IGMP_GROUP,
};

/* MIB counter value registers */
#define RTL8365MB_MIB_COUNTER_BASE	0x1000
#define RTL8365MB_MIB_COUNTER_REG(_x)	(RTL8365MB_MIB_COUNTER_BASE + (_x))

/* MIB counter address register */
#define RTL8365MB_MIB_ADDRESS_REG		0x1004
#define   RTL8365MB_MIB_ADDRESS_PORT_OFFSET	0x7C
#define   RTL8365MB_MIB_ADDRESS(_p, _x) \
		(((RTL8365MB_MIB_ADDRESS_PORT_OFFSET) * (_p) + (_x)) >> 2)

#define RTL8365MB_MIB_CTRL0_REG			0x1005
#define   RTL8365MB_MIB_CTRL0_RESET_FLAG_MASK	0x2
#define   RTL8365MB_MIB_CTRL0_BUSY_FLAG_MASK	0x0001

static struct rtl8366_mib_counter rtl8365mb_mib_counters[] = {
	{ 0, 0, 4, "ifInOctets" },
	{ 0, 4, 2, "dot3StatsFCSErrors" },
	{ 0, 6, 2, "dot3StatsSymbolErrors" },
	{ 0, 8, 2, "dot3InPauseFrames" },
	{ 0, 10, 2, "dot3ControlInUnknownOpcodes" },
	{ 0, 12, 2, "etherStatsFragments" },
	{ 0, 14, 2, "etherStatsJabbers" },
	{ 0, 16, 2, "ifInUcastPkts" },
	{ 0, 18, 2, "etherStatsDropEvents" },
	{ 0, 20, 2, "ifInMulticastPkts" },
	{ 0, 22, 2, "ifInBroadcastPkts" },
	{ 0, 24, 2, "inMldChecksumError" },
	{ 0, 26, 2, "inIgmpChecksumError" },
	{ 0, 28, 2, "inMldSpecificQuery" },
	{ 0, 30, 2, "inMldGeneralQuery" },
	{ 0, 32, 2, "inIgmpSpecificQuery" },
	{ 0, 34, 2, "inIgmpGeneralQuery" },
	{ 0, 36, 2, "inMldLeaves" },
	{ 0, 38, 2, "inIgmpLeaves" },
	{ 0, 40, 4, "etherStatsOctets" },
	{ 0, 44, 2, "etherStatsUnderSizePkts" },
	{ 0, 46, 2, "etherOversizeStats" },
	{ 0, 48, 2, "etherStatsPkts64Octets" },
	{ 0, 50, 2, "etherStatsPkts65to127Octets" },
	{ 0, 52, 2, "etherStatsPkts128to255Octets" },
	{ 0, 54, 2, "etherStatsPkts256to511Octets" },
	{ 0, 56, 2, "etherStatsPkts512to1023Octets" },
	{ 0, 58, 2, "etherStatsPkts1024to1518Octets" },
	{ 0, 60, 4, "ifOutOctets" },
	{ 0, 64, 2, "dot3StatsSingleCollisionFrames" },
	{ 0, 66, 2, "dot3StatMultipleCollisionFrames" },
	{ 0, 68, 2, "dot3sDeferredTransmissions" },
	{ 0, 70, 2, "dot3StatsLateCollisions" },
	{ 0, 72, 2, "etherStatsCollisions" },
	{ 0, 74, 2, "dot3StatsExcessiveCollisions" },
	{ 0, 76, 2, "dot3OutPauseFrames" },
	{ 0, 78, 2, "ifOutDiscards" },
	{ 0, 80, 2, "dot1dTpPortInDiscards" },
	{ 0, 82, 2, "ifOutUcastPkts" },
	{ 0, 84, 2, "ifOutMulticastPkts" },
	{ 0, 86, 2, "ifOutBroadcastPkts" },
	{ 0, 88, 2, "outOampduPkts" },
	{ 0, 90, 2, "inOampduPkts" },
	{ 0, 92, 4, "inIgmpJoinsSuccess" },
	{ 0, 96, 2, "inIgmpJoinsFail" },
	{ 0, 98, 2, "inMldJoinsSuccess" },
	{ 0, 100, 2, "inMldJoinsFail" },
	{ 0, 102, 2, "inReportSuppressionDrop" },
	{ 0, 104, 2, "inLeaveSuppressionDrop" },
	{ 0, 106, 2, "outIgmpReports" },
	{ 0, 108, 2, "outIgmpLeaves" },
	{ 0, 110, 2, "outIgmpGeneralQuery" },
	{ 0, 112, 2, "outIgmpSpecificQuery" },
	{ 0, 114, 2, "outMldReports" },
	{ 0, 116, 2, "outMldLeaves" },
	{ 0, 118, 2, "outMldGeneralQuery" },
	{ 0, 120, 2, "outMldSpecificQuery" },
	{ 0, 122, 2, "inKnownMulticastPkts" },
};

struct rtl8365mb_vlan_mc {
	u16 evid;
	u16 member;
	u8 fid;
	u8 priority;
	u8 priority_en : 1;
	u8 policing_en : 1;
	u16 meteridx;
};

struct rtl8365mb_vlan_4k {
	u16 vid;
	u16 member;
	u16 untag;
	u8 fid;
	u8 priority;
	u8 priority_en : 1;
	u8 policing_en : 1;
	u8 ivl_en : 1;
	u8 meteridx;
};

struct rtl8365mb_jam_tbl_entry {
	u16 reg;
	u16 val;
};

/* Lifted from the vendor driver sources */
static const struct rtl8365mb_jam_tbl_entry rtl8365mb_init_jam_8365mb_vc[] = {
	{ 0x13EB, 0x15BB }, { 0x1303, 0x06D6 }, { 0x1304, 0x0700 },
	{ 0x13E2, 0x003F }, { 0x13F9, 0x0090 }, { 0x121E, 0x03CA },
	{ 0x1233, 0x0352 }, { 0x1237, 0x00A0 }, { 0x123A, 0x0030 },
	{ 0x1239, 0x0084 }, { 0x0301, 0x1000 }, { 0x1349, 0x001F },
	{ 0x18E0, 0x4004 }, { 0x122B, 0x241C }, { 0x1305, 0xC000 },
	{ 0x13F0, 0x0000 },
};

static const struct rtl8365mb_jam_tbl_entry rtl8365mb_init_jam_common[] = {
	{ 0x1200, 0x7FCB }, { 0x0884, 0x0003 }, { 0x06EB, 0x0001 },
	{ 0x03Fa, 0x0007 }, { 0x08C8, 0x00C0 }, { 0x0A30, 0x020E },
	{ 0x0800, 0x0000 }, { 0x0802, 0x0000 }, { 0x09DA, 0x0013 },
	{ 0x1D32, 0x0002 },
};

enum rtl8365mb_cpu_insert {
	RTL8365MB_CPU_INSERT_TO_ALL = 0,
	RTL8365MB_CPU_INSERT_TO_TRAPPING = 1,
	RTL8365MB_CPU_INSERT_TO_NONE = 2,
};

enum rtl8365mb_cpu_position {
	RTL8365MB_CPU_POS_AFTER_SA = 0,
	RTL8365MB_CPU_POS_BEFORE_CRC = 1,
};

enum rtl8365mb_cpu_format {
	RTL8365MB_CPU_FORMAT_8BYTES = 0,
	RTL8365MB_CPU_FORMAT_4BYTES = 1,
};

enum rtl8365mb_cpu_rxlen {
	RTL8365MB_CPU_RXLEN_72BYTES = 0,
	RTL8365MB_CPU_RXLEN_64BYTES = 1,
};

struct rtl8365mb_cpu {
	bool enable; /* enable CPU tagging */
	u32 mask; /* port mask of ports that should parse CPU tags */
	u32 trap_port; /* forward trapped frames to this port */
	enum rtl8365mb_cpu_insert insert; /* tag insertion mode */
	enum rtl8365mb_cpu_position position; /* position of CPU tag in frame */
	enum rtl8365mb_cpu_rxlen rx_length; /* minimum CPU RX length */
	enum rtl8365mb_cpu_format format; /* CPU tag format */
};

/* Private chip-specific driver data */
struct rtl8365mb {
	int irq; /* registered IRQ or zero */
	u32 chip_id;
	u32 chip_ver;
	unsigned int num_phys; /* number of integrated PHYs */
	u32 port_mask; /* mask of all ports */
	u32 phy_port_mask; /* mask of ports with integrated PHYs */
	struct rtl8365mb_cpu cpu; /* CPU configuration */
	const struct rtl8365mb_jam_tbl_entry *jam_table;
	size_t jam_size;
};

static int rtl8365mb_table_poll_busy(struct realtek_smi *smi)
{
	u32 val;

	return regmap_read_poll_timeout(
		smi->map, RTL8365MB_TABLE_LUT_ADDR_REG, val,
		(val & RTL8365MB_TABLE_LUT_ADDR_BUSY_FLAG_MASK) == 0, 10, 100);
}

static int rtl8365mb_table_read(struct realtek_smi *smi,
				enum rtl8365mb_table target, u32 addr,
				u16 *data, size_t size)
{
	u32 val;
	int ret;
	int i;

	if (!FIELD_FIT(RTL8365MB_TABLE_ACCESS_ADDR_MASK, addr))
		return -EINVAL;

	if (size > RTL8365MB_TABLE_ENTRY_SIZE)
		return -E2BIG;

	ret = rtl8365mb_table_poll_busy(smi);
	if (ret)
		return ret;

	/* Set read address */
	ret = regmap_write(smi->map, RTL8365MB_TABLE_ACCESS_ADDR_REG,
			   FIELD_PREP(RTL8365MB_TABLE_ACCESS_ADDR_MASK, addr));
	if (ret)
		return ret;

	/* Execute read operation */
	ret = regmap_write_bits(
		smi->map, RTL8365MB_TABLE_ACCESS_CTRL_REG,
		RTL8365MB_TABLE_ACCESS_CTRL_TARGET_MASK |
			RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_MASK,
		FIELD_PREP(RTL8365MB_TABLE_ACCESS_CTRL_TARGET_MASK, target) |
			FIELD_PREP(RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_MASK,
				   RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_READ));
	if (ret)
		return ret;

	ret = rtl8365mb_table_poll_busy(smi);
	if (ret)
		return ret;

	/* Read table entry */
	for (i = 0; i < size; i++) {
		ret = regmap_read(smi->map, RTL8365MB_TABLE_READ_REG(i), &val);
		if (ret)
			return ret;

		data[i] = val & 0xFFFF;
	}

	return 0;
}

static int rtl8365mb_table_write(struct realtek_smi *smi,
				 enum rtl8365mb_table target, u32 addr,
				 u16 *data, size_t size)
{
	int ret;
	int i;

	if (!FIELD_FIT(RTL8365MB_TABLE_ACCESS_ADDR_MASK, addr))
		return -EINVAL;

	if (size > RTL8365MB_TABLE_ENTRY_SIZE)
		return -E2BIG;

	/* Write table entry */
	for (i = 0; i < size; i++) {
		ret = regmap_write(smi->map, RTL8365MB_TABLE_WRITE_REG(i),
				   data[i]);
		if (ret)
			return ret;
	}

	/* Set write address */
	ret = regmap_write(smi->map, RTL8365MB_TABLE_ACCESS_ADDR_REG,
			   FIELD_PREP(RTL8365MB_TABLE_ACCESS_ADDR_MASK, addr));
	if (ret)
		return ret;

	/* Execute write operation */
	ret = regmap_write_bits(
		smi->map, RTL8365MB_TABLE_ACCESS_CTRL_REG,
		RTL8365MB_TABLE_ACCESS_CTRL_TARGET_MASK |
			RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_MASK,
		FIELD_PREP(RTL8365MB_TABLE_ACCESS_CTRL_TARGET_MASK, target) |
			FIELD_PREP(RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_MASK,
				   RTL8365MB_TABLE_ACCESS_CTRL_CMD_TYPE_WRITE));

	return ret;
}

static int rtl8365mb_phy_poll_busy(struct realtek_smi *smi)
{
	u32 val;

	return regmap_read_poll_timeout(smi->map,
					RTL8365MB_INDIRECT_ACCESS_STATUS_REG,
					val, val == 0, 10, 100);
}

static int rtl8365mb_phy_ocp_prepare(struct realtek_smi *smi, int phy,
				     u32 ocp_addr)
{
	u32 val;
	int ret;

	/* Set OCP prefix */
	val = FIELD_GET(RTL8365MB_PHY_OCP_ADDR_PREFIX_MASK, ocp_addr);
	ret = regmap_update_bits(
		smi->map, RTL8365MB_GPHY_OCP_MSB_0_REG,
		RTL8365MB_GPHY_OCP_MSB_0_CFG_CPU_OCPADR_MASK,
		FIELD_PREP(RTL8365MB_GPHY_OCP_MSB_0_CFG_CPU_OCPADR_MASK, val));
	if (ret)
		return ret;

	/* Set PHY register address */
	val = RTL8365MB_PHY_BASE;
	val |= FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_ADDRESS_PHYNUM_MASK, phy);
	val |= FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_ADDRESS_OCPADR_5_1_MASK,
			  ocp_addr >> 1);
	val |= FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_ADDRESS_OCPADR_9_6_MASK,
			  ocp_addr >> 6);
	ret = regmap_write(smi->map, RTL8365MB_INDIRECT_ACCESS_ADDRESS_REG,
			   val);
	if (ret)
		return ret;

	return 0;
}

static int rtl8365mb_phy_ocp_read(struct realtek_smi *smi, int phy,
				  u32 ocp_addr, u16 *data)
{
	u32 val;
	int ret;

	ret = rtl8365mb_phy_poll_busy(smi);
	if (ret)
		return ret;

	ret = rtl8365mb_phy_ocp_prepare(smi, phy, ocp_addr);
	if (ret)
		return ret;

	/* Execute read operation */
	val = FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_MASK,
			 RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_VALUE) |
	      FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_CTRL_RW_MASK,
			 RTL8365MB_INDIRECT_ACCESS_CTRL_RW_READ);
	ret = regmap_write(smi->map, RTL8365MB_INDIRECT_ACCESS_CTRL_REG, val);
	if (ret)
		return ret;

	ret = rtl8365mb_phy_poll_busy(smi);
	if (ret)
		return ret;

	/* Get PHY register data */
	ret = regmap_read(smi->map, RTL8365MB_INDIRECT_ACCESS_READ_DATA_REG,
			  &val);
	if (ret)
		return ret;

	*data = val & 0xFFFF;

	return 0;
}

static int rtl8365mb_phy_ocp_write(struct realtek_smi *smi, int phy,
				   u32 ocp_addr, u16 data)
{
	u32 val;
	int ret;

	ret = rtl8365mb_phy_poll_busy(smi);
	if (ret)
		return ret;

	ret = rtl8365mb_phy_ocp_prepare(smi, phy, ocp_addr);
	if (ret)
		return ret;

	/* Set PHY register data */
	ret = regmap_write(smi->map, RTL8365MB_INDIRECT_ACCESS_WRITE_DATA_REG,
			   data);
	if (ret)
		return ret;

	/* Execute write operation */
	val = FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_MASK,
			 RTL8365MB_INDIRECT_ACCESS_CTRL_CMD_VALUE) |
	      FIELD_PREP(RTL8365MB_INDIRECT_ACCESS_CTRL_RW_MASK,
			 RTL8365MB_INDIRECT_ACCESS_CTRL_RW_WRITE);
	ret = regmap_write(smi->map, RTL8365MB_INDIRECT_ACCESS_CTRL_REG, val);
	if (ret)
		return ret;

	ret = rtl8365mb_phy_poll_busy(smi);
	if (ret)
		return ret;

	return 0;
}

static int rtl8365mb_phy_read(struct realtek_smi *smi, int phy, int regnum)
{
	struct rtl8365mb *mb = smi->chip_data;
	u32 ocp_addr;
	u16 val;
	int ret;

	if (phy >= mb->num_phys || regnum > RTL8365MB_PHYREGMAX)
		return -EINVAL;

	ocp_addr = RTL8365MB_PHY_OCP_ADDR_PHYREG_BASE + regnum * 2;

	ret = rtl8365mb_phy_ocp_read(smi, phy, ocp_addr, &val);
	if (ret) {
		dev_err(smi->dev,
			"failed to read PHY%d reg %02x @ %04x, ret %d\n", phy,
			regnum, ocp_addr, ret);
		return ret;
	}

	dev_dbg(smi->dev, "read PHY%d register 0x%02x @ %04x, val <- %04x\n",
		phy, regnum, ocp_addr, val);

	return val;
}

static int rtl8365mb_phy_write(struct realtek_smi *smi, int phy, int regnum,
			       u16 val)
{
	struct rtl8365mb *mb = smi->chip_data;
	u32 ocp_addr;
	int ret;

	if (phy >= mb->num_phys || regnum > RTL8365MB_PHYREGMAX)
		return -EINVAL;

	ocp_addr = RTL8365MB_PHY_OCP_ADDR_PHYREG_BASE + regnum * 2;

	ret = rtl8365mb_phy_ocp_write(smi, phy, ocp_addr, val);
	if (ret) {
		dev_err(smi->dev,
			"failed to write PHY%d reg %02x @ %04x, ret %d\n", phy,
			regnum, ocp_addr, ret);
		return ret;
	}

	dev_dbg(smi->dev, "write PHY%d register 0x%02x @ %04x, val -> %04x\n",
		phy, regnum, ocp_addr, val);

	return 0;
}

static enum dsa_tag_protocol
rtl8365mb_get_tag_protocol(struct dsa_switch *ds, int port,
			   enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_RTL8_4;
}

static int rtl8365mb_ext_config_rgmii(struct realtek_smi *smi, int port,
				      phy_interface_t interface)
{
	int tx_delay = 0;
	int rx_delay = 0;
	int ext_port;
	int ret;

	if (port == smi->cpu_port) {
		ext_port = PORT_NUM_L2E(port);
	} else {
		dev_err(smi->dev, "only one EXT port is currently supported\n");
		return -EINVAL;
	}

	/* Set the RGMII TX/RX delay
	 *
	 * The Realtek vendor driver indicates the following possible
	 * configuration settings:
	 *
	 *   TX delay:
	 *     0 = no delay, 1 = 2 ns delay
	 *   RX delay:
	 *     0 = no delay, 7 = maximum delay
	 *     No units are specified, but there are a total of 8 steps.
	 *
	 * The vendor driver also states that this must be configured *before*
	 * forcing the external interface into a particular mode, which is done
	 * in the rtl8365mb_phylink_mac_link_{up,down} functions.
	 *
	 * NOTE: For now this is hardcoded to tx_delay = 1, rx_delay = 4.
	 */
	if (interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    interface == PHY_INTERFACE_MODE_RGMII_TXID)
		tx_delay = 1; /* 2 ns */

	if (interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    interface == PHY_INTERFACE_MODE_RGMII_RXID)
		rx_delay = 4;

	ret = regmap_update_bits(
		smi->map, RTL8365MB_EXT_RGMXF_REG(ext_port),
		RTL8365MB_EXT_RGMXF_TXDELAY_MASK |
			RTL8365MB_EXT_RGMXF_RXDELAY_MASK,
		FIELD_PREP(RTL8365MB_EXT_RGMXF_TXDELAY_MASK, tx_delay) |
			FIELD_PREP(RTL8365MB_EXT_RGMXF_RXDELAY_MASK, rx_delay));
	if (ret)
		return ret;

	ret = regmap_update_bits(
		smi->map, RTL8365MB_DIGITAL_INTERFACE_SELECT_REG(ext_port),
		RTL8365MB_DIGITAL_INTERFACE_SELECT_MODE_MASK(ext_port),
		RTL8365MB_EXT_PORT_MODE_RGMII
			<< RTL8365MB_DIGITAL_INTERFACE_SELECT_MODE_OFFSET(
				   ext_port));
	if (ret)
		return ret;

	return 0;
}

static int rtl8365mb_ext_config_forcemode(struct realtek_smi *smi, int port,
					  bool link, int speed, int duplex,
					  bool tx_pause, bool rx_pause)
{
	u32 r_tx_pause;
	u32 r_rx_pause;
	u32 r_duplex;
	u32 r_speed;
	u32 r_link;
	int ext_port;
	int val;
	int ret;

	if (port == smi->cpu_port) {
		ext_port = PORT_NUM_L2E(port);
	} else {
		dev_err(smi->dev, "only one EXT port is currently supported\n");
		return -EINVAL;
	}

	if (link) {
		/* Force the link up with the desired configuration */
		r_link = 1;
		r_rx_pause = rx_pause ? 1 : 0;
		r_tx_pause = tx_pause ? 1 : 0;

		if (speed == SPEED_1000) {
			r_speed = RTL8365MB_PORT_SPEED_1000M;
		} else if (speed == SPEED_100) {
			r_speed = RTL8365MB_PORT_SPEED_100M;
		} else if (speed == SPEED_10) {
			r_speed = RTL8365MB_PORT_SPEED_10M;
		} else {
			dev_err(smi->dev, "unsupported port speed %s\n",
				phy_speed_to_str(speed));
			return -EINVAL;
		}

		if (duplex == DUPLEX_FULL) {
			r_duplex = 1;
		} else if (duplex == DUPLEX_HALF) {
			r_duplex = 0;
		} else {
			dev_err(smi->dev, "unsupported duplex %s\n",
				phy_duplex_to_str(duplex));
			return -EINVAL;
		}
	} else {
		/* Force the link down and reset any programmed configuration */
		r_link = 0;
		r_tx_pause = 0;
		r_rx_pause = 0;
		r_speed = 0;
		r_duplex = 0;
	}

	val = FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_EN_MASK, 1) |
	      FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_TXPAUSE_MASK,
			 r_tx_pause) |
	      FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_RXPAUSE_MASK,
			 r_rx_pause) |
	      FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_LINK_MASK, r_link) |
	      FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_DUPLEX_MASK,
			 r_duplex) |
	      FIELD_PREP(RTL8365MB_DIGITAL_INTERFACE_FORCE_SPEED_MASK, r_speed);
	ret = regmap_write(
		smi->map, RTL8365MB_DIGITAL_INTERFACE_FORCE_REG(ext_port), val);
	if (ret)
		return ret;

	return 0;
}

static bool rtl8365mb_phy_mode_supported(struct dsa_switch *ds, int port,
					 phy_interface_t interface)
{
	if (dsa_is_user_port(ds, port) &&
	    (interface == PHY_INTERFACE_MODE_NA ||
	     interface == PHY_INTERFACE_MODE_INTERNAL))
		/* Internal PHY */
		return true;
	else if (dsa_is_cpu_port(ds, port) &&
		 phy_interface_mode_is_rgmii(interface))
		/* Extension MAC */
		return true;

	return false;
}

static void rtl8365mb_phylink_validate(struct dsa_switch *ds, int port,
				       unsigned long *supported,
				       struct phylink_link_state *state)
{
	struct realtek_smi *smi = ds->priv;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0 };

	/* include/linux/phylink.h says:
	 *     When @state->interface is %PHY_INTERFACE_MODE_NA, phylink
	 *     expects the MAC driver to return all supported link modes.
	 */
	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    !rtl8365mb_phy_mode_supported(ds, port, state->interface)) {
		dev_err(smi->dev, "phy mode %s is unsupported on port %d\n",
			phy_modes(state->interface), port);
		linkmode_zero(supported);
		return;
	}

	phylink_set_port_modes(mask);

	phylink_set(mask, Autoneg);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);

	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 1000baseT_Full);
	phylink_set(mask, 1000baseT_Half);

	linkmode_and(supported, supported, mask);
	linkmode_and(state->advertising, state->advertising, mask);
}

static void rtl8365mb_phylink_mac_config(struct dsa_switch *ds, int port,
					 unsigned int mode,
					 const struct phylink_link_state *state)
{
	struct realtek_smi *smi = ds->priv;
	int ret;

	if (!rtl8365mb_phy_mode_supported(ds, port, state->interface)) {
		dev_err(smi->dev, "phy mode %s is unsupported on port %d\n",
			phy_modes(state->interface), port);
		return;
	}

	/* If port MAC is connected to an internal PHY, we have nothing to do */
	if (dsa_is_user_port(ds, port))
		return;

	if (mode != MLO_AN_PHY && mode != MLO_AN_FIXED) {
		dev_err(smi->dev,
			"port %d supports only conventional PHY or fixed-link\n",
			port);
		return;
	}

	if (phy_interface_mode_is_rgmii(state->interface)) {
		ret = rtl8365mb_ext_config_rgmii(smi, port, state->interface);
		if (ret)
			dev_err(smi->dev,
				"failed to configure RGMII mode on port %d: %d\n",
				port, ret);
		return;
	}

	/* TODO: Implement MII and RMII modes, which the RTL8365MB-VC also
	 * supports
	 */
}

static void rtl8365mb_phylink_mac_link_down(struct dsa_switch *ds, int port,
					    unsigned int mode,
					    phy_interface_t interface)
{
	struct realtek_smi *smi = ds->priv;
	int ret;

	if (dsa_is_cpu_port(ds, port)) {
		ret = rtl8365mb_ext_config_forcemode(smi, port, false, 0, 0,
						     false, false);
		if (ret)
			dev_err(smi->dev,
				"failed to reset forced mode on port %d: %d\n",
				port, ret);

		return;
	}
}

static void rtl8365mb_phylink_mac_link_up(struct dsa_switch *ds, int port,
					  unsigned int mode,
					  phy_interface_t interface,
					  struct phy_device *phydev, int speed,
					  int duplex, bool tx_pause,
					  bool rx_pause)
{
	struct realtek_smi *smi = ds->priv;
	int ret;

	if (dsa_is_cpu_port(ds, port)) {
		ret = rtl8365mb_ext_config_forcemode(
			smi, port, true, speed, duplex, tx_pause, rx_pause);
		if (ret)
			dev_err(smi->dev,
				"failed to force mode on port %d: %d\n", port,
				ret);

		return;
	}
}

static int rtl8365mb_port_enable(struct dsa_switch *ds, int port,
				 struct phy_device *phy)
{
	struct realtek_smi *smi = ds->priv;
	int val;

	if (dsa_is_user_port(ds, port)) {
		/* Power up the internal PHY and restart autonegotiation */
		val = rtl8365mb_phy_read(smi, port, MII_BMCR);
		if (val < 0)
			return val;

		val &= ~BMCR_PDOWN;
		val |= BMCR_ANRESTART;

		return rtl8365mb_phy_write(smi, port, MII_BMCR, val);
	}

	return 0;
}

static void rtl8365mb_port_disable(struct dsa_switch *ds, int port)
{
	struct realtek_smi *smi = ds->priv;
	int val;

	if (dsa_is_user_port(ds, port)) {
		/* Power down the internal PHY */
		val = rtl8365mb_phy_read(smi, port, MII_BMCR);
		if (val < 0)
			return;

		val |= BMCR_PDOWN;

		rtl8365mb_phy_write(smi, port, MII_BMCR, val);

		return;
	}
}

int rtl8365mb_port_vlan_filtering(struct dsa_switch *ds, int port,
				  bool vlan_filtering,
				  struct netlink_ext_ack *extack)
{
	struct realtek_smi *smi = ds->priv;
	u32 phys_port = PORT_NUM_L2P(port);
	int ret;

	dev_info(smi->dev, "%s filtering on port %d\n",
		 vlan_filtering ? "enable" : "disable", port);

	/* vlan_filtering on: Discard VLAN tagged frames if the port is not a
	 * member of the VLAN with which the packet is associated. Untagged
	 * packets should also be discarded unless the port has a PVID
	 * programmed.
	 *
	 * vlan_filtering off: Accept all VLAN tagged frames, including
	 * untagged.
	 */
	ret = regmap_update_bits(
		smi->map, RTL8365MB_VLAN_INGRESS_REG,
		RTL8365MB_VLAN_INGRESS_FILTER_PORT_EN_MASK(phys_port),
		(vlan_filtering ? 1 : 0)
			<< RTL8365MB_VLAN_INGRESS_FILTER_PORT_EN_OFFSET(
				   phys_port));
	if (ret)
		return ret;

	return 0;
}

static int _rtl8365mb_get_vlan_4k(struct realtek_smi *smi, u32 vid,
				  struct rtl8365mb_vlan_4k *vlan4k)
{
	u32 phys_member;
	u32 phys_untag;
	u16 data[3];
	int ret;

	if (vid > RTL8365MB_VIDMAX)
		return -EINVAL;

	ret = rtl8365mb_table_read(smi, RTL8365MB_TABLE_CVLAN, vid, data,
				   ARRAY_SIZE(data));
	if (ret)
		return ret;

	memset(vlan4k, 0, sizeof(*vlan4k));

	/* Unpack table entry */
	vlan4k->vid = vid;
	phys_member = FIELD_GET(RTL8365MB_CVLAN_ENTRY_D0_MBR_MASK, data[0]) |
		      (FIELD_GET(RTL8365MB_CVLAN_ENTRY_D2_MBR_EXT_MASK, data[2])
		       << 8);
	phys_untag =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D0_UNTAG_MASK, data[0]) |
		(FIELD_GET(RTL8365MB_CVLAN_ENTRY_D2_UNTAG_EXT_MASK, data[2])
		 << 8);
	vlan4k->member = PORT_MASK_P2L(phys_member);
	vlan4k->untag = PORT_MASK_P2L(phys_untag);
	vlan4k->fid = FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_FID_MASK, data[1]);
	vlan4k->priority_en =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_VBPEN_MASK, data[1]);
	vlan4k->priority =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_VBPRI_MASK, data[1]);
	vlan4k->policing_en =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_ENVLANPOL_MASK, data[1]);
	vlan4k->meteridx =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_METERIDX_MASK, data[1]) |
		(FIELD_GET(RTL8365MB_CVLAN_ENTRY_D2_METERIDX_EXT_MASK, data[2])
		 << 5);
	vlan4k->ivl_en =
		FIELD_GET(RTL8365MB_CVLAN_ENTRY_D1_IVL_SVL_MASK, data[1]);

	return 0;
}

static int rtl8365mb_get_vlan_4k(struct realtek_smi *smi, u32 vid,
				 struct rtl8366_vlan_4k *vlan4k)
{
	struct rtl8365mb_vlan_4k vlan4k_65mb;
	int ret;

	ret = _rtl8365mb_get_vlan_4k(smi, vid, &vlan4k_65mb);
	if (ret)
		return ret;

	memset(vlan4k, 0, sizeof(*vlan4k));

	vlan4k->vid = vlan4k_65mb.vid;
	vlan4k->untag = vlan4k_65mb.untag;
	vlan4k->member = vlan4k_65mb.member;
	vlan4k->fid = vlan4k_65mb.fid;

	return 0;
}

static int _rtl8365mb_set_vlan_4k(struct realtek_smi *smi,
				  const struct rtl8365mb_vlan_4k *vlan4k)
{
	struct rtl8365mb *mb = smi->chip_data;
	u16 data[3] = { 0 };
	u32 phys_member;
	u32 phys_untag;

	if ((vlan4k->member & ~(mb->port_mask)) ||
	    (vlan4k->untag & ~(mb->port_mask)) ||
	    vlan4k->vid > RTL8365MB_VIDMAX || vlan4k->fid > RTL8365MB_FIDMAX ||
	    vlan4k->priority > RTL8365MB_PRIORITYMAX ||
	    vlan4k->meteridx > RTL8365MB_METERMAX)
		return -EINVAL;

	phys_member = PORT_MASK_L2P(vlan4k->member);
	phys_untag = PORT_MASK_L2P(vlan4k->untag);

	/* Pack table entry value */
	data[0] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D0_MBR_MASK, phys_member);
	data[0] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D0_UNTAG_MASK, phys_untag);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_FID_MASK, vlan4k->fid);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_VBPEN_MASK,
			      vlan4k->priority_en);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_VBPRI_MASK,
			      vlan4k->priority);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_ENVLANPOL_MASK,
			      vlan4k->policing_en);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_METERIDX_MASK,
			      vlan4k->meteridx);
	data[1] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D1_IVL_SVL_MASK,
			      vlan4k->ivl_en);
	data[2] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D2_MBR_EXT_MASK,
			      phys_member >> 8);
	data[2] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D2_UNTAG_EXT_MASK,
			      phys_untag >> 8);
	data[2] |= FIELD_PREP(RTL8365MB_CVLAN_ENTRY_D2_METERIDX_EXT_MASK,
			      vlan4k->meteridx >> 5);

	return rtl8365mb_table_write(smi, RTL8365MB_TABLE_CVLAN, vlan4k->vid,
				     data, ARRAY_SIZE(data));
}

static int rtl8365mb_set_vlan_4k(struct realtek_smi *smi,
				 const struct rtl8366_vlan_4k *vlan4k)
{
	struct rtl8365mb_vlan_4k vlan4k_65mb = { 0 };

	/* The RTL8365MB-VC has a more granular VLAN configuration than what is
	 * offered by the RTL8366x helper library. Extend this as you wish.
	 */
	vlan4k_65mb.vid = vlan4k->vid;
	vlan4k_65mb.member = vlan4k->member;
	vlan4k_65mb.untag = vlan4k->untag;
	vlan4k_65mb.fid = vlan4k->fid;

	return _rtl8365mb_set_vlan_4k(smi, &vlan4k_65mb);
}

static int _rtl8365mb_get_vlan_mc(struct realtek_smi *smi, u32 index,
				  struct rtl8365mb_vlan_mc *vlanmc)
{
	u32 phys_member;
	u16 data[4];
	u32 val;
	int ret;
	int i;

	if (index >= smi->num_vlan_mc)
		return -EINVAL;

	for (i = 0; i < 4; i++) {
		ret = regmap_read(smi->map, RTL8365MB_VLAN_MC_REG(index) + i,
				  &val);
		if (ret)
			return ret;

		data[i] = val & 0xFFFF;
	}

	memset(vlanmc, 0, sizeof(*vlanmc));

	phys_member = FIELD_GET(RTL8365MB_VLAN_MC_D0_MBR_MASK, data[0]);
	vlanmc->member = PORT_MASK_P2L(phys_member);
	vlanmc->fid = FIELD_GET(RTL8365MB_VLAN_MC_D1_FID_MASK, data[1]);
	vlanmc->meteridx =
		FIELD_GET(RTL8365MB_VLAN_MC_D2_METERIDX_MASK, data[2]);
	vlanmc->policing_en =
		FIELD_GET(RTL8365MB_VLAN_MC_D2_ENVLANPOL_MASK, data[2]);
	vlanmc->priority = FIELD_GET(RTL8365MB_VLAN_MC_D2_VBPRI_MASK, data[2]);
	vlanmc->priority_en =
		FIELD_GET(RTL8365MB_VLAN_MC_D2_VBPEN_MASK, data[2]);
	vlanmc->evid = FIELD_GET(RTL8365MB_VLAN_MC_D3_EVID_MASK, data[3]);

	return 0;
}

static int rtl8365mb_get_vlan_mc(struct realtek_smi *smi, u32 index,
				 struct rtl8366_vlan_mc *vlanmc)
{
	struct rtl8365mb_vlan_mc vlanmc_65mb;
	struct rtl8365mb_vlan_4k vlan4k_65mb;
	int ret;

	ret = _rtl8365mb_get_vlan_mc(smi, index, &vlanmc_65mb);
	if (ret)
		return ret;

	/* The untag field is not stored in the membership config, so grab it
	 * from the vlan4k table instead. This is mostly for compatibility with
	 * the RTL8366x helper library.
	 */
	ret = _rtl8365mb_get_vlan_4k(smi, vlanmc_65mb.evid, &vlan4k_65mb);
	if (ret)
		return ret;

	memset(vlanmc, 0, sizeof(*vlanmc));

	vlanmc->vid = vlanmc_65mb.evid;
	vlanmc->untag = vlan4k_65mb.untag;
	vlanmc->member = vlanmc_65mb.member;
	vlanmc->fid = vlanmc_65mb.fid;
	vlanmc->priority = vlanmc_65mb.priority;

	return 0;
}

static int _rtl8365mb_set_vlan_mc(struct realtek_smi *smi, u32 index,
				  const struct rtl8365mb_vlan_mc *vlanmc)
{
	struct rtl8365mb *mb = smi->chip_data;
	u16 data[4] = { 0 };
	u32 phys_member;
	int ret;
	int i;

	if (index >= smi->num_vlan_mc || (vlanmc->member & ~(mb->port_mask)) ||
	    vlanmc->evid > RTL8365MB_EVIDMAX ||
	    vlanmc->fid > RTL8365MB_FIDMAX ||
	    vlanmc->priority > RTL8365MB_PRIORITYMAX ||
	    vlanmc->meteridx > RTL8365MB_METERMAX)
		return -EINVAL;

	phys_member = PORT_MASK_L2P(vlanmc->member);

	data[0] |= FIELD_PREP(RTL8365MB_VLAN_MC_D0_MBR_MASK, phys_member);
	data[1] |= FIELD_PREP(RTL8365MB_VLAN_MC_D1_FID_MASK, vlanmc->fid);
	data[2] |= FIELD_PREP(RTL8365MB_VLAN_MC_D2_METERIDX_MASK,
			      vlanmc->meteridx);
	data[2] |= FIELD_PREP(RTL8365MB_VLAN_MC_D2_ENVLANPOL_MASK,
			      vlanmc->policing_en);
	data[2] |=
		FIELD_PREP(RTL8365MB_VLAN_MC_D2_VBPRI_MASK, vlanmc->priority);
	data[2] |= FIELD_PREP(RTL8365MB_VLAN_MC_D2_VBPEN_MASK,
			      vlanmc->priority_en);
	data[3] |= FIELD_PREP(RTL8365MB_VLAN_MC_D3_EVID_MASK, vlanmc->evid);

	for (i = 0; i < 4; i++) {
		ret = regmap_write(smi->map, RTL8365MB_VLAN_MC_REG(index) + i,
				   data[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtl8365mb_set_vlan_mc(struct realtek_smi *smi, u32 index,
				 const struct rtl8366_vlan_mc *vlanmc)
{
	struct rtl8365mb_vlan_mc vlanmc_65mb = { 0 };

	vlanmc_65mb.evid = vlanmc->vid;
	/* The untag field is set in the corresponding vlan4k entry */
	vlanmc_65mb.member = vlanmc->member;
	vlanmc_65mb.fid = vlanmc->fid;
	vlanmc_65mb.priority = vlanmc->priority;

	return _rtl8365mb_set_vlan_mc(smi, index, &vlanmc_65mb);
}

static int rtl8365mb_get_mc_index(struct realtek_smi *smi, int port, int *index)
{
	int phys_port = PORT_NUM_L2P(port);
	u32 val;
	int ret;

	if (port >= smi->num_ports || *index >= smi->num_vlan_mc)
		return -EINVAL;

	ret = regmap_read(smi->map, RTL8365MB_VLAN_PVID_CTRL_REG(phys_port),
			  &val);
	if (ret)
		return ret;

	*index = (val & (RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_MASK(phys_port))) >>
		 RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_OFFSET(phys_port);

	return 0;
}

static int rtl8365mb_set_mc_index(struct realtek_smi *smi, int port, int index)
{
	int phys_port = PORT_NUM_L2P(port);

	if (port >= smi->num_ports || index >= smi->num_vlan_mc)
		return -EINVAL;

	return regmap_update_bits(
		smi->map, RTL8365MB_VLAN_PVID_CTRL_REG(phys_port),
		RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_MASK(phys_port),
		index << RTL8365MB_VLAN_PVID_CTRL_PORT_VIDX_OFFSET(phys_port));
}

static int rtl8365mb_ports_isolate(struct realtek_smi *smi)
{
	u32 phys_mask = PORT_MASK_L2P(BIT(smi->cpu_port));
	int ret;
	int i;

	/* Isolate all ports by setting the forwarding mask for each port to
	 * BIT(cpu_port). The switch will then forward all frames from the user
	 * ports to the CPU port only.
	 *
	 * For the CPU port, the forwarding mask is effectively zero. The switch
	 * will then reject all non-CPU tagged frames from the CPU port, and
	 * honour the TX mask in all CPU tagged frames from the CPU port
	 * irrespective of the forwarding mask.
	 */
	for (i = 0; i < smi->num_ports; i++) {
		u32 phys_port = PORT_NUM_L2P(i);

		ret = regmap_write(smi->map,
				   RTL8365MB_PORT_ISOLATION_REG(phys_port),
				   phys_mask);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtl8365mb_get_mib_counter(struct realtek_smi *smi, int port,
				     struct rtl8366_mib_counter *mib,
				     u64 *mibvalue)
{
	int phys_port = PORT_NUM_L2P(port);
	u32 offset;
	u32 val;
	int ret;
	int i;

	/* The MIB address is an SRAM address. We request a particular address
	 * and then poll the control register before reading the value from some
	 * counter registers.
	 */
	ret = regmap_write(smi->map, RTL8365MB_MIB_ADDRESS_REG,
			   RTL8365MB_MIB_ADDRESS(phys_port, mib->offset));
	if (ret)
		return ret;

	/* Poll for completion */
	ret = regmap_read_poll_timeout(
		smi->map, RTL8365MB_MIB_CTRL0_REG, val,
		(val & RTL8365MB_MIB_CTRL0_BUSY_FLAG_MASK) == 0, 10, 100);
	if (ret)
		return ret;

	/* Presumably this indicates a MIB counter read failure */
	if (val & RTL8365MB_MIB_CTRL0_RESET_FLAG_MASK)
		return -EIO;

	/* There are four MIB counter registers each holding a 16 bit word of a
	 * MIB counter. Depending on the offset, we should read from the upper
	 * two or lower two registers. In case the MIB counter is 4 words, we
	 * read from all four registers.
	 */
	if (mib->length == 4)
		offset = 3;
	else
		offset = (mib->offset + 1) % 4;

	/* Read the MIB counter 16 bits at a time */
	*mibvalue = 0;
	for (i = 0; i < mib->length; i++) {
		ret = regmap_read(smi->map,
				  RTL8365MB_MIB_COUNTER_REG(offset - i), &val);
		if (ret)
			return ret;

		*mibvalue = ((*mibvalue) << 16) | (val & 0xFFFF);
	}

	return 0;
}

static bool rtl8365mb_is_vlan_valid(struct realtek_smi *smi, unsigned int vlan)
{
	if (vlan > RTL8365MB_VIDMAX)
		return false;

	return true;
}

static int rtl8365mb_enable_vlan(struct realtek_smi *smi, bool enable)
{
	dev_dbg(smi->dev, "%s VLAN\n", enable ? "enable" : "disable");
	return regmap_update_bits(
		smi->map, RTL8365MB_VLAN_CTRL_REG, RTL8365MB_VLAN_CTRL_EN_MASK,
		FIELD_PREP(RTL8365MB_VLAN_CTRL_EN_MASK, enable ? 1 : 0));
}

static int rtl8365mb_enable_vlan4k(struct realtek_smi *smi, bool enable)
{
	return rtl8365mb_enable_vlan(smi, enable);
}

static int rtl8365mb_get_and_clear_status_reg(struct realtek_smi *smi, u32 reg,
					      u32 *val)
{
	int ret;

	ret = regmap_read(smi->map, reg, val);
	if (ret)
		return ret;

	return regmap_write(smi->map, reg, *val);
}

static irqreturn_t rtl8365mb_irq(int irq, void *data)
{
	struct realtek_smi *smi = data;
	struct rtl8365mb *mb;
	u32 line_changes = 0;
	u32 linkdown_ind;
	u32 linkup_ind;
	u32 stat;
	u32 val;
	int ret;

	mb = smi->chip_data;

	ret = rtl8365mb_get_and_clear_status_reg(smi, RTL8365MB_INTR_STATUS_REG,
						 &stat);
	if (ret)
		goto out_error;

	if (stat & RTL8365MB_INTR_STATUS_LINK_CHANGE_MASK) {
		ret = rtl8365mb_get_and_clear_status_reg(
			smi, RTL8365MB_PORT_LINKUP_IND_REG, &val);
		if (ret)
			goto out_error;

		linkup_ind = FIELD_GET(RTL8365MB_PORT_LINKUP_IND_MASK, val);

		ret = rtl8365mb_get_and_clear_status_reg(
			smi, RTL8365MB_PORT_LINKDOWN_IND_REG, &val);
		if (ret)
			goto out_error;

		linkdown_ind = FIELD_GET(RTL8365MB_PORT_LINKDOWN_IND_MASK, val);

		line_changes = PORT_MASK_P2L(linkup_ind | linkdown_ind) &
			       mb->phy_port_mask;
	}

	if (!line_changes)
		goto out_none;

	while (line_changes) {
		int line = __ffs(line_changes);
		int child_irq;

		line_changes &= ~BIT(line);

		child_irq = irq_find_mapping(smi->irqdomain, line);
		handle_nested_irq(child_irq);
	}

	return IRQ_HANDLED;

out_error:
	dev_err(smi->dev, "failed to read interrupt status: %d\n", ret);

out_none:
	return IRQ_NONE;
}

static struct irq_chip rtl8365mb_irq_chip = {
	.name = "rtl8365mb",
	/* The hardware doesn't support masking IRQs on a per-port basis */
};

static int rtl8365mb_irq_map(struct irq_domain *domain, unsigned int irq,
			     irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, domain->host_data);
	irq_set_chip_and_handler(irq, &rtl8365mb_irq_chip, handle_simple_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	return 0;
}

static void rtl8365mb_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_nested_thread(irq, 0);
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops rtl8365mb_irqdomain_ops = {
	.map = rtl8365mb_irq_map,
	.unmap = rtl8365mb_irq_unmap,
	.xlate = irq_domain_xlate_onecell,
};

static int _rtl8365mb_irq_enable(struct realtek_smi *smi, bool enable)
{
	return regmap_update_bits(
		smi->map, RTL8365MB_INTR_CTRL,
		RTL8365MB_INTR_CTRL_LINK_CHANGE_MASK,
		FIELD_PREP(RTL8365MB_INTR_CTRL_LINK_CHANGE_MASK,
			   enable ? 1 : 0));
}

static int rtl8365mb_irq_enable(struct realtek_smi *smi)
{
	return _rtl8365mb_irq_enable(smi, true);
}

static int rtl8365mb_irq_disable(struct realtek_smi *smi)
{
	return _rtl8365mb_irq_enable(smi, false);
}

static int rtl8365mb_irq_setup(struct realtek_smi *smi)
{
	struct rtl8365mb *mb = smi->chip_data;
	struct device_node *intc;
	u32 irq_trig;
	int virq;
	int irq;
	u32 val;
	int ret;
	int i;

	intc = of_get_child_by_name(smi->dev->of_node, "interrupt-controller");
	if (!intc) {
		dev_err(smi->dev, "missing child interrupt-controller node\n");
		return -EINVAL;
	}

	smi->irqdomain = irq_domain_add_linear(intc, mb->num_phys,
					       &rtl8365mb_irqdomain_ops, smi);
	if (!smi->irqdomain) {
		dev_err(smi->dev, "failed to add irq domain\n");
		ret = -ENOMEM;
		goto out_put_node;
	}

	for (i = 0; i < mb->num_phys; i++) {
		virq = irq_create_mapping(smi->irqdomain, i);
		if (!virq) {
			dev_err(smi->dev,
				"failed to create irq domain mapping\n");
			ret = -EINVAL;
			goto out_remove_irqdomain;
		}

		irq_set_parent(virq, irq);
	}

	/* rtl8365mb IRQs cascade off this one */
	irq = of_irq_get(intc, 0);
	if (irq <= 0) {
		if (irq != -EPROBE_DEFER)
			dev_err(smi->dev, "failed to get parent irq: %d\n",
				irq);
		ret = irq ? irq : -EINVAL;
		goto out_remove_irqdomain;
	}

	/* Configure chip interrupt signal polarity */
	irq_trig = irqd_get_trigger_type(irq_get_irq_data(irq));
	switch (irq_trig) {
	case IRQF_TRIGGER_RISING:
	case IRQF_TRIGGER_HIGH:
		val = RTL8365MB_INTR_POLARITY_HIGH;
		break;
	case IRQF_TRIGGER_FALLING:
	case IRQF_TRIGGER_LOW:
		val = RTL8365MB_INTR_POLARITY_LOW;
		break;
	default:
		dev_err(smi->dev, "unsupported irq trigger type %u\n",
			irq_trig);
		ret = -EINVAL;
		goto out_remove_irqdomain;
	}

	ret = regmap_update_bits(smi->map, RTL8365MB_INTR_POLARITY_REG,
				 RTL8365MB_INTR_POLARITY_MASK,
				 FIELD_PREP(RTL8365MB_INTR_POLARITY_MASK, val));
	if (ret)
		goto out_remove_irqdomain;

	/* Disable the interrupt in case the chip has it enabled on reset */
	ret = rtl8365mb_irq_disable(smi);
	if (ret)
		goto out_remove_irqdomain;

	/* Clear the interrupt status register */
	ret = regmap_write(smi->map, RTL8365MB_INTR_STATUS_REG,
			   RTL8365MB_INTR_STATUS_ALL_MASK);
	if (ret)
		goto out_remove_irqdomain;

	ret = request_threaded_irq(irq, NULL, rtl8365mb_irq, IRQF_ONESHOT,
				   "rtl8365mb", smi);
	if (ret) {
		dev_err(smi->dev, "failed to request irq: %d\n", ret);
		goto out_remove_irqdomain;
	}

	/* Store the irq so that we know to free it during teardown */
	mb->irq = irq;

	ret = rtl8365mb_irq_enable(smi);
	if (ret)
		goto out_free_irq;

	of_node_put(intc);

	return 0;

out_free_irq:
	free_irq(mb->irq, smi);
	mb->irq = 0;

out_remove_irqdomain:
	for (i = 0; i < mb->num_phys; i++) {
		virq = irq_find_mapping(smi->irqdomain, i);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(smi->irqdomain);
	smi->irqdomain = NULL;

out_put_node:
	of_node_put(intc);

	return ret;
}

static void rtl8365mb_irq_teardown(struct realtek_smi *smi)
{
	struct rtl8365mb *mb = smi->chip_data;
	int virq;
	int i;

	if (mb->irq) {
		free_irq(mb->irq, smi);
		mb->irq = 0;
	}

	if (smi->irqdomain) {
		for (i = 0; i < mb->num_phys; i++) {
			virq = irq_find_mapping(smi->irqdomain, i);
			irq_dispose_mapping(virq);
		}

		irq_domain_remove(smi->irqdomain);
		smi->irqdomain = NULL;
	}
}

static int rtl8365mb_cpu_config(struct realtek_smi *smi)
{
	struct rtl8365mb *mb = smi->chip_data;
	struct rtl8365mb_cpu *cpu = &mb->cpu;
	u32 phys_mask;
	u32 phys_trap_port;
	u32 val;
	int ret;

	phys_mask = PORT_MASK_L2P(cpu->mask);
	phys_trap_port = PORT_NUM_L2P(cpu->trap_port);

	ret = regmap_update_bits(smi->map, RTL8365MB_CPU_PORT_MASK_REG,
				 RTL8365MB_CPU_PORT_MASK_MASK,
				 FIELD_PREP(RTL8365MB_CPU_PORT_MASK_MASK,
					    phys_mask));
	if (ret)
		return ret;

	val = FIELD_PREP(RTL8365MB_CPU_CTRL_EN_MASK, cpu->enable ? 1 : 0) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_INSERTMODE_MASK, cpu->insert) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_TAG_POSITION_MASK, cpu->position) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_RXBYTECOUNT_MASK, cpu->rx_length) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_TAG_FORMAT_MASK, cpu->format) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_TRAP_PORT_MASK, phys_trap_port) |
	      FIELD_PREP(RTL8365MB_CPU_CTRL_TRAP_PORT_EXT_MASK,
			 phys_trap_port >> 3);
	ret = regmap_write(smi->map, RTL8365MB_CPU_CTRL_REG, val);
	if (ret)
		return ret;

	return 0;
}

static int rtl8365mb_switch_init(struct realtek_smi *smi)
{
	struct rtl8365mb *mb = smi->chip_data;
	int ret;
	int i;

	/* Do any chip-specific init jam before getting to the common stuff */
	if (mb->jam_table) {
		for (i = 0; i < mb->jam_size; i++) {
			ret = regmap_write(smi->map, mb->jam_table[i].reg,
					   mb->jam_table[i].val);
			if (ret)
				return ret;
		}
	}

	/* Common init jam */
	for (i = 0; i < ARRAY_SIZE(rtl8365mb_init_jam_common); i++) {
		ret = regmap_write(smi->map, rtl8365mb_init_jam_common[i].reg,
				   rtl8365mb_init_jam_common[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtl8365mb_reset_chip(struct realtek_smi *smi)
{
	u32 val;

	realtek_smi_write_reg_noack(smi, RTL8365MB_CHIP_RESET_REG,
				    FIELD_PREP(RTL8365MB_CHIP_RESET_HW_MASK,
					       1));

	/* Realtek documentation says the chip needs 1 second to reset. Sleep
	 * for 100 ms before accessing any registers to prevent ACK timeouts.
	 */
	msleep(100);
	return regmap_read_poll_timeout(
		smi->map, RTL8365MB_CHIP_RESET_REG, val,
		(val & RTL8365MB_CHIP_RESET_HW_MASK) == 0, 20000, 1e6);
}

static int rtl8365mb_setup(struct dsa_switch *ds)
{
	struct realtek_smi *smi = ds->priv;
	struct rtl8365mb *mb;
	int ret;

	mb = smi->chip_data;

	ret = rtl8365mb_reset_chip(smi);
	if (ret) {
		dev_err(smi->dev, "failed to reset chip: %d\n", ret);
		return ret;
	}

	/* Configure switch to vendor-defined initial state */
	ret = rtl8365mb_switch_init(smi);
	if (ret) {
		dev_err(smi->dev, "failed to initialize switch: %d\n", ret);
		return ret;
	}

	/* Configure CPU tagging */
	ret = rtl8365mb_cpu_config(smi);
	if (ret)
		return ret;

	/* Isolate all ports per DSA setup requirements */
	ret = rtl8365mb_ports_isolate(smi);
	if (ret)
		return ret;

	/* Set maximum packet length to 1536 bytes */
	ret = regmap_update_bits(smi->map, RTL8365MB_CFG0_MAX_LEN_REG,
				 RTL8365MB_CFG0_MAX_LEN_MASK,
				 FIELD_PREP(RTL8365MB_CFG0_MAX_LEN_MASK, 1536));
	if (ret)
		return ret;

	/* Set up cascading IRQs */
	ret = rtl8365mb_irq_setup(smi);
	if (ret == -EPROBE_DEFER)
		return ret;
	else if (ret)
		dev_info(smi->dev, "no interrupt support\n");

	ret = realtek_smi_setup_mdio(smi);
	if (ret) {
		dev_err(smi->dev, "could not set up MDIO bus\n");
		return -ENODEV;
	}

	return ret;
}

static void rtl8365mb_teardown(struct dsa_switch *ds)
{
	struct realtek_smi *smi = ds->priv;

	realtek_smi_teardown_mdio(smi);
	rtl8365mb_irq_teardown(smi);
}

static int rtl8365mb_get_chip_id_and_ver(struct regmap *map, u32 *id, u32 *ver)
{
	int ret;

	/* For some reason we have to write a magic value to an arbitrary
	 * register whenever accessing the chip ID/version registers.
	 */
	ret = regmap_write(map, RTL8365MB_MAGIC_REG, RTL8365MB_MAGIC_VALUE);
	if (ret)
		return ret;

	ret = regmap_read(map, RTL8365MB_CHIP_ID_REG, id);
	if (ret)
		return ret;

	ret = regmap_read(map, RTL8365MB_CHIP_VER_REG, ver);
	if (ret)
		return ret;

	/* Reset magic register */
	ret = regmap_write(map, RTL8365MB_MAGIC_REG, 0);
	if (ret)
		return ret;

	return 0;
}

static int rtl8365mb_detect(struct realtek_smi *smi)
{
	struct rtl8365mb *mb = smi->chip_data;
	u32 chip_id;
	u32 chip_ver;
	int ret;

	ret = rtl8365mb_get_chip_id_and_ver(smi->map, &chip_id, &chip_ver);
	if (ret) {
		dev_err(smi->dev, "failed to read chip id and version: %d\n",
			ret);
		return ret;
	}

	switch (chip_id) {
	case RTL8365MB_CHIP_ID_8365MB_VC:
		dev_info(smi->dev,
			 "found an RTL8365MB-VC switch (ver=0x%04x)\n",
			 chip_ver);

		smi->cpu_port = RTL8365MB_CPU_PORT_NUM_8365MB_VC;
		smi->num_ports = RTL8365MB_NUM_PORTS_8365MB_VC;
		smi->num_vlan_mc = RTL8365MB_NUM_VLANS;
		smi->mib_counters = rtl8365mb_mib_counters;
		smi->num_mib_counters = ARRAY_SIZE(rtl8365mb_mib_counters);

		mb->chip_id = chip_id;
		mb->chip_ver = chip_ver;
		mb->num_phys = RTL8365MB_NUM_PHYS_8365MB_VC;
		mb->port_mask = RTL8365MB_PORT_MASK_8365MB_VC;
		mb->phy_port_mask = RTL8365MB_PHY_PORT_MASK_8365MB_VC;
		mb->jam_table = rtl8365mb_init_jam_8365mb_vc;
		mb->jam_size = ARRAY_SIZE(rtl8365mb_init_jam_8365mb_vc);

		mb->cpu.enable = 1;
		mb->cpu.mask = BIT(smi->cpu_port);
		mb->cpu.trap_port = smi->cpu_port;
		mb->cpu.insert = RTL8365MB_CPU_INSERT_TO_ALL;
		mb->cpu.position = RTL8365MB_CPU_POS_AFTER_SA;
		mb->cpu.rx_length = RTL8365MB_CPU_RXLEN_64BYTES;
		mb->cpu.format = RTL8365MB_CPU_FORMAT_8BYTES;

		break;
	default:
		dev_err(smi->dev,
			"found an unknown Realtek switch (id=0x%04x, ver=0x%04x)\n",
			chip_id, chip_ver);
		return -ENODEV;
	}

	return 0;
}

static const struct dsa_switch_ops rtl8365mb_switch_ops = {
	.get_tag_protocol = rtl8365mb_get_tag_protocol,
	.setup = rtl8365mb_setup,
	.teardown = rtl8365mb_teardown,
	.phylink_validate = rtl8365mb_phylink_validate,
	.phylink_mac_config = rtl8365mb_phylink_mac_config,
	.phylink_mac_link_down = rtl8365mb_phylink_mac_link_down,
	.phylink_mac_link_up = rtl8365mb_phylink_mac_link_up,
	.get_strings = rtl8366_get_strings,
	.get_ethtool_stats = rtl8366_get_ethtool_stats,
	.get_sset_count = rtl8366_get_sset_count,
	.port_vlan_filtering = rtl8365mb_port_vlan_filtering,
	.port_vlan_add = rtl8366_vlan_add,
	.port_vlan_del = rtl8366_vlan_del,
	.port_enable = rtl8365mb_port_enable,
	.port_disable = rtl8365mb_port_disable,
};

static const struct realtek_smi_ops rtl8365mb_smi_ops = {
	.detect = rtl8365mb_detect,
	.get_vlan_mc = rtl8365mb_get_vlan_mc,
	.set_vlan_mc = rtl8365mb_set_vlan_mc,
	.get_vlan_4k = rtl8365mb_get_vlan_4k,
	.set_vlan_4k = rtl8365mb_set_vlan_4k,
	.get_mc_index = rtl8365mb_get_mc_index,
	.set_mc_index = rtl8365mb_set_mc_index,
	.get_mib_counter = rtl8365mb_get_mib_counter,
	.is_vlan_valid = rtl8365mb_is_vlan_valid,
	.enable_vlan = rtl8365mb_enable_vlan,
	.enable_vlan4k = rtl8365mb_enable_vlan4k,
	.phy_read = rtl8365mb_phy_read,
	.phy_write = rtl8365mb_phy_write,
};

const struct realtek_smi_variant rtl8365mb_variant = {
	.ds_ops = &rtl8365mb_switch_ops,
	.ops = &rtl8365mb_smi_ops,
	.clk_delay = 10,
	.cmd_read = 0xb9,
	.cmd_write = 0xb8,
	.chip_data_sz = sizeof(struct rtl8365mb),
};
EXPORT_SYMBOL_GPL(rtl8365mb_variant);
