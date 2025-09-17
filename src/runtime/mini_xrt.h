/*
 *  Copyright (C) 2019-2022, Xilinx Inc
 *
 *  This file is dual licensed.  It may be redistributed and/or modified
 *  under the terms of the Apache 2.0 License OR version 2 of the GNU
 *  General Public License.
 *
 *  Apache License Verbiage
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  GPL license Verbiage:
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.  This program is
 *  distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 *  License for more details.  You should have received a copy of the
 *  GNU General Public License along with this program; if not, write
 *  to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 *  Boston, MA 02111-1307 USA
 *
 */

#ifndef MINI_XRT_H
#define MINI_XRT_H

#ifdef __GNUC__
#define XRT_DEPRECATED __attribute__((deprecated))
#else
#define XRT_DEPRECATED
#endif

#if defined(_WIN32)
#ifdef XCL_DRIVER_DLL_EXPORT
#define XCL_DRIVER_DLLESPEC __declspec(dllexport)
#else
#define XCL_DRIVER_DLLESPEC __declspec(dllimport)
#endif
#else
#define XCL_DRIVER_DLLESPEC __attribute__((visibility("default")))
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define to_cfg_pkg(pkg) \
    ((struct ert_configure_cmd *)(pkg))
#define to_start_krnl_pkg(pkg) \
    ((struct ert_start_kernel_cmd *)(pkg))
#define to_copybo_pkg(pkg) \
    ((struct ert_start_copybo_cmd *)(pkg))
#define to_cfg_sk_pkg(pkg) \
    ((struct ert_configure_sk_cmd *)(pkg))
#define to_init_krnl_pkg(pkg) \
    ((struct ert_init_kernel_cmd *)(pkg))
#define to_validate_pkg(pkg) \
    ((struct ert_validate_cmd *)(pkg))
#define to_abort_pkg(pkg) \
    ((struct ert_abort_cmd *)(pkg))

#define HOST_RW_PATTERN 0xF0F0F0F0
#define DEVICE_RW_PATTERN 0x0F0F0F0F

typedef unsigned char xuid_t[16];

#define XRT_NULL_HANDLE nullptr

/**
 * typedef xrtDeviceHandle - opaque device handle
 */
typedef void *xrtDeviceHandle;

/**
 * typedef xrtBufferHandle - opaque buffer handle
 */
typedef void *xrtBufferHandle;

/**
 * typedef xrtBufferFlags - flags for BO
 *
 * See ``xrt_mem.h`` for available flags
 */
typedef uint64_t xrtBufferFlags;

/**
 * typedef xrtMemoryGroup - Memory bank group for buffer
 */
typedef uint32_t xrtMemoryGroup;

/**
 * typedef xrtKernelHandle - opaque kernel handle
 *
 * A kernel handle is obtained by opening a kernel.  Clients
 * pass this kernel handle to APIs that operate on a kernel.
 */
typedef void *xrtKernelHandle;

/**
 * typedef xrtRunHandle - opaque handle to a specific kernel run
 *
 * A run handle is obtained by running a kernel.  Clients
 * use a run handle to check or wait for kernel completion.
 */
typedef void *xrtRunHandle;  // NOLINT

enum xclBOSyncDirection {
    XCL_BO_SYNC_BO_TO_DEVICE = 0,
    XCL_BO_SYNC_BO_FROM_DEVICE,
    XCL_BO_SYNC_BO_GMIO_TO_AIE,
    XCL_BO_SYNC_BO_AIE_TO_GMIO,
};

/**
 * Encoding of flags passed to xcl buffer allocation APIs
 */
struct xcl_bo_flags {
    union {
        uint32_t flags;
        struct {
            uint16_t bank;    // [15-0]
            uint8_t slot;     // [16-23]
            uint8_t boflags;  // [24-31]
        };
    };
};

/**
 * XCL BO Flags bits layout
 *
 * bits  0 ~ 15: DDR BANK index
 * bits 24 ~ 31: BO flags
 */
#define XRT_BO_FLAGS_MEMIDX_MASK (0xFFFFFFUL)
#define XCL_BO_FLAGS_NONE (0)
#define XCL_BO_FLAGS_CACHEABLE (1U << 24)
#define XCL_BO_FLAGS_KERNBUF (1U << 25)
#define XCL_BO_FLAGS_SGL (1U << 26)
#define XCL_BO_FLAGS_SVM (1U << 27)
#define XCL_BO_FLAGS_DEV_ONLY (1U << 28)
#define XCL_BO_FLAGS_HOST_ONLY (1U << 29)
#define XCL_BO_FLAGS_P2P (1U << 30)
#define XCL_BO_FLAGS_EXECBUF (1U << 31)

/**
 * XRT Native BO flags
 *
 * These flags are simple aliases for use with XRT native BO APIs.
 */
#define XRT_BO_FLAGS_NONE XCL_BO_FLAGS_NONE
#define XRT_BO_FLAGS_CACHEABLE XCL_BO_FLAGS_CACHEABLE
#define XRT_BO_FLAGS_DEV_ONLY XCL_BO_FLAGS_DEV_ONLY
#define XRT_BO_FLAGS_HOST_ONLY XCL_BO_FLAGS_HOST_ONLY
#define XRT_BO_FLAGS_P2P XCL_BO_FLAGS_P2P
#define XRT_BO_FLAGS_SVM XCL_BO_FLAGS_SVM

/**
 * This is the legacy usage of XCL DDR Flags.
 *
 * byte-0 lower 4 bits for DDR Flags are one-hot encoded
 */
enum xclDDRFlags {
    XCL_DEVICE_RAM_BANK0 = 0x00000000,
    XCL_DEVICE_RAM_BANK1 = 0x00000002,
    XCL_DEVICE_RAM_BANK2 = 0x00000004,
    XCL_DEVICE_RAM_BANK3 = 0x00000008,
};

/**
 * struct ert_packet: ERT generic packet format
 *
 * @state:   [3-0] current state of a command
 * @custom:  [11-4] custom per specific commands
 * @count:   [22-12] number of words in payload (data)
 * @opcode:  [27-23] opcode identifying specific command
 * @type:    [31-28] type of command (currently 0)
 * @data:    count number of words representing packet payload
 */
struct ert_packet {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t custom : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-28] */
        };
        uint32_t header;
    };
    uint32_t data[1]; /* count number of words */
};

