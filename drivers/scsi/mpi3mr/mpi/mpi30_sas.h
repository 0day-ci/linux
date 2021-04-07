/*
 *  Copyright 2016-2020 Broadcom Inc. All rights reserved.
 *
 *           Name: mpi30_sas.h
 *    Description: Contains MPI Serial Attached SCSI structures and definitions
 *  Creation Date: 12/08/2016
 *        Version: 03.00.00
 */
#ifndef MPI30_SAS_H
#define MPI30_SAS_H     1

/*****************************************************************************
 *              SAS Device Info Definitions                                  *
 ****************************************************************************/
#define MPI3_SAS_DEVICE_INFO_SSP_TARGET             (0x00000100)
#define MPI3_SAS_DEVICE_INFO_STP_SATA_TARGET        (0x00000080)
#define MPI3_SAS_DEVICE_INFO_SMP_TARGET             (0x00000040)
#define MPI3_SAS_DEVICE_INFO_SSP_INITIATOR          (0x00000020)
#define MPI3_SAS_DEVICE_INFO_STP_INITIATOR          (0x00000010)
#define MPI3_SAS_DEVICE_INFO_SMP_INITIATOR          (0x00000008)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK       (0x00000007)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_NO_DEVICE  (0x00000000)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_END_DEVICE (0x00000001)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_EXPANDER   (0x00000002)

/*****************************************************************************
 *              SMP Passthrough Request Message                              *
 ****************************************************************************/
typedef struct _MPI3_SMP_PASSTHROUGH_REQUEST {
    U16                     HostTag;                        /* 0x00 */
    U8                      IOCUseOnly02;                   /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     IOCUseOnly04;                   /* 0x04 */
    U8                      IOCUseOnly06;                   /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U16                     ChangeCount;                    /* 0x08 */
    U8                      Reserved0A;                     /* 0x0A */
    U8                      IOUnitPort;                     /* 0x0B */
    U32                     Reserved0C[3];                  /* 0x0C */
    U64                     SASAddress;                     /* 0x18 */
    MPI3_SGE_SIMPLE         RequestSGE;                     /* 0x20 */
    MPI3_SGE_SIMPLE         ResponseSGE;                    /* 0x30 */
} MPI3_SMP_PASSTHROUGH_REQUEST, MPI3_POINTER PTR_MPI3_SMP_PASSTHROUGH_REQUEST,
  Mpi3SmpPassthroughRequest_t, MPI3_POINTER pMpi3SmpPassthroughRequest_t;

#endif  /* MPI30_SAS_H */
