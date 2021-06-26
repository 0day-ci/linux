/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * -- <linux/cdrom.h>
 * General header file for linux CD-ROM drivers 
 * Copyright (C) 1992         David Giller, rafetmad@oxy.edu
 *               1994, 1995   Eberhard Mönkeberg, emoenke@gwdg.de
 *               1996         David van Leeuwen, david@tm.tno.nl
 *               1997, 1998   Erik Andersen, andersee@debian.org
 *               1998-2002    Jens Axboe, axboe@suse.de
 */
 
#ifndef _UAPI_LINUX_CDROM_H
#define _UAPI_LINUX_CDROM_H

#include <linux/types.h>
#include <asm/byteorder.h>

/*******************************************************
 * As of Linux 2.1.x, all Linux CD-ROM application programs will use this 
 * (and only this) include file.  It is my hope to provide Linux with
 * a uniform interface between software accessing CD-ROMs and the various 
 * device drivers that actually talk to the drives.  There may still be
 * 23 different kinds of strange CD-ROM drives, but at least there will 
 * now be one, and only one, Linux CD-ROM interface.
 *
 * Additionally, as of Linux 2.1.x, all Linux application programs 
 * should use the O_NONBLOCK option when opening a CD-ROM device 
 * for subsequent ioctl commands.  This allows for neat system errors 
 * like "No medium found" or "Wrong medium type" upon attempting to 
 * mount or play an empty slot, mount an audio disc, or play a data disc.
 * Generally, changing an application program to support O_NONBLOCK
 * is as easy as the following:
 *       -    drive = open("/dev/cdrom", O_RDONLY);
 *       +    drive = open("/dev/cdrom", O_RDONLY | O_NONBLOCK);
 * It is worth the small change.
 *
 *  Patches for many common CD programs (provided by David A. van Leeuwen)
 *  can be found at:  ftp://ftp.gwdg.de/pub/linux/cdrom/drivers/cm206/
 * 
 *******************************************************/

/* When a driver supports a certain function, but the cdrom drive we are 
 * using doesn't, we will return the error EDRIVE_CANT_DO_THIS.  We will 
 * borrow the "Operation not supported" error from the network folks to 
 * accomplish this.  Maybe someday we will get a more targeted error code, 
 * but this will do for now... */
#define EDRIVE_CANT_DO_THIS  EOPNOTSUPP

/*******************************************************
 * The CD-ROM IOCTL commands  -- these should be supported by 
 * all the various cdrom drivers.  For the CD-ROM ioctls, we 
 * will commandeer byte 0x53, or 'S'.
 *******************************************************/
#define CDROMPAUSE		0x5301 /* Pause Audio Operation */ 
#define CDROMRESUME		0x5302 /* Resume paused Audio Operation */
#define CDROMPLAYMSF		0x5303 /* Play Audio MSF (struct cdrom_msf) */
#define CDROMPLAYTRKIND		0x5304 /* Play Audio Track/index 
                                           (struct cdrom_ti) */
#define CDROMREADTOCHDR		0x5305 /* Read TOC header 
                                           (struct cdrom_tochdr) */
#define CDROMREADTOCENTRY	0x5306 /* Read TOC entry 
                                           (struct cdrom_tocentry) */
#define CDROMSTOP		0x5307 /* Stop the cdrom drive */
#define CDROMSTART		0x5308 /* Start the cdrom drive */
#define CDROMEJECT		0x5309 /* Ejects the cdrom media */
#define CDROMVOLCTRL		0x530a /* Control output volume 
                                           (struct cdrom_volctrl) */
#define CDROMSUBCHNL		0x530b /* Read subchannel data 
                                           (struct cdrom_subchnl) */
#define CDROMREADMODE2		0x530c /* Read CDROM mode 2 data (2336 Bytes) 
                                           (struct cdrom_read) */
#define CDROMREADMODE1		0x530d /* Read CDROM mode 1 data (2048 Bytes)
                                           (struct cdrom_read) */
#define CDROMREADAUDIO		0x530e /* (struct cdrom_read_audio) */
#define CDROMEJECT_SW		0x530f /* enable(1)/disable(0) auto-ejecting */
#define CDROMMULTISESSION	0x5310 /* Obtain the start-of-last-session 
                                           address of multi session disks 
                                           (struct cdrom_multisession) */
#define CDROM_GET_MCN		0x5311 /* Obtain the "Universal Product Code" 
                                           if available (struct cdrom_mcn) */
#define CDROM_GET_UPC		CDROM_GET_MCN  /* This one is deprecated, 
                                          but here anyway for compatibility */
#define CDROMRESET		0x5312 /* hard-reset the drive */
#define CDROMVOLREAD		0x5313 /* Get the drive's volume setting 
                                          (struct cdrom_volctrl) */
#define CDROMREADRAW		0x5314	/* read data in raw mode (2352 Bytes)
                                           (struct cdrom_read) */
/* 
 * These ioctls are used only used in aztcd.c and optcd.c
 */
#define CDROMREADCOOKED		0x5315	/* read data in cooked mode */
#define CDROMSEEK		0x5316  /* seek msf address */
  
/*
 * This ioctl is only used by the scsi-cd driver.  
   It is for playing audio in logical block addressing mode.
 */
#define CDROMPLAYBLK		0x5317	/* (struct cdrom_blk) */

/* 
 * These ioctls are only used in optcd.c
 */
#define CDROMREADALL		0x5318	/* read all 2646 bytes */

/* 
 * These ioctls are (now) only in ide-cd.c for controlling 
 * drive spindown time.  They should be implemented in the
 * Uniform driver, via generic packet commands, GPCMD_MODE_SELECT_10,
 * GPCMD_MODE_SENSE_10 and the GPMODE_POWER_PAGE...
 *  -Erik
 */
#define CDROMGETSPINDOWN        0x531d
#define CDROMSETSPINDOWN        0x531e

/* 
 * These ioctls are implemented through the uniform CD-ROM driver
 * They _will_ be adopted by all CD-ROM drivers, when all the CD-ROM
 * drivers are eventually ported to the uniform CD-ROM driver interface.
 */
#define CDROMCLOSETRAY		0x5319	/* pendant of CDROMEJECT */
#define CDROM_SET_OPTIONS	0x5320  /* Set behavior options */
#define CDROM_CLEAR_OPTIONS	0x5321  /* Clear behavior options */
#define CDROM_SELECT_SPEED	0x5322  /* Set the CD-ROM speed */
#define CDROM_SELECT_DISC	0x5323  /* Select disc (for juke-boxes) */
#define CDROM_MEDIA_CHANGED	0x5325  /* Check is media changed  */
#define CDROM_DRIVE_STATUS	0x5326  /* Get tray position, etc. */
#define CDROM_DISC_STATUS	0x5327  /* Get disc type, etc. */
#define CDROM_CHANGER_NSLOTS    0x5328  /* Get number of slots */
#define CDROM_LOCKDOOR		0x5329  /* lock or unlock door */
#define CDROM_DEBUG		0x5330	/* Turn debug messages on/off */
#define CDROM_GET_CAPABILITY	0x5331	/* get capabilities */

/* Note that scsi/scsi_ioctl.h also uses 0x5382 - 0x5386.
 * Future CDROM ioctls should be kept below 0x537F
 */

/* This ioctl is only used by sbpcd at the moment */
#define CDROMAUDIOBUFSIZ        0x5382	/* set the audio buffer size */
					/* conflict with SCSI_IOCTL_GET_IDLUN */

/* DVD-ROM Specific ioctls */
#define DVD_READ_STRUCT		0x5390  /* Read structure */
#define DVD_WRITE_STRUCT	0x5391  /* Write structure */
#define DVD_AUTH		0x5392  /* Authentication */

#define CDROM_SEND_PACKET	0x5393	/* send a packet to the drive */
#define CDROM_NEXT_WRITABLE	0x5394	/* get next writable block */
#define CDROM_LAST_WRITTEN	0x5395	/* get last block written on disc */

/*******************************************************
 * CDROM IOCTL structures
 *******************************************************/

/* Address in MSF format */
struct cdrom_msf0		
{
	__u8	minute;
	__u8	second;
	__u8	frame;
};

/* Address in either MSF or logical format */
union cdrom_addr		
{
	struct cdrom_msf0	msf;
	int			lba;
};