/**
 * struct ert_start_kernel_cmd: ERT start kernel command format
 *
 * @state:           [3-0]   current state of a command
 * @stat_enabled:    [4]     enabled driver to record timestamp for various
 *                           states cmd has gone through. The stat data
 *                           is appended after cmd data.
 * @extra_cu_masks:  [11-10] extra CU masks in addition to mandatory mask
 * @count:           [22-12] number of words following header for cmd data. Not
 *                           include stat data.
 * @opcode:          [27-23] 0, opcode for start_kernel
 * @type:            [31-27] 0, type of start_kernel
 *
 * @cu_mask:         first mandatory CU mask
 * @data:            count-1 number of words representing interpreted payload
 *
 * The packet payload is comprised of reserved id field, a mandatory CU mask,
 * and extra_cu_masks per header field, followed by a CU register map of size
 * (count - (1 + extra_cu_masks)) uint32_t words.
 */
struct ert_start_kernel_cmd {
    union {
        struct {
            uint32_t state : 4;          /* [3-0]   */
            uint32_t stat_enabled : 1;   /* [4]     */
            uint32_t unused : 5;         /* [9-5]   */
            uint32_t extra_cu_masks : 2; /* [11-10] */
            uint32_t count : 11;         /* [22-12] */
            uint32_t opcode : 5;         /* [27-23] */
            uint32_t type : 4;           /* [31-27] */
        };
        uint32_t header;
    };

    /* payload */
    uint32_t cu_mask; /* mandatory cu mask */
    uint32_t data[1]; /* count-1 number of words */
};

/**
 * struct ert_init_kernel_cmd: ERT initialize kernel command format
 * this command initializes CUs by writing CU registers. CUs are
 * represented by cu_mask and extra_cu_masks.
 *
 * @state:           [3-0]   current state of a command
 * @update_rtp:      [4]     command is for runtime update of cu argument
 * @extra_cu_masks:  [11-10] extra CU masks in addition to mandatory mask
 * @count:           [22-12] number of words following header
 * @opcode:          [27-23] 0, opcode for init_kernel
 * @type:            [31-27] 0, type of init_kernel
 *
 * @cu_run_timeout   the configured CU timeout value in Microseconds
 *                   setting to 0 means CU should not timeout
 * @cu_reset_timeout the configured CU reset timeout value in Microseconds
 *                   when CU timeout, CU will be reset. this indicates
 *                   CU reset should be completed within the timeout value.
 *                   if cu_run_timeout is set to 0, this field is undefined.
 *
 * @cu_mask:         first mandatory CU mask
 * @data:            count-9 number of words representing interpreted payload
 *
 * The packet payload is comprised of reserved id field, 8 reserved fields,
 * a mandatory CU mask, and extra_cu_masks per header field, followed by a
 * CU register map of size (count - (9 + extra_cu_masks)) uint32_t words.
 */
struct ert_init_kernel_cmd {
    union {
        struct {
            uint32_t state : 4;          /* [3-0]   */
            uint32_t update_rtp : 1;     /* [4]  */
            uint32_t unused : 5;         /* [9-5]  */
            uint32_t extra_cu_masks : 2; /* [11-10]  */
            uint32_t count : 11;         /* [22-12] */
            uint32_t opcode : 5;         /* [27-23] */
            uint32_t type : 4;           /* [31-27] */
        };
        uint32_t header;
    };

    uint32_t cu_run_timeout;   /* CU timeout value in Microseconds */
    uint32_t cu_reset_timeout; /* CU reset timeout value in Microseconds */
    uint32_t reserved[6];      /* reserved for future use */

    /* payload */
    uint32_t cu_mask; /* mandatory cu mask */
    uint32_t data[1]; /* count-9 number of words */
};

#define KDMA_BLOCK_SIZE 64 /* Limited by KDMA CU */
struct ert_start_copybo_cmd {
    uint32_t state : 4;          /* [3-0], must be ERT_CMD_STATE_NEW */
    uint32_t unused : 6;         /* [9-4] */
    uint32_t extra_cu_masks : 2; /* [11-10], = 3 */
    uint32_t count : 11;         /* [22-12], = 16, exclude 'arg' */
    uint32_t opcode : 5;         /* [27-23], = ERT_START_COPYBO */
    uint32_t type : 4;           /* [31-27], = ERT_DEFAULT */
    uint32_t cu_mask[4];         /* mandatory cu masks */
    uint32_t reserved[4];        /* for scheduler use */
    uint32_t src_addr_lo;        /* low 32 bit of src addr */
    uint32_t src_addr_hi;        /* high 32 bit of src addr */
    uint32_t src_bo_hdl;         /* src bo handle, cleared by driver */
    uint32_t dst_addr_lo;        /* low 32 bit of dst addr */
    uint32_t dst_addr_hi;        /* high 32 bit of dst addr */
    uint32_t dst_bo_hdl;         /* dst bo handle, cleared by driver */
    uint32_t size;               /* size in bytes low 32 bit*/
    uint32_t size_hi;            /* size in bytes high 32 bit*/
    void *arg;                   /* pointer to aux data for KDS */
};

/**
 * struct ert_configure_cmd: ERT configure command format
 *
 * @state:           [3-0] current state of a command
 * @count:           [22-12] number of words in payload (5 + num_cus)
 * @opcode:          [27-23] 1, opcode for configure
 * @type:            [31-27] 0, type of configure
 *
 * @slot_size:       command queue slot size
 * @num_cus:         number of compute units in program
 * @cu_shift:        shift value to convert CU idx to CU addr
 * @cu_base_addr:    base address to add to CU addr for actual physical address
 *
 * @ert:1            enable embedded HW scheduler
 * @polling:1        poll for command completion
 * @cu_dma:1         enable CUDMA custom module for HW scheduler
 * @cu_isr:1         enable CUISR custom module for HW scheduler
 * @cq_int:1         enable interrupt from host to HW scheduler
 * @cdma:1           enable CDMA kernel
 * @unused:25
 * @dsa52:1          reserved for internal use
 *
 * @data:            addresses of @num_cus CUs
 */
