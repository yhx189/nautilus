/* 
 * This file is part of the Nautilus AeroKernel developed
 * by the Hobbes and V3VEE Projects with funding from the 
 * United States National  Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  The Hobbes Project is a collaboration
 * led by Sandia National Laboratories that includes several national 
 * laboratories and universities. You can find out more at:
 * http://www.v3vee.org  and
 * http://xtack.sandia.gov/hobbes
 *
 * Copyright (c) 2015, Kyle C. Hale <kh@u.northwestern.edu>
 * Copyright (c) 2015, The V3VEE Project  <http://www.v3vee.org> 
 *                     The Hobbes Project <http://xstack.sandia.gov/hobbes>
 * All rights reserved.
 *
 * Author: Kyle C. Hale <kh@u.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "LICENSE.txt".
 */
#ifndef __PCI_H__
#define __PCI_H__

#include <nautilus/list.h>

#define PCI_CFG_ADDR_PORT 0xcf8
#define PCI_CFG_DATA_PORT 0xcfc

#define PCI_SLOT_SHIFT 11
#define PCI_BUS_SHIFT  16
#define PCI_FUN_SHIFT  8

#define PCI_REG_MASK(x) ((x) & 0xfc)

#define PCI_ENABLE_BIT 0x80000000UL

#define PCI_MAX_BUS 256
#define PCI_MAX_DEV 32
#define PCI_MAX_FUN 8

#define PCI_CLASS_LEGACY  0x0
#define PCI_CLASS_STORAGE 0x1
#define PCI_CLASS_NET     0x2
#define PCI_CLASS_DISPLAY 0x3
#define PCI_CLASS_MULTIM  0x4
#define PCI_CLASS_MEM     0x5
#define PCI_CLASS_BRIDGE  0x6
#define PCI_CLASS_SIMPLE  0x7
#define PCI_CLASS_BSP     0x8
#define PCI_CLASS_INPUT   0x9
#define PCI_CLASS_DOCK    0xa
#define PCI_CLASS_PROC    0xb
#define PCI_CLASS_SERIAL  0xc
#define PCI_CLASS_WIRELESS 0xd
#define PCI_CLASS_INTIO    0xe
#define PCI_CLASS_SAT      0xf
#define PCI_CLASS_CRYPTO   0x10
#define PCI_CLASS_SIG      0x11
#define PCI_CLASS_NOCLASS  0xff


#define PCI_SUBCLASS_BRIDGE_PCI 0x4

/* PCI Device Capabilities */
#define  PCI_CAP_ID_PM          0x01    /* Power Management */
#define  PCI_CAP_ID_AGP         0x02    /* Accelerated Graphics Port */
#define  PCI_CAP_ID_VPD         0x03    /* Vital Product Data */
#define  PCI_CAP_ID_SLOTID      0x04    /* Slot Identification */
#define  PCI_CAP_ID_MSI         0x05    /* Message Signalled Interrupts */
#define  PCI_CAP_ID_CHSWP       0x06    /* CompactPCI HotSwap */
#define  PCI_CAP_ID_PCIX        0x07    /* PCI-X */
#define  PCI_CAP_ID_HT          0x08    /* HyperTransport */
#define  PCI_CAP_ID_VNDR        0x09    /* Vendor-Specific */
#define  PCI_CAP_ID_DBG         0x0A    /* Debug port */
#define  PCI_CAP_ID_CCRC        0x0B    /* CompactPCI Central Resource Control */
#define  PCI_CAP_ID_SHPC        0x0C    /* PCI Standard Hot-Plug Controller */
#define  PCI_CAP_ID_SSVID       0x0D    /* Bridge subsystem vendor/device ID */
#define  PCI_CAP_ID_AGP3        0x0E    /* AGP Target PCI-PCI bridge */
#define  PCI_CAP_ID_SECDEV      0x0F    /* Secure Device */
#define  PCI_CAP_ID_EXP         0x10    /* PCI Express */
#define  PCI_CAP_ID_MSIX        0x11    /* MSI-X */
#define  PCI_CAP_ID_SATA        0x12    /* SATA Data/Index Conf. */
#define  PCI_CAP_ID_AF          0x13    /* PCI Advanced Features */
#define  PCI_CAP_ID_EA          0x14    /* PCI Enhanced Allocation */
#define  PCI_CAP_ID_MAX         PCI_CAP_ID_EA
#define PCI_CAP_LIST_NEXT       1       /* Next capability in the list */
#define PCI_CAP_FLAGS           2       /* Capability defined flags (16 bits) */
#define PCI_CAP_SIZEOF          4


/* MSI */
#define PCI_MSI_FLAGS           2       /* Message Control */
#define  PCI_MSI_FLAGS_ENABLE   0x0001  /* MSI feature enabled */
#define  PCI_MSI_FLAGS_QMASK    0x000e  /* Maximum queue size available */
#define  PCI_MSI_FLAGS_QSIZE    0x0070  /* Message queue size configured */
#define  PCI_MSI_FLAGS_64BIT    0x0080  /* 64-bit addresses allowed */
#define  PCI_MSI_FLAGS_MASKBIT  0x0100  /* Per-vector masking capable */
#define PCI_MSI_RFU             3       /* Rest of capability flags */
#define PCI_MSI_ADDRESS_LO      4       /* Lower 32 bits */
#define PCI_MSI_ADDRESS_HI      8       /* Upper 32 bits (if PCI_MSI_FLAGS_64BIT set) */
#define PCI_MSI_DATA_32         8       /* 16 bits of data for 32-bit devices */
#define PCI_MSI_MASK_32         12      /* Mask bits register for 32-bit devices */
#define PCI_MSI_PENDING_32      16      /* Pending intrs for 32-bit devices */
#define PCI_MSI_DATA_64         12      /* 16 bits of data for 64-bit devices */
#define PCI_MSI_MASK_64         16      /* Mask bits register for 64-bit devices */
#define PCI_MSI_PENDING_64      20      /* Pending intrs for 64-bit devices */
 