/* This struct is used by the CDROMPLAYMSF ioctl */ 
struct cdrom_msf 
{
	__u8	cdmsf_min0;	/* start minute */
	__u8	cdmsf_sec0;	/* start second */
	__u8	cdmsf_frame0;	/* start frame */
	__u8	cdmsf_min1;	/* end minute */
	__u8	cdmsf_sec1;	/* end second */
	__u8	cdmsf_frame1;	/* end frame */
};

/* This struct is used by the CDROMPLAYTRKIND ioctl */
struct cdrom_ti 
{
	__u8	cdti_trk0;	/* start track */
	__u8	cdti_ind0;	/* start index */
	__u8	cdti_trk1;	/* end track */
	__u8	cdti_ind1;	/* end index */
};

/* This struct is used by the CDROMREADTOCHDR ioctl */
struct cdrom_tochdr 	
{
	__u8	cdth_trk0;	/* start track */
	__u8	cdth_trk1;	/* end track */
};

/* This struct is used by the CDROMVOLCTRL and CDROMVOLREAD ioctls */
struct cdrom_volctrl
{
	__u8	channel0;
	__u8	channel1;
	__u8	channel2;
	__u8	channel3;
};

/* This struct is used by the CDROMSUBCHNL ioctl */
struct cdrom_subchnl 
{
	__u8	cdsc_format;
	__u8	cdsc_audiostatus;
	__u8	cdsc_adr:	4;
	__u8	cdsc_ctrl:	4;
	__u8	cdsc_trk;
	__u8	cdsc_ind;
	union cdrom_addr cdsc_absaddr;
	union cdrom_addr cdsc_reladdr;
};


/* This struct is used by the CDROMREADTOCENTRY ioctl */
struct cdrom_tocentry 
{
	__u8	cdte_track;
	__u8	cdte_adr	:4;
	__u8	cdte_ctrl	:4;
	__u8	cdte_format;
	union cdrom_addr cdte_addr;
	__u8	cdte_datamode;
};

/* This struct is used by the CDROMREADMODE1, and CDROMREADMODE2 ioctls */
struct cdrom_read      
{
	int	cdread_lba;
	char 	*cdread_bufaddr;
	int	cdread_buflen;
};

/* This struct is used by the CDROMREADAUDIO ioctl */
struct cdrom_read_audio
{
	union cdrom_addr addr; /* frame address */
	__u8 addr_format;      /* CDROM_LBA or CDROM_MSF */
	int nframes;           /* number of 2352-byte-frames to read at once */
	__u8 __user *buf;      /* frame buffer (size: nframes*2352 bytes) */
};

/* This struct is used with the CDROMMULTISESSION ioctl */
struct cdrom_multisession
{
	union cdrom_addr addr; /* frame address: start-of-last-session 
	                           (not the new "frame 16"!).  Only valid
	                           if the "xa_flag" is true. */
	__u8 xa_flag;        /* 1: "is XA disk" */
	__u8 addr_format;    /* CDROM_LBA or CDROM_MSF */
};

/* This struct is used with the CDROM_GET_MCN ioctl.  
 * Very few audio discs actually have Universal Product Code information, 
 * which should just be the Medium Catalog Number on the box.  Also note 
 * that the way the codeis written on CD is _not_ uniform across all discs!
 */  
struct cdrom_mcn 
{
  __u8 medium_catalog_number[14]; /* 13 ASCII digits, null-terminated */
};

/* This is used by the CDROMPLAYBLK ioctl */
struct cdrom_blk 
{
	unsigned from;
	unsigned short len;
};

#define CDROM_PACKET_SIZE	12

#define CGC_DATA_UNKNOWN	0
#define CGC_DATA_WRITE		1
#define CGC_DATA_READ		2
#define CGC_DATA_NONE		3

/* for CDROM_PACKET_COMMAND ioctl */
struct cdrom_generic_command
{
	unsigned char 		cmd[CDROM_PACKET_SIZE];
	unsigned char		__user *buffer;
	unsigned int 		buflen;
	int			stat;
	struct request_sense	__user *sense;
	unsigned char		data_direction;
	int			quiet;
	int			timeout;
	union {
		void		__user *reserved[1];	/* unused, actually */
		void            __user *unused;
	};
};

/*
 * A CD-ROM physical sector size is 2048, 2052, 2056, 2324, 2332, 2336, 
 * 2340, or 2352 bytes long.  

*         Sector types of the standard CD-ROM data formats:
 *
 * format   sector type               user data size (bytes)
 * -----------------------------------------------------------------------------
 *   1     (Red Book)    CD-DA          2352    (CD_FRAMESIZE_RAW)
 *   2     (Yellow Book) Mode1 Form1    2048    (CD_FRAMESIZE)
 *   3     (Yellow Book) Mode1 Form2    2336    (CD_FRAMESIZE_RAW0)
 *   4     (Green Book)  Mode2 Form1    2048    (CD_FRAMESIZE)
 *   5     (Green Book)  Mode2 Form2    2328    (2324+4 spare bytes)
 *
 *
 *       The layout of the standard CD-ROM data formats:
 * -----------------------------------------------------------------------------
 * - audio (red):                  | audio_sample_bytes |
 *                                 |        2352        |
 *
 * - data (yellow, mode1):         | sync - head - data - EDC - zero - ECC |
 *                                 |  12  -   4  - 2048 -  4  -   8  - 276 |
 *
 * - data (yellow, mode2):         | sync - head - data |
 *                                 |  12  -   4  - 2336 |
 *
 * - XA data (green, mode2 form1): | sync - head - sub - data - EDC - ECC |
 *                                 |  12  -   4  -  8  - 2048 -  4  - 276 |
 *
 * - XA data (green, mode2 form2): | sync - head - sub - data - Spare |
 *                                 |  12  -   4  -  8  - 2324 -  4    |
 *
 */

/* Some generally useful CD-ROM information -- mostly based on the above */
#define CD_MINS              74 /* max. minutes per CD, not really a limit */
#define CD_SECS              60 /* seconds per minute */
#define CD_FRAMES            75 /* frames per second */
#define CD_SYNC_SIZE         12 /* 12 sync bytes per raw data frame */
#define CD_MSF_OFFSET       150 /* MSF numbering offset of first frame */
#define CD_CHUNK_SIZE        24 /* lowest-level "data bytes piece" */
#define CD_NUM_OF_CHUNKS     98 /* chunks per frame */
#define CD_FRAMESIZE_SUB     96 /* subchannel data "frame" size */
#define CD_HEAD_SIZE          4 /* header (address) bytes per raw data frame */
#define CD_SUBHEAD_SIZE       8 /* subheader bytes per raw XA data frame */
#define CD_EDC_SIZE           4 /* bytes EDC per most raw data frame types */
#define CD_ZERO_SIZE          8 /* bytes zero per yellow book mode 1 frame */
#define CD_ECC_SIZE         276 /* bytes ECC per most raw data frame types */
#define CD_FRAMESIZE       2048 /* bytes per frame, "cooked" mode */
#define CD_FRAMESIZE_RAW   2352 /* bytes per frame, "raw" mode */
#define CD_FRAMESIZE_RAWER 2646 /* The maximum possible returned bytes */ 
/* most drives don't deliver everything: */
#define CD_FRAMESIZE_RAW1 (CD_FRAMESIZE_RAW-CD_SYNC_SIZE) /*2340*/
#define CD_FRAMESIZE_RAW0 (CD_FRAMESIZE_RAW-CD_SYNC_SIZE-CD_HEAD_SIZE) /*2336*/
/* total frames on the specific medium-disk format */
#define CD_MAX_FRAMES			(CD_MINS * CD_SECS * CD_FRAMES)
#define CD_DVD_MAX_FRAMES		(2295104)
#define CD_DVDDL_MAX_FRAMES	(4173824)
#define CD_BD_MAX_FRAMES		(12219392)
#define CD_BDDL_MAX_FRAMES		(24438784)

#define CD_XA_HEAD        (CD_HEAD_SIZE+CD_SUBHEAD_SIZE) /* "before data" part of raw XA frame */
#define CD_XA_TAIL        (CD_EDC_SIZE+CD_ECC_SIZE) /* "after data" part of raw XA frame */
#define CD_XA_SYNC_HEAD   (CD_SYNC_SIZE+CD_XA_HEAD) /* sync bytes + header of XA frame */