struct ert_configure_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t unused : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };

    /* payload */
    uint32_t slot_size;
    uint32_t num_cus;
    uint32_t cu_shift;
    uint32_t cu_base_addr;

    /* features */
    uint32_t ert : 1;
    uint32_t polling : 1;
    uint32_t cu_dma : 1;
    uint32_t cu_isr : 1;
    uint32_t cq_int : 1;
    uint32_t cdma : 1;
    uint32_t dataflow : 1;
    /* WORKAROUND: allow xclRegWrite/xclRegRead access shared CU */
    uint32_t rw_shared : 1;
    uint32_t kds_30 : 1;
    uint32_t dmsg : 1;
    uint32_t echo : 1;
    uint32_t intr : 1;
    uint32_t unusedf : 19;
    uint32_t dsa52 : 1;

    /* cu address map size is num_cus */
    uint32_t data[1];
};

/*
 * Note: We need to put maximum 128 soft kernel image
 *       in one config command (1024 DWs including header).
 *       So each one needs to be smaller than 8 DWs.
 *
 * This data struct is obsoleted. Only used in legacy ERT firmware.
 * Use 'struct config_sk_image_uuid' instead on XGQ based ERT.
 *
 * @start_cuidx:     start index of compute units of each image
 * @num_cus:         number of compute units of each image
 * @sk_name:         symbol name of soft kernel of each image
 */
struct config_sk_image {
    uint32_t start_cuidx;
    uint32_t num_cus;
    uint32_t sk_name[5];
};

/*
 * Note: We need to put maximum 128 soft kernel image
 *       in one config command (1024 DWs including header).
 *       So each one needs to be smaller than 8 DWs.
 *
 * @start_cuidx:     start index of compute units of each image
 * @num_cus:         number of compute units of each image
 * @sk_name:         symbol name of soft kernel of each image
 * @sk_uuid:         xclbin uuid that this soft kernel image belones to
 */
struct config_sk_image_uuid {
    uint32_t start_cuidx;
    uint32_t num_cus;
    uint32_t sk_name[5];
    unsigned char sk_uuid[16];
};

/**
 * struct ert_configure_sk_cmd: ERT configure soft kernel command format
 *
 * @state:           [3-0] current state of a command
 * @count:           [22-12] number of words in payload
 * @opcode:          [27-23] 1, opcode for configure
 * @type:            [31-27] 0, type of configure
 *
 * @num_image:       number of images
 */
struct ert_configure_sk_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t unused : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };

    /* payload */
    uint32_t num_image;
    struct config_sk_image image[1];
};

/**
 * struct ert_unconfigure_sk_cmd: ERT unconfigure soft kernel command format
 *
 * @state:           [3-0] current state of a command
 * @count:           [22-12] number of words in payload
 * @opcode:          [27-23] 1, opcode for configure
 * @type:            [31-27] 0, type of configure
 *
 * @start_cuidx:     start index of compute units
 * @num_cus:         number of compute units in program
 */
struct ert_unconfigure_sk_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t unused : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };

    /* payload */
    uint32_t start_cuidx;
    uint32_t num_cus;
};

/**
 * struct ert_abort_cmd: ERT abort command format.
 *
 * @exec_bo_handle: The bo handle of execbuf command to abort
 */
struct ert_abort_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t custom : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };

    /* payload */
    uint64_t exec_bo_handle;
};

/**
 * struct ert_validate_cmd: ERT BIST command format.
 *
 */
struct ert_validate_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t custom : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };
    uint32_t timestamp;
    uint32_t cq_read_single;
    uint32_t cq_write_single;
    uint32_t cu_read_single;
    uint32_t cu_write_single;
};

/**
 * struct ert_validate_cmd: ERT BIST command format.
 *
 */
struct ert_access_valid_cmd {
    union {
        struct {
            uint32_t state : 4;  /* [3-0]   */
            uint32_t custom : 8; /* [11-4]  */
            uint32_t count : 11; /* [22-12] */
            uint32_t opcode : 5; /* [27-23] */
            uint32_t type : 4;   /* [31-27] */
        };
        uint32_t header;
    };
    uint32_t h2h_access;
    uint32_t h2d_access;
    uint32_t d2h_access;
    uint32_t d2d_access;
    uint32_t d2cu_access;
    uint32_t wr_count;
    uint32_t wr_test;
};

/**
 * ERT command state
 *
 * @ERT_CMD_STATE_NEW:         Set by host before submitting a command to
 *                             scheduler
 * @ERT_CMD_STATE_QUEUED:      Internal scheduler state
 * @ERT_CMD_STATE_SUBMITTED:   Internal scheduler state
 * @ERT_CMD_STATE_RUNNING:     Internal scheduler state
 * @ERT_CMD_STATE_COMPLETED:   Set by scheduler when command completes
 * @ERT_CMD_STATE_ERROR:       Set by scheduler if command failed
 * @ERT_CMD_STATE_ABORT:       Set by scheduler if command abort
 * @ERT_CMD_STATE_TIMEOUT:     Set by scheduler if command timeout and reset
 * @ERT_CMD_STATE_NORESPONSE:  Set by scheduler if command timeout and fail to
 *                             reset
 */
enum ert_cmd_state {
    ERT_CMD_STATE_NEW = 1,
    ERT_CMD_STATE_QUEUED = 2,
    ERT_CMD_STATE_RUNNING = 3,
    ERT_CMD_STATE_COMPLETED = 4,
    ERT_CMD_STATE_ERROR = 5,
    ERT_CMD_STATE_ABORT = 6,
    ERT_CMD_STATE_SUBMITTED = 7,
    ERT_CMD_STATE_TIMEOUT = 8,
    ERT_CMD_STATE_NORESPONSE = 9,
    ERT_CMD_STATE_SKERROR = 10,    // Check for error return code from Soft Kernel
    ERT_CMD_STATE_SKCRASHED = 11,  // Soft kernel has crashed
    ERT_CMD_STATE_MAX,             // Always the last one
};

struct cu_cmd_state_timestamps {
    uint64_t skc_timestamps[ERT_CMD_STATE_MAX];  // In nano-second
};

/**
 * Opcode types for commands
 *
 * @ERT_START_CU:       start a workgroup on a CU
 * @ERT_START_KERNEL:   currently aliased to ERT_START_CU
 * @ERT_CONFIGURE:      configure command scheduler
 * @ERT_EXEC_WRITE:     execute a specified CU after writing
 * @ERT_CU_STAT:        get stats about CU execution
 * @ERT_START_COPYBO:   start KDMA CU or P2P, may be converted to ERT_START_CU
 *                      before cmd reach to scheduler, short-term hack
 * @ERT_SK_CONFIG:      configure soft kernel
 * @ERT_SK_START:       start a soft kernel
 * @ERT_SK_UNCONFIG:    unconfigure a soft kernel
 * @ERT_START_KEY_VAL:  same as ERT_START_CU but with key-value pair flavor
 */
