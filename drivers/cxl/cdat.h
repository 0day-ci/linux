/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Coherent Device Attribute table (CDAT)
 *
 * Specification available from UEFI.org
 *
 * Whilst CDAT is defined as a single table, the access via DOE maiboxes is
 * done one entry at a time, where the first entry is the header.
 */

#define CXL_DOE_TABLE_ACCESS_REQ_CODE		0x000000ff
#define   CXL_DOE_TABLE_ACCESS_REQ_CODE_READ	0
#define CXL_DOE_TABLE_ACCESS_TABLE_TYPE		0x0000ff00
#define   CXL_DOE_TABLE_ACCESS_TABLE_TYPE_CDATA	0
#define CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE	0xffff0000

/*
 * CDAT entries are little endian and are read from PCI config space which
 * is also little endian.
 * As such, on a big endian system these will have been reversed.
 * This prevents us from making easy use of packed structures.
 * Style form pci_regs.h
 */

#define CDAT_HEADER_LENGTH_DW 3
#define CDAT_HEADER_DW0_LENGTH		0xffffffff
#define CDAT_HEADER_DW1_REVISION	0x000000ff
#define CDAT_HEADER_DW1_CHECKSUM	0x0000ff00
#define CDAT_HEADER_DW2_SEQUENCE	0xffffffff

/* All structures have a common first DW */
#define CDAT_STRUCTURE_DW0_TYPE		0x000000ff
#define   CDAT_STRUCTURE_DW0_TYPE_DSMAS 0
#define   CDAT_STRUCTURE_DW0_TYPE_DSLBIS 1
#define   CDAT_STRUCTURE_DW0_TYPE_DSMSCIS 2
#define   CDAT_STRUCTURE_DW0_TYPE_DSIS 3
#define   CDAT_STRUCTURE_DW0_TYPE_DSEMTS 4
#define   CDAT_STRUCTURE_DW0_TYPE_SSLBIS 5

#define CDAT_STRUCTURE_DW0_LENGTH	0xffff0000

/* Device Scoped Memory Affinity Structure */
#define CDAT_DSMAS_DW1_DSMAD_HANDLE	0x000000ff
#define CDAT_DSMAS_DW1_FLAGS		0x0000ff00
#define CDAT_DSMAS_DPA_OFFSET(entry) ((u64)((entry)[3]) << 32 | (entry)[2])
#define CDAT_DSMAS_DPA_LEN(entry) ((u64)((entry)[5]) << 32 | (entry)[4])

/* Device Scoped Latency and Bandwidth Information Structure */
#define CDAT_DSLBIS_DW1_HANDLE		0x000000ff
#define CDAT_DSLBIS_DW1_FLAGS		0x0000ff00
#define CDAT_DSLBIS_DW1_DATA_TYPE	0x00ff0000
#define CDAT_DSLBIS_BASE_UNIT(entry) ((u64)((entry)[3]) << 32 | (entry)[2])
#define CDAT_DSLBIS_DW4_ENTRY_0		0x0000ffff
#define CDAT_DSLBIS_DW4_ENTRY_1		0xffff0000
#define CDAT_DSLBIS_DW5_ENTRY_2		0x0000ffff

/* Device Scoped Memory Side Cache Information Structure */
#define CDAT_DSMSCIS_DW1_HANDLE		0x000000ff
#define CDAT_DSMSCIS_MEMORY_SIDE_CACHE_SIZE(entry) \
	((u64)((entry)[3]) << 32 | (entry)[2])
#define CDAT_DSMSCIS_DW4_MEMORY_SIDE_CACHE_ATTRS 0xffffffff

/* Device Scoped Initiator Structure */
#define CDAT_DSIS_DW1_FLAGS		0x000000ff
#define CDAT_DSIS_DW1_HANDLE		0x0000ff00

/* Device Scoped EFI Memory Type Structure */
#define CDAT_DSEMTS_DW1_HANDLE		0x000000ff
#define CDAT_DSEMTS_DW1_EFI_MEMORY_TYPE_ATTR	0x0000ff00
#define CDAT_DSEMTS_DPA_OFFSET(entry)	((u64)((entry)[3]) << 32 | (entry)[2])
#define CDAT_DSEMTS_DPA_LENGTH(entry)	((u64)((entry)[5]) << 32 | (entry)[4])

/* Switch Scoped Latency and Bandwidth Information Structure */
#define CDAT_SSLBIS_DW1_DATA_TYPE	0x000000ff
#define CDAT_SSLBIS_BASE_UNIT(entry)	((u64)((entry)[3]) << 32 | (entry)[2])
#define CDAT_SSLBIS_ENTRY_PORT_X(entry, i) ((entry)[4 + (i) * 2] & 0x0000ffff)
#define CDAT_SSLBIS_ENTRY_PORT_Y(entry, i) (((entry)[4 + (i) * 2] & 0xffff0000) >> 16)
#define CDAT_SSLBIS_ENTRY_LAT_OR_BW(entry, i) ((entry)[4 + (i) * 2 + 1] & 0x0000ffff)