/* CD-ROM address types (cdrom_tocentry.cdte_format) */
#define	CDROM_LBA 0x01 /* "logical block": first frame is #0 */
#define	CDROM_MSF 0x02 /* "minute-second-frame": binary, not bcd here! */

/* bit to tell whether track is data or audio (cdrom_tocentry.cdte_ctrl) */
#define	CDROM_DATA_TRACK	0x04

/* The leadout track is always 0xAA, regardless of # of tracks on disc */
#define	CDROM_LEADOUT		0xAA

/* audio states (from SCSI-2, but seen with other drives, too) */
#define	CDROM_AUDIO_INVALID	0x00	/* audio status not supported */
#define	CDROM_AUDIO_PLAY	0x11	/* audio play operation in progress */
#define	CDROM_AUDIO_PAUSED	0x12	/* audio play operation paused */
#define	CDROM_AUDIO_COMPLETED	0x13	/* audio play successfully completed */
#define	CDROM_AUDIO_ERROR	0x14	/* audio play stopped due to error */
#define	CDROM_AUDIO_NO_STATUS	0x15	/* no current audio status to return */

/* capability flags used with the uniform CD-ROM driver */ 
#define CDC_CLOSE_TRAY		0x1     /* caddy systems _can't_ close */
#define CDC_OPEN_TRAY		0x2     /* but _can_ eject.  */
#define CDC_LOCK		0x4     /* disable manual eject */
#define CDC_SELECT_SPEED 	0x8     /* programmable speed */
#define CDC_SELECT_DISC		0x10    /* select disc from juke-box */
#define CDC_MULTI_SESSION 	0x20    /* read sessions>1 */
#define CDC_MCN			0x40    /* Medium Catalog Number */
#define CDC_MEDIA_CHANGED 	0x80    /* media changed */
#define CDC_PLAY_AUDIO		0x100   /* audio functions */
#define CDC_RESET               0x200   /* hard reset device */
#define CDC_DRIVE_STATUS        0x800   /* driver implements drive status */
#define CDC_GENERIC_PACKET	0x1000	/* driver implements generic packets */
#define CDC_CD_R		0x2000	/* drive is a CD-R */
#define CDC_CD_RW		0x4000	/* drive is a CD-RW */
#define CDC_DVD			0x8000	/* drive is a DVD */
#define CDC_DVD_R		0x10000	/* drive can write DVD-R */
#define CDC_DVD_RAM		0x20000	/* drive can write DVD-RAM */
#define CDC_MO_DRIVE		0x40000 /* drive is an MO device */
#define CDC_MRW			0x80000 /* drive can read MRW */
#define CDC_MRW_W		0x100000 /* drive can write MRW */
#define CDC_RAM			0x200000 /* ok to open for WRITE */

/* drive status possibilities returned by CDROM_DRIVE_STATUS ioctl */
#define CDS_NO_INFO		0	/* if not implemented */
#define CDS_NO_DISC		1
#define CDS_TRAY_OPEN		2
#define CDS_DRIVE_NOT_READY	3
#define CDS_DISC_OK		4

/* return values for the CDROM_DISC_STATUS ioctl */
/* can also return CDS_NO_[INFO|DISC], from above */
#define CDS_AUDIO		100
#define CDS_DATA_1		101
#define CDS_DATA_2		102
#define CDS_XA_2_1		103
#define CDS_XA_2_2		104
#define CDS_MIXED		105

/* User-configurable behavior options for the uniform CD-ROM driver */
#define CDO_AUTO_CLOSE		0x1     /* close tray on first open() */
#define CDO_AUTO_EJECT		0x2     /* open tray on last release() */
#define CDO_USE_FFLAGS		0x4     /* use O_NONBLOCK information on open */
#define CDO_LOCK		0x8     /* lock tray on open files */
#define CDO_CHECK_TYPE		0x10    /* check type on open for data */

/* Special codes used when specifying changer slots. */
#define CDSL_NONE       	(INT_MAX-1)
#define CDSL_CURRENT    	INT_MAX

/* For partition based multisession access. IDE can handle 64 partitions
 * per drive - SCSI CD-ROM's use minors to differentiate between the
 * various drives, so we can't do multisessions the same way there.
 * Use the -o session=x option to mount on them.
 */
#define CD_PART_MAX		64
#define CD_PART_MASK		(CD_PART_MAX - 1)

/*********************************************************************
 * Generic Packet commands, MMC commands, and such
 *********************************************************************/

 /* The generic packet command opcodes for CD/DVD Logical Units,
 * From Table 57 of the SFF8090 Ver. 3 (Mt. Fuji) draft standard. */
#define GPCMD_BLANK			    0xa1
#define GPCMD_CLOSE_TRACK		    0x5b
#define GPCMD_FLUSH_CACHE		    0x35
#define GPCMD_FORMAT_UNIT		    0x04
#define GPCMD_GET_CONFIGURATION		    0x46
#define GPCMD_GET_EVENT_STATUS_NOTIFICATION 0x4a
#define GPCMD_GET_PERFORMANCE		    0xac
#define GPCMD_INQUIRY			    0x12
#define GPCMD_LOAD_UNLOAD		    0xa6
#define GPCMD_MECHANISM_STATUS		    0xbd
#define GPCMD_MODE_SELECT_10		    0x55
#define GPCMD_MODE_SENSE_10		    0x5a
#define GPCMD_PAUSE_RESUME		    0x4b
#define GPCMD_PLAY_AUDIO_10		    0x45
#define GPCMD_PLAY_AUDIO_MSF		    0x47
#define GPCMD_PLAY_AUDIO_TI		    0x48
#define GPCMD_PLAY_CD			    0xbc
#define GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL  0x1e
#define GPCMD_READ_10			    0x28
#define GPCMD_READ_12			    0xa8
#define GPCMD_READ_BUFFER		    0x3c
#define GPCMD_READ_BUFFER_CAPACITY	    0x5c
#define GPCMD_READ_CDVD_CAPACITY	    0x25
#define GPCMD_READ_CD			    0xbe
#define GPCMD_READ_CD_MSF		    0xb9
#define GPCMD_READ_DISC_INFO		    0x51
#define GPCMD_READ_DVD_STRUCTURE	    0xad
#define GPCMD_READ_FORMAT_CAPACITIES	    0x23
#define GPCMD_READ_HEADER		    0x44
#define GPCMD_READ_TRACK_RZONE_INFO	    0x52
#define GPCMD_READ_SUBCHANNEL		    0x42
#define GPCMD_READ_TOC_PMA_ATIP		    0x43
#define GPCMD_REPAIR_RZONE_TRACK	    0x58
#define GPCMD_REPORT_KEY		    0xa4
#define GPCMD_REQUEST_SENSE		    0x03
#define GPCMD_RESERVE_RZONE_TRACK	    0x53
#define GPCMD_SEND_CUE_SHEET		    0x5d
#define GPCMD_SCAN			    0xba
#define GPCMD_SEEK			    0x2b
#define GPCMD_SEND_DVD_STRUCTURE	    0xbf
#define GPCMD_SEND_EVENT		    0xa2
#define GPCMD_SEND_KEY			    0xa3
#define GPCMD_SEND_OPC			    0x54
#define GPCMD_SET_READ_AHEAD		    0xa7
#define GPCMD_SET_STREAMING		    0xb6
#define GPCMD_START_STOP_UNIT		    0x1b
#define GPCMD_STOP_PLAY_SCAN		    0x4e
#define GPCMD_TEST_UNIT_READY		    0x00
#define GPCMD_VERIFY_10			    0x2f
#define GPCMD_WRITE_10			    0x2a
#define GPCMD_WRITE_12			    0xaa
#define GPCMD_WRITE_AND_VERIFY_10	    0x2e
#define GPCMD_WRITE_BUFFER		    0x3b
/* This is listed as optional in ATAPI 2.6, but is (curiously) 
 * missing from Mt. Fuji, Table 57.  It _is_ mentioned in Mt. Fuji
 * Table 377 as an MMC command for SCSi devices though...  Most ATAPI
 * drives support it. */