enum ert_cmd_opcode {
    ERT_START_CU = 0,
    ERT_START_KERNEL = 0,
    ERT_CONFIGURE = 2,
    ERT_EXIT = 3,
    ERT_ABORT = 4,
    ERT_EXEC_WRITE = 5,
    ERT_CU_STAT = 6,
    ERT_START_COPYBO = 7,
    ERT_SK_CONFIG = 8,
    ERT_SK_START = 9,
    ERT_SK_UNCONFIG = 10,
    ERT_INIT_CU = 11,
    ERT_START_FA = 12,
    ERT_CLK_CALIB = 13,
    ERT_MB_VALIDATE = 14,
    ERT_START_KEY_VAL = 15,
    ERT_ACCESS_TEST_C = 16,
    ERT_ACCESS_TEST = 17,
};

/**
 * Command types
 *
 * @ERT_DEFAULT:        default command type
 * @ERT_KDS_LOCAL:      command processed by KDS locally
 * @ERT_CTRL:           control command uses reserved command queue slot
 * @ERT_CU:             compute unit command
 */
enum ert_cmd_type {
    ERT_DEFAULT = 0,
    ERT_KDS_LOCAL = 1,
    ERT_CTRL = 2,
    ERT_CU = 3,
    ERT_SCU = 4,
};

/**
 * Soft kernel types
 *
 * @SOFTKERNEL_TYPE_EXEC:       executable
 */
enum softkernel_type {
    SOFTKERNEL_TYPE_EXEC = 0,
};

/*
 * Base address GPIO per spec
 * | Offset  | Description
 * -----------------------
 * | 0x00    | ERT_MGMT_PF_base_addr (Not sure where this should be use)
 * | 0x08    | ERT_USER_PF_base_addr. The base address of ERT peripherals
 */
#if defined(ERT_BUILD_V20)
uint32_t ert_base_addr = 0;
#define ERT_BASE_ADDR 0x01F30008
#endif

#if defined(ERT_BUILD_V30)
uint32_t ert_base_addr = 0;
#define ERT_BASE_ADDR 0x01F30008
#endif

/**
 * Address constants per spec
 */
#define ERT_WORD_SIZE 4     /* 4 bytes */
#define ERT_CQ_SIZE 0x10000 /* 64K */
#if defined(ERT_BUILD_U50)
#define ERT_CQ_BASE_ADDR 0x340000
#define ERT_CSR_ADDR 0x360000
#elif defined(ERT_BUILD_V20)
#define ERT_CQ_BASE_ADDR (0x000000 + ert_base_addr)
#define ERT_CSR_ADDR (0x010000 + ert_base_addr)
#elif defined(ERT_BUILD_V30)
#define ERT_CQ_BASE_ADDR 0x1F60000
#define ERT_CSR_ADDR (0x010000 + ert_base_addr)
#else
#define ERT_CQ_BASE_ADDR 0x190000
#define ERT_CSR_ADDR 0x180000
#endif

/**
 * The STATUS REGISTER is for communicating completed CQ slot indices
 * MicroBlaze write, host reads.  MB(W) / HOST(COR)
 */
#define ERT_STATUS_REGISTER_ADDR (ERT_CSR_ADDR)
#define ERT_STATUS_REGISTER_ADDR0 (ERT_CSR_ADDR)
#define ERT_STATUS_REGISTER_ADDR1 (ERT_CSR_ADDR + 0x4)
#define ERT_STATUS_REGISTER_ADDR2 (ERT_CSR_ADDR + 0x8)
#define ERT_STATUS_REGISTER_ADDR3 (ERT_CSR_ADDR + 0xC)

/**
 * The CU DMA REGISTER is for communicating which CQ slot is to be started
 * on a specific CU.  MB selects a free CU on which the command can
 * run, then writes the 1<<CU back to the command slot CU mask and
 * writes the slot index to the CU DMA REGISTER.  HW is notified when
 * the register is written and now does the DMA transfer of CU regmap
 * map from command to CU, while MB continues its work. MB(W) / HW(R)
 */
#define ERT_CU_DMA_ENABLE_ADDR (ERT_CSR_ADDR + 0x18)
#define ERT_CU_DMA_REGISTER_ADDR (ERT_CSR_ADDR + 0x1C)
#define ERT_CU_DMA_REGISTER_ADDR0 (ERT_CSR_ADDR + 0x1C)
#define ERT_CU_DMA_REGISTER_ADDR1 (ERT_CSR_ADDR + 0x20)
#define ERT_CU_DMA_REGISTER_ADDR2 (ERT_CSR_ADDR + 0x24)
#define ERT_CU_DMA_REGISTER_ADDR3 (ERT_CSR_ADDR + 0x28)

/**
 * The SLOT SIZE is the size of slots in command queue, it is
 * configurable per xclbin. MB(W) / HW(R)
 */
#define ERT_CQ_SLOT_SIZE_ADDR (ERT_CSR_ADDR + 0x2C)

/**
 * The CU_OFFSET is the size of a CU's address map in power of 2.  For
 * example a 64K regmap is 2^16 so 16 is written to the CU_OFFSET_ADDR.
 * MB(W) / HW(R)
 */
#define ERT_CU_OFFSET_ADDR (ERT_CSR_ADDR + 0x30)

/**
 * The number of slots is command_queue_size / slot_size.
 * MB(W) / HW(R)
 */
#define ERT_CQ_NUMBER_OF_SLOTS_ADDR (ERT_CSR_ADDR + 0x34)

/**
 * All CUs placed in same address space separated by CU_OFFSET. The
 * CU_BASE_ADDRESS is the address of the first CU. MB(W) / HW(R)
 */
#define ERT_CU_BASE_ADDRESS_ADDR (ERT_CSR_ADDR + 0x38)

/**
 * The CQ_BASE_ADDRESS is the base address of the command queue.
 * MB(W) / HW(R)
 */
#define ERT_CQ_BASE_ADDRESS_ADDR (ERT_CSR_ADDR + 0x3C)

