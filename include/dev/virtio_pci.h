#ifndef __VIRTIO_PCI
#define __VIRTIO_PCI

#include <dev/virtio_ring.h>

#define MAX_VRINGS 4

enum virtio_pci_dev_type { VIRTIO_PCI_NET, VIRTIO_PCI_BLOCK, VIRTIO_PCI_OTHER };

struct virtio_packet_data{
  uint8_t src[6];
  uint8_t dst[6];
  uint8_t type[2];
  uint8_t data[1000];
};

struct virtio_packet_hdr{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
  uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE 0
#define VIRTIO_NET_HDR_GSO_TCPV4 1
#define VIRTIO_NET_HDR_GSO_UDP 3
#define VIRTIO_NET_HDR_GSO_TCPV6 4
#define VIRTIO_NET_HDR_GSO_ECN 0x80
  uint8_t gso_type;
  uint16_t hdr_len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
  uint16_t num_buffers;
};

struct virtio_packet{
   struct virtio_packet_hdr hdr;
   struct virtio_packet_data data;
};


struct virtio_pci_vring {
  uint64_t size_bytes;
  // pointer to unaligned address supplied by nautilus
  // for memory management later (free)
  uint8_t *data ;
  // aligned start of vring within data
  uint8_t *aligned_data;

  // compatability with support code supplied in virtio docs
  // the pointers here go into aligned_data
  struct virtq vq;
  // for processing respones 
  unsigned int last_seen_used;
};

struct virtio_pci_dev {
  enum virtio_pci_dev_type type;
  char name[32];

  // for our linked list of virtio devices
  struct list_head virtio_node;

  struct pci_dev *pci_dev;

  uint8_t   pci_intr;  // number on bus
  uint8_t   intr_vec;  // number we will see

  // Where registers are mapped into the I/O address space
  uint16_t  ioport_start;
  uint16_t  ioport_end;  

  // Where registers are mapped into the physical memory address space
  uint64_t  mem_start;
  uint64_t  mem_end;

  // The number of vrings in use
  uint8_t num_vrings;
  struct virtio_pci_vring vring[MAX_VRINGS];
};

int virtio_pci_init(struct naut_info * naut);
int virtio_pci_deinit();


#endif