/* MSI-X */
#define PCI_MSIX_FLAGS          2       /* Message Control */
#define  PCI_MSIX_FLAGS_QSIZE   0x07FF  /* Table size */
#define  PCI_MSIX_FLAGS_MASKALL 0x4000  /* Mask all vectors for this function */
#define  PCI_MSIX_FLAGS_ENABLE  0x8000  /* MSI-X enable */
#define PCI_MSIX_TABLE          4       /* Table offset */
#define  PCI_MSIX_TABLE_BIR     0x00000007 /* BAR index */
#define  PCI_MSIX_TABLE_OFFSET  0xfffffff8 /* Offset into specified BAR */
#define PCI_MSIX_PBA            8       /* Pending Bit Array offset */
#define  PCI_MSIX_PBA_BIR       0x00000007 /* BAR index */
#define  PCI_MSIX_PBA_OFFSET    0xfffffff8 /* Offset into specified BAR */
#define PCI_MSIX_FLAGS_BIRMASK  PCI_MSIX_PBA_BIR /* deprecated */
#define PCI_CAP_MSIX_SIZEOF     12      /* size of MSIX registers */

#define PCI_MSIX_ENTRY_SIZE             16
#define  PCI_MSIX_ENTRY_LOWER_ADDR      0
#define  PCI_MSIX_ENTRY_UPPER_ADDR      4
#define  PCI_MSIX_ENTRY_DATA            8
#define  PCI_MSIX_ENTRY_VECTOR_CTRL     12
#define   PCI_MSIX_ENTRY_CTRL_MASKBIT   1


struct naut_info; 

struct pci_bus {
    uint32_t num;
    struct list_head bus_node;
    struct list_head dev_list;
    struct pci_info * pci;
};


struct pci_cfg_space {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t cmd;
    uint16_t status;
    uint8_t  rev_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cl_size;
    uint8_t  lat_timer;
    uint8_t  hdr_type;
    uint8_t  bist;

    union {
        // type = 00h (device)
        struct {
            uint32_t bars[6];
            uint32_t cardbus_cis_ptr;
            uint16_t subsys_vendor_id;
            uint16_t subsys_id;
            uint32_t exp_rom_bar;
            uint8_t  cap_ptr;
            uint8_t  rsvd[7];
            uint8_t  intr_line;
            uint8_t  intr_pin;
            uint8_t  min_grant;
            uint8_t  max_latency;
            uint32_t data[48];
        } __packed dev_cfg;

        // type = 01h (PCI-to-PCI bridge)
        struct {
            uint32_t bars[2];
            uint8_t  primary_bus_num;
            uint8_t  secondary_bus_num;
            uint8_t  sub_bus_num;
            uint8_t  secondary_lat_timer;
            uint8_t  io_base;
            uint8_t  io_limit;
            uint16_t secondary_status;
            uint16_t mem_base;
            uint16_t mem_limit;
            uint16_t prefetch_mem_base;
            uint16_t prefetch_mem_limit;
            uint32_t prefetch_base_upper;
            uint32_t prefetch_limit_upper;
            uint16_t io_base_upper;
            uint16_t io_limit_upper;
            uint8_t  cap_ptr;
            uint8_t  rsvd[3];
            uint32_t exp_rom_bar;
            uint8_t  intr_line;
            uint8_t  intr_pin;
            uint16_t bridge_ctrl;
            uint32_t data[48];
        } __packed pci_to_pci_bridge_cfg;

        struct {
            uint32_t cardbus_socket_bar;
            uint8_t  cap_list_offset;
            uint8_t  rsvd;
            uint16_t secondary_status;
            uint8_t  pci_bus_num;
            uint8_t  cardbus_bus_num;
            uint8_t  sub_bus_num;
            uint8_t  cardbus_lat_timer;
            uint32_t mem_base0;
            uint32_t mem_limit0;
            uint32_t mem_base1;
            uint32_t mem_limit1;
            uint32_t io_base0;
            uint32_t io_limit0;
            uint32_t io_base1;
            uint32_t io_limit1;
            uint8_t  intr_line;
            uint8_t  intr_pin;
            uint16_t bridge_ctrl;
            uint16_t subsys_dev_id;
            uint16_t subsys_vendor_id;
            uint32_t legacy_base;
            uint32_t rsvd1[14];
            uint32_t data[32];
        } __packed cardbus_bridge_cfg;

    } __packed;

} __packed;


struct pci_dev {
    uint32_t num;
    struct pci_bus * bus;
    struct list_head dev_node;
    struct pci_cfg_space cfg;
};


struct pci_info {
    uint32_t num_buses;
    struct list_head bus_list;
};



uint16_t pci_cfg_readw(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t off);
uint32_t pci_cfg_readl(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t off);

void pci_cfg_writew(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t off, uint16_t val);
void pci_cfg_writel(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t off, uint32_t val);

int pci_cfg_has_capability(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t cap);
int pci_cfg_set_capability(uint8_t bus, uint8_t slot, uint8_t fun, uint8_t cap, uint8_t val);

int pci_init (struct naut_info * naut);


#endif