/**
 * The CU_ISR_HANDLER_ENABLE (MB(W)/HW(R)) enables the HW handling of
 * CU interrupts.  When a CU interrupts (when done), hardware handles
 * the interrupt and writes the index of the CU that completed into
 * the CU_STATUS_REGISTER (HW(W)/MB(COR)) as a bitmask
 */
#define ERT_CU_ISR_HANDLER_ENABLE_ADDR (ERT_CSR_ADDR + 0x40)
#define ERT_CU_STATUS_REGISTER_ADDR (ERT_CSR_ADDR + 0x44)
#define ERT_CU_STATUS_REGISTER_ADDR0 (ERT_CSR_ADDR + 0x44)
#define ERT_CU_STATUS_REGISTER_ADDR1 (ERT_CSR_ADDR + 0x48)
#define ERT_CU_STATUS_REGISTER_ADDR2 (ERT_CSR_ADDR + 0x4C)
#define ERT_CU_STATUS_REGISTER_ADDR3 (ERT_CSR_ADDR + 0x50)

/**
 * The CQ_STATUS_ENABLE (MB(W)/HW(R)) enables interrupts from HOST to
 * MB to indicate the presense of a new command in some slot.  The
 * slot index is written to the CQ_STATUS_REGISTER (HOST(W)/MB(R))
 */
#define ERT_CQ_STATUS_ENABLE_ADDR (ERT_CSR_ADDR + 0x54)
#define ERT_CQ_STATUS_REGISTER_ADDR (ERT_CSR_ADDR + 0x58)
#define ERT_CQ_STATUS_REGISTER_ADDR0 (ERT_CSR_ADDR + 0x58)
#define ERT_CQ_STATUS_REGISTER_ADDR1 (ERT_CSR_ADDR + 0x5C)
#define ERT_CQ_STATUS_REGISTER_ADDR2 (ERT_CSR_ADDR + 0x60)
#define ERT_CQ_STATUS_REGISTER_ADDR3 (ERT_CSR_ADDR + 0x64)

/**
 * The NUMBER_OF_CU (MB(W)/HW(R) is the number of CUs per current
 * xclbin.  This is an optimization that allows HW to only check CU
 * completion on actual CUs.
 */
#define ERT_NUMBER_OF_CU_ADDR (ERT_CSR_ADDR + 0x68)

/**
 * Enable global interrupts from MB to HOST on command completion.
 * When enabled writing to STATUS_REGISTER causes an interrupt in HOST.
 * MB(W)
 */
#define ERT_HOST_INTERRUPT_ENABLE_ADDR (ERT_CSR_ADDR + 0x100)

/**
 * Interrupt controller base address
 * This value is per hardware BSP (XPAR_INTC_SINGLE_BASEADDR)
 */
#if defined(ERT_BUILD_U50)
#define ERT_INTC_ADDR 0x00310000
#elif defined(ERT_BUILD_V20)
#define ERT_INTC_ADDR 0x01F20000
#elif defined(ERT_BUILD_V30)
#define ERT_INTC_ADDR 0x01F20000
#define ERT_INTC_CU_0_31_ADDR (0x0000 + ert_base_addr)
#define ERT_INTC_CU_32_63_ADDR (0x1000 + ert_base_addr)
#define ERT_INTC_CU_64_95_ADDR (0x2000 + ert_base_addr)
#define ERT_INTC_CU_96_127_ADDR (0x3000 + ert_base_addr)
#else
#define ERT_INTC_ADDR 0x41200000
#define ERT_INTC_CU_0_31_ADDR 0x0000
#define ERT_INTC_CU_32_63_ADDR 0x1000
#define ERT_INTC_CU_64_95_ADDR 0x2000
#define ERT_INTC_CU_96_127_ADDR 0x3000
#endif

/**
 * Look up table for CUISR for CU addresses
 */
#define ERT_CUISR_LUT_ADDR (ERT_CSR_ADDR + 0x400)

/**
 * ERT exit command/ack
 */
#define ERT_EXIT_CMD ((ERT_EXIT << 23) | ERT_CMD_STATE_NEW)
#define ERT_EXIT_ACK (ERT_CMD_STATE_COMPLETED)
#define ERT_EXIT_CMD_OP (ERT_EXIT << 23)

/**
 * State machine for both CUDMA and CUISR modules
 */
#define ERT_HLS_MODULE_IDLE 0x1
#define ERT_CUDMA_STATE (ERT_CSR_ADDR + 0x318)
#define ERT_CUISR_STATE (ERT_CSR_ADDR + 0x328)

/**
 * Interrupt address masks written by MB when interrupts from
 * CU are enabled
 */
#define ERT_INTC_IPR_ADDR (ERT_INTC_ADDR + 0x4)  /* pending */
#define ERT_INTC_IER_ADDR (ERT_INTC_ADDR + 0x8)  /* enable */
#define ERT_INTC_IAR_ADDR (ERT_INTC_ADDR + 0x0C) /* acknowledge */
#define ERT_INTC_MER_ADDR (ERT_INTC_ADDR + 0x1C) /* master enable */

#define ERT_INTC_CU_0_31_IPR (ERT_INTC_CU_0_31_ADDR + 0x4)  /* pending */
#define ERT_INTC_CU_0_31_IER (ERT_INTC_CU_0_31_ADDR + 0x8)  /* enable */
#define ERT_INTC_CU_0_31_IAR (ERT_INTC_CU_0_31_ADDR + 0x0C) /* acknowledge */
#define ERT_INTC_CU_0_31_MER (ERT_INTC_CU_0_31_ADDR + 0x1C) /* master enable */

#define ERT_INTC_CU_32_63_IPR (ERT_INTC_CU_32_63_ADDR + 0x4)  /* pending */
#define ERT_INTC_CU_32_63_IER (ERT_INTC_CU_32_63_ADDR + 0x8)  /* enable */
#define ERT_INTC_CU_32_63_IAR (ERT_INTC_CU_32_63_ADDR + 0x0C) /* acknowledge */
#define ERT_INTC_CU_32_63_MER (ERT_INTC_CU_32_63_ADDR + 0x1C) /* master enable */

#define ERT_INTC_CU_64_95_IPR (ERT_INTC_CU_64_95_ADDR + 0x4)  /* pending */
#define ERT_INTC_CU_64_95_IER (ERT_INTC_CU_64_95_ADDR + 0x8)  /* enable */
#define ERT_INTC_CU_64_95_IAR (ERT_INTC_CU_64_95_ADDR + 0x0C) /* acknowledge */
#define ERT_INTC_CU_64_95_MER (ERT_INTC_CU_64_95_ADDR + 0x1C) /* master enable */