#define GPCMD_SET_SPEED			    0xbb
/* This seems to be a SCSI specific CD-ROM opcode 
 * to play data at track/index */
#define GPCMD_PLAYAUDIO_TI		    0x48
/*
 * From MS Media Status Notification Support Specification. For
 * older drives only.
 */
#define GPCMD_GET_MEDIA_STATUS		    0xda

/* Mode page codes for mode sense/set */
#define GPMODE_VENDOR_PAGE		0x00
#define GPMODE_R_W_ERROR_PAGE		0x01
#define GPMODE_WRITE_PARMS_PAGE		0x05
#define GPMODE_WCACHING_PAGE		0x08
#define GPMODE_AUDIO_CTL_PAGE		0x0e
#define GPMODE_POWER_PAGE		0x1a
#define GPMODE_FAULT_FAIL_PAGE		0x1c
#define GPMODE_TO_PROTECT_PAGE		0x1d
#define GPMODE_CAPABILITIES_PAGE	0x2a
#define GPMODE_ALL_PAGES		0x3f
/* Not in Mt. Fuji, but in ATAPI 2.6 -- deprecated now in favor
 * of MODE_SENSE_POWER_PAGE */
#define GPMODE_CDROM_PAGE		0x0d



/* DVD struct types */
#define DVD_STRUCT_PHYSICAL	0x00
#define DVD_STRUCT_COPYRIGHT	0x01
#define DVD_STRUCT_DISCKEY	0x02
#define DVD_STRUCT_BCA		0x03
#define DVD_STRUCT_MANUFACT	0x04

struct dvd_layer {
	__u8 book_version	: 4;
	__u8 book_type		: 4;
	__u8 min_rate		: 4;
	__u8 disc_size		: 4;
	__u8 layer_type		: 4;
	__u8 track_path		: 1;
	__u8 nlayers		: 2;
	__u8 track_density	: 4;
	__u8 linear_density	: 4;
	__u8 bca		: 1;
	__u32 start_sector;
	__u32 end_sector;
	__u32 end_sector_l0;
};

#define DVD_LAYERS	4

struct dvd_physical {
	__u8 type;
	__u8 layer_num;
	struct dvd_layer layer[DVD_LAYERS];
};

struct dvd_copyright {
	__u8 type;

	__u8 layer_num;
	__u8 cpst;
	__u8 rmi;
};

struct dvd_disckey {
	__u8 type;

	unsigned agid		: 2;
	__u8 value[2048];
};

struct dvd_bca {
	__u8 type;

	int len;
	__u8 value[188];
};

struct dvd_manufact {
	__u8 type;

	__u8 layer_num;
	int len;
	__u8 value[2048];
};

typedef union {
	__u8 type;

	struct dvd_physical	physical;
	struct dvd_copyright	copyright;
	struct dvd_disckey	disckey;
	struct dvd_bca		bca;
	struct dvd_manufact	manufact;
} dvd_struct;

/*
 * DVD authentication ioctl
 */

/* Authentication states */
#define DVD_LU_SEND_AGID	0
#define DVD_HOST_SEND_CHALLENGE	1
#define DVD_LU_SEND_KEY1	2
#define DVD_LU_SEND_CHALLENGE	3
#define DVD_HOST_SEND_KEY2	4

/* Termination states */
#define DVD_AUTH_ESTABLISHED	5
#define DVD_AUTH_FAILURE	6

/* Other functions */
#define DVD_LU_SEND_TITLE_KEY	7
#define DVD_LU_SEND_ASF		8
#define DVD_INVALIDATE_AGID	9
#define DVD_LU_SEND_RPC_STATE	10
#define DVD_HOST_SEND_RPC_STATE	11

/* State data */
typedef __u8 dvd_key[5];		/* 40-bit value, MSB is first elem. */
typedef __u8 dvd_challenge[10];	/* 80-bit value, MSB is first elem. */

struct dvd_lu_send_agid {
	__u8 type;
	unsigned agid		: 2;
};

struct dvd_host_send_challenge {
	__u8 type;
	unsigned agid		: 2;

	dvd_challenge chal;
};

struct dvd_send_key {
	__u8 type;
	unsigned agid		: 2;

	dvd_key key;
};

struct dvd_lu_send_challenge {
	__u8 type;
	unsigned agid		: 2;

	dvd_challenge chal;
};

#define DVD_CPM_NO_COPYRIGHT	0
#define DVD_CPM_COPYRIGHTED	1

#define DVD_CP_SEC_NONE		0
#define DVD_CP_SEC_EXIST	1

#define DVD_CGMS_UNRESTRICTED	0
#define DVD_CGMS_SINGLE		2
#define DVD_CGMS_RESTRICTED	3

struct dvd_lu_send_title_key {
	__u8 type;
	unsigned agid		: 2;

	dvd_key title_key;
	int lba;
	unsigned cpm		: 1;
	unsigned cp_sec		: 1;
	unsigned cgms		: 2;
};

struct dvd_lu_send_asf {
	__u8 type;
	unsigned agid		: 2;

	unsigned asf		: 1;
};

struct dvd_host_send_rpcstate {
	__u8 type;
	__u8 pdrc;
};

struct dvd_lu_send_rpcstate {
	__u8 type		: 2;
	__u8 vra		: 3;
	__u8 ucca		: 3;
	__u8 region_mask;
	__u8 rpc_scheme;
};

typedef union {
	__u8 type;

	struct dvd_lu_send_agid		lsa;
	struct dvd_host_send_challenge	hsc;
	struct dvd_send_key		lsk;
	struct dvd_lu_send_challenge	lsc;
	struct dvd_send_key		hsk;
	struct dvd_lu_send_title_key	lstk;
	struct dvd_lu_send_asf		lsasf;
	struct dvd_host_send_rpcstate	hrpcs;
	struct dvd_lu_send_rpcstate	lrpcs;
} dvd_authinfo;

struct request_sense {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 valid		: 1;
	__u8 error_code		: 7;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 error_code		: 7;
	__u8 valid		: 1;
#endif
	__u8 segment_number;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1		: 2;
	__u8 ili		: 1;
	__u8 reserved2		: 1;
	__u8 sense_key		: 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 sense_key		: 4;
	__u8 reserved2		: 1;
	__u8 ili		: 1;
	__u8 reserved1		: 2;
#endif
	__u8 information[4];
	__u8 add_sense_len;
	__u8 command_info[4];
	__u8 asc;
	__u8 ascq;
	__u8 fruc;
	__u8 sks[3];
	__u8 asb[46];
};

/*
 * feature profile
 */
#define CDF_RWRT	0x0020	/* "Random Writable" */
#define CDF_HWDM	0x0024	/* "Hardware Defect Management" */
#define CDF_MRW 	0x0028

/*
 * media status bits
 */
#define CDM_MRW_NOTMRW			0
#define CDM_MRW_BGFORMAT_INACTIVE	1
#define CDM_MRW_BGFORMAT_ACTIVE		2
#define CDM_MRW_BGFORMAT_COMPLETE	3

/*
 * mrw address spaces
 */
#define MRW_LBA_DMA			0
#define MRW_LBA_GAA			1

/*
 * mrw mode pages (first is deprecated) -- probed at init time and
 * cdi->mrw_mode_page is set
 */
#define MRW_MODE_PC_PRE1		0x2c
#define MRW_MODE_PC			0x03

struct mrw_feature_desc {
	__be16 feature_code;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1		: 2;
	__u8 feature_version	: 4;
	__u8 persistent		: 1;
	__u8 curr		: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 curr		: 1;
	__u8 persistent		: 1;
	__u8 feature_version	: 4;
	__u8 reserved1		: 2;
#endif
	__u8 add_len;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2		: 7;
	__u8 write		: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 write		: 1;
	__u8 reserved2		: 7;
#endif
	__u8 reserved3;
	__u8 reserved4;
	__u8 reserved5;
};

/* cf. mmc4r02g.pdf 5.3.10 Random Writable Feature (0020h) pg 197 of 635 */
struct rwrt_feature_desc {
	__be16 feature_code;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1		: 2;
	__u8 feature_version	: 4;
	__u8 persistent		: 1;
	__u8 curr		: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 curr		: 1;
	__u8 persistent		: 1;
	__u8 feature_version	: 4;
	__u8 reserved1		: 2;
#endif
	__u8 add_len;
	__u32 last_lba;
	__u32 block_size;
	__u16 blocking;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2		: 7;
	__u8 page_present	: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 page_present	: 1;
	__u8 reserved2		: 7;
#endif
	__u8 reserved3;
};

