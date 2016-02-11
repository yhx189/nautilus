#ifndef __VIRTIO_PCI
#define __VIRTIO_PCI

#define MAX_VRINGS 4

enum virtio_pci_dev_type { VIRTIO_PCI_NET, VIRTIO_PCI_BLOCK, VIRTIO_PCI_OTHER };

struct virtio_pci_vring {
  uint64_t size_bytes;
  uint8_t *data ;
  uint8_t *aligned_data;
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