#define ERT_INTC_CU_96_127_IPR (ERT_INTC_CU_96_127_ADDR + 0x4)  /* pending */
#define ERT_INTC_CU_96_127_IER (ERT_INTC_CU_96_127_ADDR + 0x8)  /* enable */
#define ERT_INTC_CU_96_127_IAR (ERT_INTC_CU_96_127_ADDR + 0x0C) /* acknowledge */
#define ERT_INTC_CU_96_127_MER (ERT_INTC_CU_96_127_ADDR + 0x1C) /* master enable */

#if defined(ERT_BUILD_V30)
#define ERT_CLK_COUNTER_ADDR 0x1F70000
#else
#define ERT_CLK_COUNTER_ADDR 0x0
#endif

/*
 * Used in driver and user space code
 */
/*
 * Upper limit on number of dependencies in execBuf waitlist
 */
#define MAX_DEPS 8

/*
 * Maximum size of mandatory fields in bytes for all packet type
 */
#define MAX_HEADER_SIZE 64

/*
 * Maximum size of mandatory fields in bytes for all packet type
 */
#define MAX_CONFIG_PACKET_SIZE 512

/*
 * Maximum size of CQ slot
 */
#define MAX_CQ_SLOT_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

/*
 * xclProbe() - Enumerate devices found in the system
 *
 * Return: count of devices found
 */
XCL_DRIVER_DLLESPEC
unsigned int
xclProbe();

/**
 * xrtDeviceOpen() - Open a device and obtain its handle
 *
 * @index:         Device index
 * Return:         Handle representing the opened device, or nullptr on error
 */
XCL_DRIVER_DLLESPEC
xrtDeviceHandle
xrtDeviceOpen(unsigned int index);

/**
 * xrtDeviceOpenByBDF() - Open a device and obtain its handle
 *
 * @bdf:           PCIe BDF identifying the device to open
 * Return:         Handle representing the opened device, or nullptr on error
 */
XCL_DRIVER_DLLESPEC
xrtDeviceHandle
xrtDeviceOpenByBDF(const char *bdf);

/**
 * xrtDeviceClose() - Close an opened device
 *
 * @dhdl:       Handle to device previously opened with xrtDeviceOpen
 * Return:      0 on success, error otherwise
 */
XCL_DRIVER_DLLESPEC
int xrtDeviceClose(xrtDeviceHandle dhdl);

/**
 * xrtDeviceLoadXclbin() - Load an xclbin image
 *
 * @dhdl:       Handle to device previously opened with xrtDeviceOpen
 * @xclbin:     Pointer to complete axlf in memory image
 * Return:      0 on success, error otherwise
 *
 * The xclbin image can safely be deleted after calling
 * this funciton.
 */
XCL_DRIVER_DLLESPEC
int xrtDeviceLoadXclbin(xrtDeviceHandle dhdl, const struct axlf *xclbin);

/**
 * xrtDeviceLoadXclbinFile() - Read and load an xclbin file
 *
 * @dhdl:       Handle to device previously opened with xrtDeviceOpen
 * @xclbin_fnm: Full path to xclbin file
 * Return:      0 on success, error otherwise
 *
 * This function read the file from disk and loads
 * the xclbin.   Using this function allows one time
 * allocation of data that needs to be kept in memory.
 */
XCL_DRIVER_DLLESPEC
int xrtDeviceLoadXclbinFile(xrtDeviceHandle dhdl, const char *xclbin_fnm);

/**
 * xrtDeviceLoadXclbinHandle() - load an xclbin from an xrt::xclbin handle
 *
 * @dhdl:       Handle to device previously opened with xrtDeviceOpen
 * @uuid:       uuid_t struct of xclbin id
 * Return:      0 on success, error otherwise
 *
 * This function reads the xclbin id already loaded in the system and
 * comapres it with the input uuid. If they match, load the cached
 * xclbin metadata into caller's process. Otherwise returns error.
 */
XCL_DRIVER_DLLESPEC
int xrtDeviceLoadXclbinUUID(xrtDeviceHandle dhdl, const xuid_t uuid);

/**
 * xrtDeviceGetXclbinUUID() - Get UUID of xclbin image loaded on device
 *
 * @dhdl:   Handle to device previously opened with xrtDeviceOpen
 * @out:    Return xclbin id in this uuid_t struct
 * Return:  0 on success or appropriate error number
 *
 * Note that current UUID can be different from the UUID of
 * the xclbin loaded by this process using @load_xclbin()
 */
XCL_DRIVER_DLLESPEC
int xrtDeviceGetXclbinUUID(xrtDeviceHandle dhdl, xuid_t out);

/**
 * xrtBOAllocUserPtr() - Allocate a BO using userptr provided by the user
 *
 * @dhdl:          Device handle
 * @userptr:       Pointer to 4K aligned user memory
 * @size:          Size of buffer
 * @flags:         Specify type of buffer
 * @grp:           Specify bank information
 * Return:         xrtBufferHandle on success or NULL
 */
XCL_DRIVER_DLLESPEC
xrtBufferHandle
xrtBOAllocUserPtr(xrtDeviceHandle dhdl, void *userptr, size_t size, xrtBufferFlags flags, xrtMemoryGroup grp);

/**
 * xrtBOAlloc() - Allocate a BO of requested size with appropriate flags
 *
 * @dhdl:          Device handle
 * @size:          Size of buffer
 * @flags:         Specify type of buffer
 * @grp:           Specify bank information
 * Return:         xrtBufferHandle on success or NULL
 */
XCL_DRIVER_DLLESPEC
xrtBufferHandle
xrtBOAlloc(xrtDeviceHandle dhdl, size_t size, xrtBufferFlags flags, xrtMemoryGroup grp);

/**
 * xrtBOSubAlloc() - Allocate a sub buffer from a parent buffer
 *
 * @parent:        Parent buffer handle
 * @size:          Size of sub buffer
 * @offset:        Offset into parent buffer
 * Return:         xrtBufferHandle on success or NULL
 */
XCL_DRIVER_DLLESPEC
xrtBufferHandle
xrtBOSubAlloc(xrtBufferHandle parent, size_t size, size_t offset);

/**
 * xrtBOFree() - Free a previously allocated BO
 *
 * @bhdl:         Buffer handle
 * Return:        0 on success, or err code on error
 */