typedef struct {
	__be16 disc_information_length;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1			: 3;
        __u8 erasable			: 1;
        __u8 border_status		: 2;
        __u8 disc_status		: 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
        __u8 disc_status		: 2;
        __u8 border_status		: 2;
        __u8 erasable			: 1;
	__u8 reserved1			: 3;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__u8 n_first_track;
	__u8 n_sessions_lsb;
	__u8 first_track_lsb;
	__u8 last_track_lsb;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 did_v			: 1;
        __u8 dbc_v			: 1;
        __u8 uru			: 1;
        __u8 reserved2			: 2;
	__u8 dbit			: 1;
	__u8 mrw_status			: 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 mrw_status			: 2;
	__u8 dbit			: 1;
        __u8 reserved2			: 2;
        __u8 uru			: 1;
        __u8 dbc_v			: 1;
	__u8 did_v			: 1;
#endif
	__u8 disc_type;
	__u8 n_sessions_msb;
	__u8 first_track_msb;
	__u8 last_track_msb;
	__u32 disc_id;
	__u32 lead_in;
	__u32 lead_out;
	__u8 disc_bar_code[8];
	__u8 reserved3;
	__u8 n_opc;
} disc_information;

typedef struct {
	__be16 track_information_length;
	__u8 track_lsb;
	__u8 session_lsb;
	__u8 reserved1;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2			: 2;
        __u8 damage			: 1;
        __u8 copy			: 1;
        __u8 track_mode			: 4;
	__u8 rt				: 1;
	__u8 blank			: 1;
	__u8 packet			: 1;
	__u8 fp				: 1;
	__u8 data_mode			: 4;
	__u8 reserved3			: 6;
	__u8 lra_v			: 1;
	__u8 nwa_v			: 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
        __u8 track_mode			: 4;
        __u8 copy			: 1;
        __u8 damage			: 1;
	__u8 reserved2			: 2;
	__u8 data_mode			: 4;
	__u8 fp				: 1;
	__u8 packet			: 1;
	__u8 blank			: 1;
	__u8 rt				: 1;
	__u8 nwa_v			: 1;
	__u8 lra_v			: 1;
	__u8 reserved3			: 6;
#endif
	__be32 track_start;
	__be32 next_writable;
	__be32 free_blocks;
	__be32 fixed_packet_size;
	__be32 track_size;
	__be32 last_rec_address;
} track_information;

/* CDB Get Configuration command */

/**
 * The Drive shall return the Feature Header and all Feature Descriptors supported by the
 * Drive without regard to currency
 */
#define CDR_CFG_RT_FULL 0x00
/**
 * The Drive shall return the Feature Header and only those Feature Descriptors in which
 * the Current bit is set to one.
 */
#define CDR_CFG_RT_CURRENT 0x01
/**
 * The Feature Header and the Feature Descriptor identified by Starting Feature Number
 * shall be returned. If the Drive does not support the specified feature, only the Feature
 * Header shall be returned.
 */
#define CDR_CFG_RT_SPECIFIED_SFN 0x02
#define CDR_CFG_RT_RESERVED 0x03

/**
 * @brief GET CONFIGURATION Command
 * The GET CONFIGURATION command provides a Host with information about Drive capabilities;
 * both current and potential.
 *
 * @note The command shall not return a CHECK CONDITION Status due to a pending
 * UNIT ATTENTION Condition.
 */
struct cdb_get_configuration {
	__u8 code;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1 : 6;
	/* The RT field identifies the type of data to be returned by the Drive */
	__u8 rt : 2;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 rt : 2;
	__u8 reserved1 : 6;
#endif
	/**
	 * The Starting Feature Number field indicates the first Feature number to be returned.
	 * All supported Feature numbers higher than the Starting Feature Number shall be
	 * returned.
	 */
	__be16 sfn;
	__u8 reserved2[3];
	/**
	 * The Allocation Length field specifies the maximum length in bytes of the
	 * Get Configuration response data. An Allocation Length field of zero indicates that no
	 * data shall be transferred
	 */
	__be16 length;
	__u8 control;

} __packed;

/* Features */

/* Feature and Profile Descriptors*/

/**
 * @brief The Version, Persisten and Current byte.
 * This structure is required for many CDB features.
 */
struct cdb_ft_vpc_byte {
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1 : 2;
	/**
	 * The Version field is reserved and shall be set to zero unless otherwise specified
	 * within the Feature Description
	 */
	__u8 ver : 4;
	/**
	 * The Persistent bit, when set to zero, shall indicate that this Feature may change
	 * its current status.
	 * When set to one, shall indicate that this Feature is always active.
	 * The Drive shall not set this bit to one if the Current bit is, or may become, zero.
	 */
	__u8 per : 1;
	/**
	 * The Current bit, when set to zero, indicates that this Feature is not currently
	 * active and that the Feature Dependent Data may not be valid.
	 * When set to one, this Feature is currently active and the Feature Dependent Data is
	 * valid.
	 */
	__u8 cur : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 cur : 1;
	__u8 per : 1;
	__u8 ver : 4;
	__u8 reserved1 : 2;
#endif
} __packed;

/**
 * @brief Feature Descriptor generic
 * A Feature Descriptor shall describe each Feature supported by a Drive. All
 * Feature descriptors shall be a multiple of four bytes
 */
struct cdb_ft_generic {
	/**
	 * The Feature Code field shall identify a Feature supported by the Drive
	 */
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	/**
	 * The Additional Length field indicates the number of Feature specific
	 * bytes that follow this header. This field shall be an integral multiple
	 * of 4
	 */
	__u8 length;
} __packed;

/* Profile list */
#define MMC_PROFILE_NONE 0x0000
#define MMC_PROFILE_CD_ROM 0x0008
#define MMC_PROFILE_CD_R 0x0009
#define MMC_PROFILE_CD_RW 0x000A
#define MMC_PROFILE_DVD_ROM 0x0010
#define MMC_PROFILE_DVD_R_SR 0x0011
#define MMC_PROFILE_DVD_RAM 0x0012
#define MMC_PROFILE_DVD_RW_RO 0x0013
#define MMC_PROFILE_DVD_RW_SR 0x0014
#define MMC_PROFILE_DVD_R_DL_SR 0x0015
#define MMC_PROFILE_DVD_R_DL_JR 0x0016
#define MMC_PROFILE_DVD_RW_DL 0x0017
#define MMC_PROFILE_DVD_DDR 0x0018
#define MMC_PROFILE_DVD_PLUS_RW 0x001A
#define MMC_PROFILE_DVD_PLUS_R 0x001B
#define MMC_PROFILE_DVD_PLUS_RW_DL 0x002A
#define MMC_PROFILE_DVD_PLUS_R_DL 0x002B
#define MMC_PROFILE_BD_ROM 0x0040
#define MMC_PROFILE_BD_R_SRM 0x0041
#define MMC_PROFILE_BD_R_RRM 0x0042
#define MMC_PROFILE_BD_RE 0x0043
#define MMC_PROFILE_HDDVD_ROM 0x0050
#define MMC_PROFILE_HDDVD_R 0x0051
#define MMC_PROFILE_HDDVD_RAM 0x0052
#define MMC_PROFILE_HDDVD_RW 0x0053
#define MMC_PROFILE_HDDVD_R_DL 0x0058
#define MMC_PROFILE_HDDVD_RW_DL 0x005A
#define MMC_PROFILE_INVALID 0xFFFF

/**
 * @brief The CDB Feature Header
 * Response data consists of a header field and zero or more variable length
 * Feature descriptors
 */
struct feature_header {
	/**
	 * The Data Length field indicates the amount of data available given a
	 * sufficient allocation length following this field.
	 * This length shall not be truncated due to an insufficient Allocation
	 * Length
	 */
	__u32 data_len;
	__u8 reserved1;
	__u8 reserved2;
	/**
	 * The Current Profile field shall identify one of the profiles from the
	 * Profile List Feature. If there are no Profiles currently active, this
	 * field shall contain zero.
	 */
	__u16 curr_profile;
} __packed;

struct mode_page_header {
	__be16 mode_data_length;
	__u8 medium_type;
	__u8 reserved1;
	__u8 reserved2;
	__u8 reserved3;
	__be16 desc_length;
};

/**
 * @brief Profile descriptors are returned in the order of preferred
 * operation – most desirable to least desirable. e.g., a DVD-ROM
 * that is also able to read a CD-ROM should list the DVD-ROM
 * Profile first and the CD-ROM Profile second.
 */
struct mmc_profile {
	/* The Profile Number identifies a Profile */
	__be16 profile;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1 : 7;
	/**
	 * The current_p bit, when set to one, shall indicate that this
	 * Profile is currently active.
	 */
	__u8 current_p : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 current_p : 1;
	__u8 reserved1 : 7;
#endif

	__u8 reserved2;
} __packed;

/**
 * @brief Profile List Feature (0000h)
 *
 * This Feature identifies Profiles supported by the Drive.
 * Profiles are defined as collections of Features and provide a method
 * to quickly determine the Drive’s type.
 */
struct cdf_profile_list {
	/* The Feature Code */
	__be16 code;

	struct cdb_ft_vpc_byte vpc;
	/**
	 * The Additional Length field shall be set
	 * to ((number of Profile Descriptors) * 4).
	 */
	__u8 length;
} __packed;

/**
 * @brief The core feature: phisycal interface standards
 */
enum cdf_cf_pis {
	CF_PIS_UNSPECIFIED = 0x00000000U,
	CF_PIS_SCSI_FAMILY,
	CF_PIS_ATAPI,
	CF_PIS_IEEE_1394_1995,
	CF_PIS_IEEE_1394A,
	CF_PIS_FIBRE_CHANNEL,
	CF_PIS_IEEE_1394_B,
	CF_PIS_USB,
	CF_PIS_RESERVED,
	CF_PIS_DEF_INCITS = 0x00010000U,
	CF_PIS_DEF_SFF = 0x00020000U,
	CF_PIS_DEF_IEEE = 0x00030000U,
	CF_PIS_DEF_RESERVED = 0x00040000U
};

/**
 * @brief Core Feature (0001h)
 * This Feature identifies a Drive that supports functionality common
 * to all devices.
 */
struct cdf_core {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	/* The Additional Length field shall be set to 8. */
	__u8 length;
	/**
	 * The Physical Interface Standard field shall be set to a value
	 * selected from @enum cdf_cf_pis
	 * It is possible that more than one physical interface exists
	 * between the Host and Drive, e.g., an IEEE1394 Host connecting
	 * to an ATAPI bridge to an ATAPI Drive. The Drive may not be aware
	 * of interfaces beyond the ATAPI.
	 */
	__be32 interface;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 6;
	/**
	 * The INQ2 bit permits the Drive to indicate support for certain
	 * features of the INQUIRY command. If INQ2 is set to one, the
	 * Drive shall support validation of EVPD, Page Code, and the
	 * 16-bit Allocation Length fields
	 */
	__u8 inq2 : 1;
	/**
	 * DBE (Device Busy Event) shall be set to one.
	 */
	__u8 dbevent : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 dbevent : 1;
	__u8 inq2 : 1;
	__u8 reserved2 : 6;
#endif

	__u8 reserved3[3];
} __packed;

/**
 * @brief Morphing Feature (0002h)
 * This Feature identifies the ability of the Drive to notify
 * A Host about operational changes and accept Host requests to
 * prevent operational changes.
 */
struct cdf_morphing {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 6;
	__u8 ocevent : 1;
	__u8 async : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 async : 1;
	__u8 ocevent : 1;
	__u8 reserved2 : 6;
#endif

	__u8 reserved3[3];
} __packed;

/**
 * @brief Removable Medium: Loading Mechanism Types
 */
enum cdf_removable_media_lmt {
	CDF_LMT__CADDY_SLOT_TYPE,
	CDF_LMT__TRAY_TYPE,
	CDF_LMT__POP_UP_TYPE,
	CDF_LMT__RESERVED1,
	CDF_LMT__EMBEDDED_INDIVIDUALLY,
	CDF_LMT__EMBEDDED_MAGAZINE,
	CDF_LMT__RESERVED2,
};

/**
 * @brief Removable Medium Feature (0003h)
 *
 * This Feature identifies a Drive that has a medium that is removable.
 * Media shall be considered removable if it is possible to remove it
 * from the loaded position, i.e., a single mechanism changer, even if
 * the media is captive to the changer.
 *
 * The Drive shall generate Events for media changes.
 * Event Notification Class 4 (Media Events) shall be supported. This
 * includes reporting user requests to load/eject the medium.
 */
struct cdf_removable_medium {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;
	/* The Additional Length field shall be set to 4. */
	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	/**
	 * The Loading Mechanism Type field shall be set according to
	 * @enum cdf_removable_media_lmt
	 */
	__u8 mechanism : 3;
	/**
	 * If the Load bit is set to zero, the Drive is unable to load
	 * the medium or cartridge via the START STOP UNIT command with
	 * the LoEj bit set to one, e.g. the tray type loading mechanism
	 * that is found in many portable PCs.
	 * If the Load bit is set to one, the Drive is able to load the
	 * medium or cartridge.
	 */
	__u8 load : 1;
	/**
	 * The Eject bit, when set to zero, indicates that the device is
	 * unable to eject the medium or magazine via the normal START STOP UNIT
	 * command with the LoEj bit set.
	 * When set to one, indicates that the device is able to eject
	 * the medium or magazine.
	 */
	__u8 eject : 1;
	/**
	 * The Pvnt Jmpr bit, when set to zero, shall indicate that the
	 * Prevent Jumper is present.
	 * When set to one, the Prevent Jumper is not present.
	 * The Pvnt Jmpr bit shall not change state, even if the physical
	 * jumper is added or removed during operation.
	 */
	__u8 prvnt_jmp : 1;
	__u8 reserved2 : 1;
	/**
	 * If Lock is set to zero, there is no locking mechanism for locking
	 * the medium into the Drive. If Lock is set to one, the Drive is
	 * capable of locking the media into the Drive.
	 */
	__u8 lock : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 lock : 1;
	__u8 reserved2 : 1;
	__u8 prvnt_jmp : 1;
	__u8 eject : 1;
	__u8 load : 1;
	__u8 mechanism : 3;
#endif

	__u8 reserved3[3];
} __packed;

/**
 * @brief Random Readable Feature (0010h)
 *
 * This Feature identifies a Drive that is able to read data from logical
 * blocks referenced by Logical Block Addresses, but not requiring that
 * either the addresses or the read sequences occur in any particular order.
 */
struct cdf_random_readable {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;
	/* The Additional Length field shall be set to 8. */
	__u8 length;
	/**
	 * The Logical Block Size shall be set to the number of bytes per
	 * logical block.
	 */
	__be32 block_size;
	/**
	 * The Blocking field shall indicate the number of logical blocks per
	 * device readable unit.
	 * If there is more than one Blocking on the medium possible,
	 * the Blocking field shall be set to zero.
	 */
	__be16 blocking;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 7;
	/**
	 * The PP (Page Present) bit, when set to zero, shall indicate that
	 * the Read/Write Error Recovery mode page may not be present.
	 * When set to one, shall indicate that the Read/Write Error Recovery
	 * mode page is present.
	 */
	__u8 pp : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 pp : 1;
	__u8 reserved2 : 7;
#endif

	__u8 reserved3;
} __packed;

/*
 * Multi-read Feature (001Dh)
 * The Drive shall conform to the OSTA Multi-Read
 * specification 1.00, with the exception of CD Play
 * capability (the CD Audio Feature is not required).
 */
struct cdf_multi_read {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;
} __packed;

/*
 * CD Read Feature (001Eh)
 * This Feature identifies a Drive that is able to read
 * CD specific information from the media and is able
 * to read user data from all types of CD sectors.
 */
struct cdf_cd_read {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;
#if defined(__BIG_ENDIAN_BITFIELD)

	/*
	 * If DAP is set to one, the READ CD and READ CD MSF
	 * commands support the DAP bit in bit 1, byte 1
	 * of the CDB.
	 */
	__u8 dap : 1;
	__u8 reserved2 : 5;