XCL_DRIVER_DLLESPEC
int xrtBOFree(xrtBufferHandle bhdl);

/**
 * xrtBOSize() - Get the size of this buffer
 *
 * @bhdl:         Buffer handle
 * Return:        Size of buffer in bytes
 */
XCL_DRIVER_DLLESPEC
size_t
xrtBOSize(xrtBufferHandle bhdl);

/**
 * xrtBOAddr() - Get the physical address of this buffer
 *
 * @bhdl:         Buffer handle
 * Return:        Device address of this BO, or LLONG_MAX on error
 */
XCL_DRIVER_DLLESPEC
uint64_t
xrtBOAddress(xrtBufferHandle bhdl);

/**
 * xrtBOSync() - Synchronize buffer contents in requested direction
 *
 * @bhdl:          Bufferhandle
 * @dir:           To device or from device
 * @size:          Size of data to synchronize
 * @offset:        Offset within the BO
 * Return:         0 on success or error
 *
 * Synchronize the buffer contents between host and device. Depending
 * on the memory model this may require DMA to/from device or CPU
 * cache flushing/invalidation
 */
XCL_DRIVER_DLLESPEC
int xrtBOSync(xrtBufferHandle bhdl, enum xclBOSyncDirection dir, size_t size, size_t offset);

/**
 * xrtBOMap() - Memory map BO into user's address space
 *
 * @bhdl:       Buffer handle
 * Return:      Memory mapped buffer, or NULL on error
 *
 * Map the contents of the buffer object into host memory.  The buffer
 * object is unmapped when freed.
 */
XCL_DRIVER_DLLESPEC
void *
xrtBOMap(xrtBufferHandle bhdl);

/**
 * xrtBOWrite() - Copy-in user data to host backing storage of BO
 *
 * @bhdl:          Buffer handle
 * @src:           Source data pointer
 * @size:          Size of data to copy
 * @seek:          Offset within the BO
 * Return:         0 on success or appropriate error number
 *
 * Copy host buffer contents to previously allocated device
 * memory. ``seek`` specifies how many bytes to skip at the beginning
 * of the BO before copying-in ``size`` bytes of host buffer.
 */
XCL_DRIVER_DLLESPEC
int xrtBOWrite(xrtBufferHandle bhdl, const void *src, size_t size, size_t seek);

/**
 * xrtBORead() - Copy-out user data from host backing storage of BO
 *
 * @bhdl:          Buffer handle
 * @dst:           Destination data pointer
 * @size:          Size of data to copy
 * @skip:          Offset within the BO
 * Return:         0 on success or appropriate error number
 *
 * Copy contents of previously allocated device memory to host
 * buffer. ``skip`` specifies how many bytes to skip from the
 * beginning of the BO before copying-out ``size`` bytes of device
 * buffer.
 */
XCL_DRIVER_DLLESPEC
int xrtBORead(xrtBufferHandle bhdl, void *dst, size_t size, size_t skip);

/**
 * xrtBOCopy() - Deep copy BO content from another buffer
 *
 * @dst:          Destination BO to copy to
 * @src:          Source BO to copy from
 * @sz:           Size of data to copy
 * @dst_offset:   Offset into destination buffer to copy to
 * @src_offset:   Offset into src buffer to copy from
 * Return:        0 on success or appropriate error number
 *
 * It is an error if sz is 0 bytes or sz + src/dst_offset is out of bounds.
 */
XCL_DRIVER_DLLESPEC
int xrtBOCopy(xrtBufferHandle dst, xrtBufferHandle src, size_t sz, size_t dst_offset, size_t src_offset);

/**
 * xrtPLKernelOpen() - Open a PL kernel and obtain its handle.
 *
 * @deviceHandle:  Handle to the device with the kernel
 * @xclbinId:      The uuid of the xclbin with the specified kernel.
 * @name:          Name of kernel to open.
 * Return:         Handle representing the opened kernel.
 *
 * The kernel name must uniquely identify compatible kernel instances
 * (compute units).  Optionally specify which kernel instance(s) to
 * open using "kernelname:{instancename1,instancename2,...}" syntax.
 * The compute units are opened with shared access, meaning that
 * other kernels and other process will have shared access to same
 * compute units.  If exclusive access is needed then open the
 * kernel using @xrtPLKernelOpenExclusve().
 *
 * An xclbin with the specified kernel must have been loaded prior
 * to calling this function. An XRT_NULL_HANDLE is returned on error
 * and errno is set accordingly.
 *
 * A kernel handle is thread safe and can be shared between threads.
 */
XCL_DRIVER_DLLESPEC
xrtKernelHandle
xrtPLKernelOpen(xrtDeviceHandle deviceHandle, const xuid_t xclbinId, const char *name);

/**
 * xrtPLKernelOpenExclusive() - Open a PL kernel and obtain its handle.
 *
 * @deviceHandle:  Handle to the device with the kernel
 * @xclbinId:      The uuid of the xclbin with the specified kernel.
 * @name:          Name of kernel to open.
 * Return:         Handle representing the opened kernel.
 *
 * Same as @xrtPLKernelOpen(), but opens compute units with exclusive
 * access.  Fails if any compute unit is already opened with either
 * exclusive or shared access.
 */
XCL_DRIVER_DLLESPEC
xrtKernelHandle
xrtPLKernelOpenExclusive(xrtDeviceHandle deviceHandle, const xuid_t xclbinId, const char *name);

/**
 * xrtKernelClose() - Close an opened kernel
 *
 * @kernelHandle: Handle to kernel previously opened with xrtKernelOpen
 * Return:        0 on success, -1 on error
 */
XCL_DRIVER_DLLESPEC
int xrtKernelClose(xrtKernelHandle kernelHandle);

/**
 * xrtKernelArgGroupId() - Acquire bank group id for kernel argument
 *
 * @kernelHandle: Handle to kernel previously opened with xrtKernelOpen
 * @argno:        Index of kernel argument
 * Return:        Group id or negative error code on error
 *
 * A valid group id is a non-negative integer.  The group id is required
 * when constructing a buffer object.
 *
 * The kernel argument group id is ambigious if kernel has multiple kernel
 * with different connectivity for specified argument.  In this case the
 * API returns error.
 */
XCL_DRIVER_DLLESPEC
int xrtKernelArgGroupId(xrtKernelHandle kernelHandle, int argno);

/**
 * xrtKernelArgOffset() - Get the offset of kernel argument
 *
 * @khdl:   Handle to kernel previously opened with xrtKernelOpen
 * @argno:  Index of kernel argument
 * Return:  The kernel register offset of the argument with specified index
 *
 * Use with ``xrtKernelReadRegister()`` and ``xrtKernelWriteRegister()``
 * if manually reading or writing kernel registers for explicit arguments.
 */
XCL_DRIVER_DLLESPEC
uint32_t
xrtKernelArgOffset(xrtKernelHandle khdl, int argno);

/**
 * xrtKernelReadRegister() - Read data from kernel address range
 *
 * @kernelHandle: Handle to kernel previously opened with xrtKernelOpen
 * @offset:       Offset in register space to read from
 * @datap:        Pointer to location where to write data
 * Return:        0 on success, errcode otherwise
 *
 * The kernel must be associated with exactly one kernel instance
 * (compute unit), which must be opened for exclusive access.
 */
XCL_DRIVER_DLLESPEC
int xrtKernelReadRegister(xrtKernelHandle kernelHandle, uint32_t offset, uint32_t *datap);

/**
 * xrtKernelWriteRegister() - Write to the address range of a kernel
 *
 * @kernelHandle: Handle to kernel previously opened with xrtKernelOpen
 * @offset:       Offset in register space to write to
 * @data:         Data to write
 * Return:        0 on success, errcode otherwise
 *
 * The kernel must be associated with exactly one kernel instance
 * (compute unit), which must be opened for exclusive access.
 */
XCL_DRIVER_DLLESPEC
int xrtKernelWriteRegister(xrtKernelHandle kernelHandle, uint32_t offset, uint32_t data);

/**
 * xrtKernelRun() - Start a kernel execution
 *
 * @kernelHandle: Handle to the kernel to run
 * @...:          Kernel arguments
 * Return:        Run handle which must be closed with xrtRunClose()
 *
 * A run handle is specific to one execution of a kernel.  Once
 * execution completes, the run handle can be re-used to execute the
 * same kernel again.  When no longer needed, then run handle must be
 * closed with xrtRunClose().
 */
XCL_DRIVER_DLLESPEC
xrtRunHandle
xrtKernelRun(xrtKernelHandle kernelHandle, ...);

/**
 * xrtRunOpen() - Open a new run handle for a kernel without starting kernel
 *
 * @kernelHandle: Handle to the kernel to associate the run handle with
 * Return:        Run handle which must be closed with xrtRunClose()
 *
 * The handle can be used repeatedly to start an execution of the
 * associated kernel.  This API allows application to manage run
 * handles without maintaining corresponding kernel handle.
 */
XCL_DRIVER_DLLESPEC
xrtRunHandle
xrtRunOpen(xrtKernelHandle kernelHandle);

/**
 * xrtRunSetArg() - Set a specific kernel argument for this run
 *
 * @rhdl:       Handle to the run object to modify
 * @index:      Index of kernel argument to set
 * @...:        The argument value to set.
 * Return:      0 on success, -1 on error
 *
 * Use this API to explicitly set specific kernel arguments prior
 * to starting kernel execution.  After setting all arguments, the
 * kernel execution can be start with xrtRunStart()
 */
XCL_DRIVER_DLLESPEC
int xrtRunSetArg(xrtRunHandle rhdl, int index, ...);

/**
 * xrtRunUpdateArg() - Asynchronous update of kernel argument
 *
 * @rhdl:       Handle to the run object to modify
 * @index:      Index of kernel argument to update
 * @...:        The argument value to update.
 * Return:      0 on success, -1 on error
 *
 * Use this API to asynchronously update a specific kernel
 * argument of an existing run.
 *
 * This API is only supported on Edge.
 */
XCL_DRIVER_DLLESPEC
int xrtRunUpdateArg(xrtRunHandle rhdl, int index, ...);

/**
 * xrtRunStart() - Start existing run handle
 *
 * @rhdl:       Handle to the run object to start
 * Return:      0 on success, -1 on error
 *
 * Use this API when re-using a run handle for more than one execution
 * of the kernel associated with the run handle.
 */
XCL_DRIVER_DLLESPEC
int xrtRunStart(xrtRunHandle rhdl);

/**
 * xrtRunWait() - Wait for a run to complete
 *
 * @rhdl:       Handle to the run object to start
 * Return:      Run command state for completed run,
 *              or ERT_CMD_STATE_ABORT on error
 *
 * Blocks current thread until job has completed
 */
XCL_DRIVER_DLLESPEC
enum ert_cmd_state
xrtRunWait(xrtRunHandle rhdl);

/**
 * xrtRunWait() - Wait for a run to complete
 *
 * @rhdl:       Handle to the run object to start
 * @timeout_ms: Timeout in millisecond
 * Return:      Run command state for completed run, or
 *              current status if timeout.
 *
 * Blocks current thread until job has completed
 */
XCL_DRIVER_DLLESPEC
enum ert_cmd_state
xrtRunWaitFor(xrtRunHandle rhdl, unsigned int timeout_ms);

/**
 * xrtRunState() - Check the current state of a run
 *
 * @rhdl:       Handle to check
 * Return:      The underlying command execution state per ert.h
 */
XCL_DRIVER_DLLESPEC
enum ert_cmd_state
xrtRunState(xrtRunHandle rhdl);

/**
 * xrtRunSetCallback() - Set a callback function
 *
 * @rhdl:        Handle to set callback on
 * @state:       State to invoke callback on
 * @callback:    Callback function
 * @data:        User data to pass to callback function
 *
 * Register a run callback function that is invoked when the
 * run changes underlying execution state to specified state.
 * Support states are: ERT_CMD_STATE_COMPLETED (to be extended)
 */
XCL_DRIVER_DLLESPEC
int xrtRunSetCallback(xrtRunHandle rhdl, enum ert_cmd_state state,
                      void (*callback)(xrtRunHandle, enum ert_cmd_state, void *),
                      void *data);

/**
 * xrtRunClose() - Close a run handle
 *
 * @rhdl:  Handle to close
 * Return:      0 on success, -1 on error
 */
XCL_DRIVER_DLLESPEC
int xrtRunClose(xrtRunHandle rhdl);

/// @endcond
#ifdef __cplusplus
}
#endif

#endif  // MINI_XRT_H