	/*
	 * The C2 Flags, when set to one, indicates the Drive
	 * supports the C2 Error Pointers.
	 * When set to zero the Drive does not support
	 * C2 Error Pointers.
	 */
	__u8 c2flags : 1;
	/*
	 * The CD-Text bit, when set to one, indicates the Drive
	 * supports Format Code 5h of the READ TOC/PMA/ATIP
	 * command.
	 * When set to zero, CD-Text is not supported.
	 */
	__u8 cdtext : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 cdtext : 1;
	__u8 c2flags : 1;
	__u8 reserved2 : 5;
	__u8 dap : 1;
#endif

	__u8 reserved3[3];
} __packed;

/*
 * DVD Read Feature (001Fh)
 * This Feature identifies a Drive that is able to read DVD
 * specific information from the media.
 */
struct cdf_dvd_read {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 7;
	/*
	 * If MULTI110 is set to one, the Drive shall
	 * be compliant with the DVD Multi Drive Read-only
	 * specifications as defined in [DVD-Ref8].
	 */
	__u8 multi110 : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 multi110 : 1;
	__u8 reserved2 : 7;
#endif
	__u8 reserved3;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved4 : 6;
	/*
	 * If the DVD-RW Dual Layer (Dual-RW) bit is set to one,
	 * the Drive is able to read DVD-RW DL media that has the
	 * Complete state.
	 * If the Dual-RW bit is set to zero, the Drive is unable
	 * to read the DVD-RW DL media.
	 */
	__u8 dualrw : 1;
	/*
	 * If the DVD-R Dual Layer (Dual-R) bit is set to one,
	 * the Drive shall support reading all recording modes
	 * (i.e., Sequential recording and Layer Jump recording modes)
	 * of DVD-R DL discs.
	 * The Drive shall support Remapping on DVD-R DL discs.
	 */
	__u8 dualr : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 dualr : 1;
	__u8 dualrw : 1;
	__u8 reserved4 : 6;
#endif

	__u8 reserved5;
} __packed;

/**
 * @brief DVD+R Feature (002Bh)
 * The presence of the DVD+R Feature indicates that the Drive is
 * capable of reading a recorded DVD+R disc that is written according
 * to [DVD+Ref1].
 * Specifically, this includes the capability of reading DCBs.
 */
struct cdf_dvd_plus_r {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 7;
	/**
	 * If the Write bit is set to one, then the Drive is also capable
	 * of writing DVD+R discs according to [DVD+Ref1].
	 */
	__u8 write : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 write : 1;
	__u8 reserved2 : 7;
#endif

	__u8 reserved3[3];
} __packed;

/**
 * @brief CD Track at Once Feature (002Dh)
 * This Feature identifies a Drive that is able to write data to
 * a CD track.
 */
struct cdf_cd_track_at_once {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 1;
	/**
	 * The BUF bit, if set to 1, shall indicate that the Drive
	 * is capable of zero loss linking.
	 */
	__u8 buf : 1;
	__u8 reserved1 : 1;
	/**
	 * The R-W Raw bit, if set to 1, shall indicate that the Drive
	 * supports writing R-W Sub code in the Raw mode.
	 * The R-W Sub-code bit shall be set if this bit is set.
	 */
	__u8 rw_raw : 1;
	/**
	 * The R-W Pack bit, if set to 1, shall indicate that the Drive
	 * supports writing R-W Sub code in the Packed mode.
	 * The R-W Sub-code bit shall be set if this bit is set.
	 */
	__u8 rw_pack : 1;
	/**
	 * The Test Write bit indicates that the Drive is able to
	 * perform test writes.
	 */
	__u8 test_write : 1;
	/**
	 * The CD-RW bit indicates support for overwriting a Track at
	 * Once track with another.
	 */
	__u8 cd_rw : 1;
	/**
	 * The R-W Sub-code bit indicates that the Drive is able to
	 * record the R-W Sub-channels with user supplied data.
	 */
	__u8 rw_subcode : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 rw_subcode : 1;
	__u8 cd_rw : 1;
	__u8 test_write : 1;
	__u8 rw_pack : 1;
	__u8 rw_raw : 1;
	__u8 reserved3 : 1;
	__u8 buf : 1;
	__u8 reserved2 : 1;
#endif

	__u8 reserved4;
	/**
	 * The data type references to the
	 * "Incremental Streaming Writable Feature"
	 */
	__be16 data_type_supported;
} __packed;

/**
 * @brief BD Read Feature (0040h)
 * This Feature identifies a Drive that is able to read control
 * structures and user data from the BD disc.
 */
struct cdf_bd_read {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

	__u8 reserved2[4];
	/**
	 * If the Version K bit (K = 0...15) of the Class M (M = 0...3)
	 * bit map is set to zero, the Drive claims no read capabilities
	 * for BD-R(E)(ROM) discs of Class M and Version K.
	 * If the Version K bit of Class M is set to one, the Drive is
	 * able to read BD-RE discs of class M and Version K.
	 *
	 */

	/* Class M (M = 0..3) BD-RE Read Support */
	__be16 class0_bdre_read_support;
	__be16 class1_bdre_read_support;
	__be16 class2_bdre_read_support;
	__be16 class3_bdre_read_support;
	/* Class M (M = 0..3) BD-R Read Support */
	__be16 class0_bdr_read_support;
	__be16 class1_bdr_read_support;
	__be16 class2_bdr_read_support;
	__be16 class3_bdr_read_support;
	/* Class M (M = 0..3) BD-ROM Read Support */
	__be16 class0_bdrom_read_support;
	__be16 class1_bdrom_read_support;
	__be16 class2_bdrom_read_support;
	__be16 class3_bdrom_read_support;
} __packed;

/**
 * @brief Power Management Feature (0100h)
 * This Feature identifies a Drive that is able to perform Host and
 * Drive directed power management.
 */
struct cdf_power_mgmt {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;
} __packed;

/**
 * @brief Real Time Streaming Feature (0107h)
 * This Feature identifies a Drive that is able to perform reading
 * and writing within Host specified (and Drive verified) performance
 * ranges. This Feature also indicates whether the Drive supports the
 * Stream playback operation.
 */
struct cdf_rt_streaming {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	__u8 length;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved2 : 3;
	/**
	 * The Read Buffer Capacity Block (RBCB) bit indicates that the
	 * Drive supports the READ_BUFFER_CAPACITY command and its Block
	 * bit.
	 */
	__u8 rbcb : 1;
	/**
	 * The Set CD Speed (SCS) bit of one indicates that the Drive
	 * supports the SET_CD_SPEED command. Otherwise, the Drive does not
	 * support the SET_CD_SPEED command.
	 */
	__u8 scs : 1;
	/**
	 * The mode page 2A (MP2A) bit of one indicates that the MM
	 * Capabilities & Mechanical Status mode page (2Ah) with the Drive
	 * Write Speed Performance Descriptor Blocks is supported.
	 * Otherwise, the MM Capabilities & Mechanical Status mode
	 * page (2Ah), with the Drive Write Speed Performance Descriptor
	 * Blocks are not supported by the Drive.
	 */
	__u8 mp2a : 1;
	/**
	 * A Write Speed Performance Descriptor (WSPD) bit of one indicates
	 * that the Drive supports the Write Speed (Type field = 03h) data
	 * of GET PERFORMANCE command and the WRC field of SET STREAMING
	 * command. This bit shall be set to one, if Drive supports writing
	 * speed selection.
	 */
	__u8 wspd : 1;
	/**
	 * A Stream Writing (SW) bit of one indicates that the Drive
	 * supports the Stream recording operation. A SW bit of zero
	 * indicates that the Drive may not support the Stream recording
	 * operation.
	 */
	__u8 sw : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 sw : 1;
	__u8 wspd : 1;
	__u8 mp2a : 1;
	__u8 scs : 1;
	__u8 rbcb : 1;
	__u8 reserved2 : 3;
#endif

	__u8 reserved3[3];
} __packed;

/**
 * @brief Disc Control Blocks (DCBs) Feature (010Ah)
 *
 * This Feature identifies a Drive that is able to read and/or write
 * DCBs from or to the media.
 */
struct cdf_dcbs {
	__be16 code;

	struct cdb_ft_vpc_byte vpc;

	/**
	 * The Additional Length field shall be set to N * 4, where n is
	 * the number of Supported DCB entries. The Supported DCB entry
	 * n fields shall each contain the Content Descriptor of a
	 * supported DCB.
	 * Entries shall be sorted in ascending order.
	 */
	__u8 length;

	/**
	 * Non supported read and/or write the DCBs blocks.
	 */
	__be32 supported_dcb_entry[0];
};

/*
 * feature codes list
 */

/* A list of all Profiles supported by the Drive*/
#define CDF_PROFILE_LIST_CODE 0x0000
/* Mandatory behavior for all devices */
#define CDF_CORE 0x0001

#define CDF_MORPHING_CODE 0x0002
/* The medium may be removed from the device */
#define CDF_REMOVEBLE_MEDIA 0x0003
#define CDF_RANDOM_READ 0x0010
/* The Drive is able to read all CD media types; based on OSTA MultiRead */
#define CDF_MULTI_READ 0x001D
/* The ability to read CD specific structures */
#define CDF_CD_READ 0x001E
/* The ability to read DVD specific structures*/
#define CDF_DVD_READ 0x001F
/* Write support for randomly addressed writes */
#define CDF_RWRT 0x0020
/* Write support for sequential recording */
#define CDF_INC_STREAM_WR 0x0021
/* Hardware Defect Management */
#define CDF_HWDM 0x0024
/* The ability to recognize and read and optionally write MRW formatted media */
#define CDF_MRW 0x0028
/* The ability to read DVD+R recorded media formats */
#define CDF_DVD_R 0x002B
/* Ability to write CD with Track at Once recording */
#define CDF_CD_TRACK_ONCE 0x002D
/* The ability to read control structures and user data from a BD disc */
#define CDF_BD_READ 0x0040
/* The ability to write control structures and user data to certain BD discs */
#define CDF_BD_WRITE 0x0041
/* Host and device directed power management */
#define CDF_POWER_MGMT 0x0100
/* Ability to perform DVD CSS/CPPM authentication and RPC */
#define CDF_DVD_CSS 0x0106
/* Ability to read and write using Host requested performance parameters */
#define CDF_REAL_TIME_STREAM 0x0107
/* The ability to read and/or write DCBs*/
#define CDF_DCBS 0x010A

/**
 * The READ TOC/PMA/ATIP format field values
 */
enum cdb_read_tpa_format {
	/**
	 * The Track/Session Number field specifies starting track number
	 * for which the data is returned. For multi-session discs, TOC
	 * data is returned for all sessions. Track number AAh is reported
	 * only for the Lead-out area of the last complete session.
	 */
	CDB_TPA_FORMATTED_TOC,
	/**
	 * This format returns the first complete session number, last
	 * complete session number and last complete session starting address.
	 * In this format, the Track/Session Number field is reserved and
	 * should be set to 00h.
	 * NOTE: This format provides the Host access to the last closed
	 * session starting address quickly.
	 */
	CDB_TPA_MULTI_SESS_INFO,
	/**
	 * This format returns all Q sub-code data in the Lead-In (TOC) areas
	 * starting from a session number as specified in the Number
	 * Track/Session Number field.
	 * In this mode, the Drive shall support Q Sub-channel POINT field
	 * value of A0h, A1h, A2h, Track numbers, B0h, B1h, B2h, B3h, B4h, C0h,
	 * and C1h.
	 * There is no defined LBA addressing and MSF bit shall be set to one.
	 */
	CDB_TPA_RAW_TOC,
	/**
	 * This format returns Q sub-channel data in the PMA area. In this
	 * format, the Track/Session Number field is reserved and shall be
	 * set to 00h. There is no defined LBA addressing and MSF bit
	 * shall be set to one.
	 */
	CDB_TPA_PMA,
	/**
	 * This format returns ATIP data. In this format, the Track/Session
	 * Number field is reserved and shall be set to 00h. There is no
	 * defined LBA addressing and MSF bit shall be set to one.
	 */
	CDB_TPA_ATIP,
	/**
	 * This format returns CD-TEXT information that is recorded in the
	 * Lead-in area as R-W Sub-channel Data.
	 */
	CDB_TPA_CD_TEXT,
};

#define TPA_SECTOR_MODE0		(0x00)
#define TPA_SECTOR_AUDIO		(0x01)
#define TPA_SECTOR_MODE1		(0x02)
#define TPA_SECTOR_MODE2		(0x03)
#define TPA_SECTOR_MODE2_FORM1		(0x04)
#define TPA_SECTOR_MODE2_FORM2		(0x05)
#define TPA_SECTOR_MODE2_MIXED		(TPA_SECTOR_MODE1 | TPA_SECTOR_MODE2_FORM1)
#define TPA_SECTOR_RAW				(0x07)
#define TPA_SECTOR_RAW_SCRAMBLED	(0x08)

/**
 * @brief The READ TOC/PMA/ATIP CDB (43h)
 * The READ TOC/PMA/ATIP command requests that the Drive read data from a
 * Table of Contents, the Program Memory Area (PMA), or the Absolute Time
 * in Pre-Grove (ATIP) from CD media, format according to CDB parameters
 * and transfer the result to the Host.
 */
struct cdb_read_toc_pma_atip {
	__u8 code;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved1 : 6;
	/**
	 * When MSF is set to zero, the address fields in some returned data
	 * formats shall be in LBA form. When MSF is set to one, the address
	 * fields in some returned data formats shall be in MSF form
	 */
	__u8 msf : 1;
	__u8 reserved2 : 1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 reserved2 : 1;
	__u8 msf : 1;
	__u8 reserved1 : 6;
#endif

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 reserved3 : 4;
	/**
	 * The Format field is used to select specific returned data format
	 * according to @enum cdb_read_tpa_format
	 */
	__u8 format : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 format : 4;
	__u8 reserved3 : 4;
#endif

	__u8 reserved4[3];
	/**
	 * The Track/Session Number field provides a method to restrict the
	 * returned of some data formats to a specific session or a track range
	 */
	__u8 number;

	/**
	 * The Allocation Length field specifies the maximum number of bytes that
	 * may be returned by the Drive.
	 * An Allocation Length field of zero shall not be considered an error
	 */
	__be16 length;

	__u8 control;
} __packed;

#define READ_TPA_LEADOUT_TRACK	(0xAAU)
/*
 * Control magic byte
 * Some legacy media recorder implementations set the control byte,
 * helping determine the relevant TOC/PMA/ATIP formats.
 * We should support this as well.
 */
#define READ_TPA_CTRL_MAGIC_SESS	(0x40U)
#define READ_TPA_CTRL_MAGIC_RAW		(0x80U)

/**
 * @brief READ TOC/PMA/ATIP Data list header
 * The response data list shows the general description of the response data
 * to the Read TOC/PMA/ATIP command.
 */
struct read_tpa_header {
	__be16 length;
	/* First Track/Session/Reserved Field */
	__u8 n_first_stf;
	/* Last Track/Session/Reserved Field */
	__u8 n_last_stf;
} __packed;

/**
 * @brief Response Format 0000b: Formatted TOC
 * The response data consist of four header bytes and zero or more track
 * descriptors.
 */
struct read_tpa_toc_formatted {
	__u8 reserved1;
#if defined(__BIG_ENDIAN_BITFIELD)
	/**
	 * The ADR field gives the type of information encoded in the Q Sub-channel
	 * of the block where this TOC entry was found.
	 */
	__u8 addr : 4;
	/**
	 * The CONTROL Field indicates the attributes of the track.
	 */
	__u8 control : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 control : 4;
	__u8 addr : 4;
#endif
	/**
	 * The Track Start Address contains the address of the first block with user
	 * information for that track number as read from the Table of Contents.
	 * A MSF bit of zero indicates that the Track Start Address field shall contain
	 * a logical block address.
	 * A MSF bit of one indicates the Logical Block Address field shall contain a
	 * MSF address
	 */
	__u8 track_number;
	__u8 reserved2;
	/**
	 * The Track Number field indicates the track number for that the data in the
	 * TOC track descriptor is valid. A track number of READ_TPA_LEADOUT_TRACK
	 * indicates that the track descriptor is for the start of the Lead-out area.
	 */
	__be32 start_addr_track;
} __packed;


#endif /* _UAPI_LINUX_CDROM_H */
