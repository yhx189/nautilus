#include <nautilus/nautilus.h>
#include <dev/pci.h>
#include <dev/virtio_pci.h>
#include <dev/virtio_ring.h>
#include <dev/apic.h>
#include <nautilus/irq.h>
#include <nautilus/backtrace.h>
#ifndef NAUT_CONFIG_DEBUG_VIRTIO_PCI
#undef DEBUG_PRINT
#define DEBUG_PRINT(fmt, args...)
#endif

// set to 1 to use memory mapped regs
// set to 0 to use ioport mapped regs
// leave at 0 for time being...
#define ACCESS_VIA_MEM 0

#define INFO(fmt, args...) printk("VIRTIO_PCI: " fmt, ##args)
#define DEBUG(fmt, args...) DEBUG_PRINT("VIRTIO_PCI: DEBUG: " fmt, ##args)
#define ERROR(fmt, args...) printk("VIRTIO_PCI: ERROR: " fmt, ##args)

// list of virtio devices we are managing
static struct list_head dev_list;
static struct virtio_pci_dev * p_dev;
static struct list_head tx_queue;

static struct apic_dev * apic;
// common register offsets
#define DEVICE_FEATURES 0x0    // 4 byte
#define GUEST_FEATURES  0x4    // 4 byte
#define QUEUE_ADDR      0x8    // 4 byte
#define QUEUE_SIZE      0xc    // 2 byte
#define QUEUE_SEL       0xe    // 2 byte
#define QUEUE_NOTIFY    0x10   // 2 byte
#define DEVICE_STATUS   0x12   // 1 byte
#define ISR_STATUS      0x13   // 1 byte
// #define CONFIG_VEC    0x14
// #define QUEUE_VEC     0x15
#define MAC_ADDR_1      0x14   // 1 byte
#define MAC_ADDR_2      0x15   // 1 byte
#define MAC_ADDR_3      0x16   // 1 byte
#define MAC_ADDR_4      0x17   // 1 byte
#define MAC_ADDR_5      0x18   // 1 byte
#define MAC_ADDR_6      0x19   // 1 byte
#define MAC_STATUS      0x1a   // 1 byte

#define RECEIVE_QUEUE   0x0
#define TRANSMIT_QUEUE  0x1
inline static uint32_t read_regl(struct virtio_pci_dev *dev, uint32_t offset)
{
  uint32_t result;
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single read
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  //  DEBUG("addr=%p\n",addr);
  __asm__ __volatile__ ("movl (%1),%0"
			: "=r"(result)
			: "r"(addr)
			: "memory");
  return result;
#else
  return inl(dev->ioport_start+offset);
#endif
}


inline static void write_regl(struct virtio_pci_dev *dev, uint32_t offset, uint32_t data)
{
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single write
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  __asm__ __volatile__ ("movl %1, (%0)"
			: "=r"(addr)
			: "r"(data)
			: "memory");
#else
  outl(data,dev->ioport_start+offset);
#endif
}

uint32_t read_regw(struct virtio_pci_dev *dev, uint32_t offset)
{
  uint16_t result;
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single read
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  __asm__ __volatile__ ("movw (%1),%0"
			: "=r"(result)
			: "r"(addr)
			: "memory");
  return result;
#else
  return inw(dev->ioport_start+offset);
#endif
}

inline void write_regw(struct virtio_pci_dev *dev, uint32_t offset, uint16_t data)
{
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single write
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  __asm__ __volatile__ ("movw %1, (%0)"
			: "=r"(addr)
			: "r"(data)
			: "memory");
#else
  outw(data,dev->ioport_start+offset);
#endif
}

inline static uint32_t read_regb(struct virtio_pci_dev *dev, uint32_t offset)
{
  uint8_t result;
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single read
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  __asm__ __volatile__ ("movb (%1),%0"
			: "=r"(result)
			: "r"(addr)
			: "memory");
  return result;
#else
  return inb(dev->ioport_start+offset);
#endif
}

inline static void write_regb(struct virtio_pci_dev *dev, uint32_t offset, uint8_t data)
{
#if ACCESS_VIA_MEM
  // we want to be assured that we are doing a single write
  // without any compiler nonsense
  uint64_t addr = dev->mem_start + offset;
  __asm__ __volatile__ ("movb %1, (%0)"
			: "=r"(addr)
			: "r"(data)
			: "memory");
#else
  outb(data,dev->ioport_start+offset);
#endif
}


static int discover_devices(struct pci_info *pci)
{
  struct list_head *curbus, *curdev;
  int num=0;

  DEBUG("Discovering and naming virtio devices\n");

  INIT_LIST_HEAD(&dev_list);

  if (!pci) { 
    ERROR("No PCI info\n");
    return -1;
  }


  struct virtio_pci_dev *vdev;
  list_for_each(curbus,&(pci->bus_list)) { 
    struct pci_bus *bus = list_entry(curbus,struct pci_bus,bus_node);

    DEBUG("Searching PCI bus %u for Virtio devices\n", bus->num);

    list_for_each(curdev, &(bus->dev_list)) { 
      struct pci_dev *pdev = list_entry(curdev,struct pci_dev,dev_node);
      struct pci_cfg_space *cfg = &pdev->cfg;

      DEBUG("Device %u is a %x:%x\n", pdev->num, cfg->vendor_id, cfg->device_id);

      if (cfg->vendor_id==0x1af4 && cfg->device_id>=0x1000 && cfg->device_id<=0x103f) {
	DEBUG("Virtio Device Found (subsys_id=0x%x)\n",cfg->dev_cfg.subsys_id);

	vdev = malloc(sizeof(struct virtio_pci_dev));
	if (!vdev) {
	  ERROR("Cannot allocate device\n");
	  return -1;
	}

	memset(vdev,0,sizeof(*vdev));
	
	vdev->pci_dev = pdev;

	switch (cfg->dev_cfg.subsys_id) { 
	case 0x1:
	  DEBUG("Net Device\n");
	  vdev->type = VIRTIO_PCI_NET;
	  break;
	case 0x2:
	  DEBUG("Block Device\n");
	  vdev->type = VIRTIO_PCI_BLOCK;
	  break;
	default:
	  DEBUG("Other Device\n");
	  vdev->type = VIRTIO_PCI_OTHER;
	  break;
	}

	snprintf(vdev->name,32, "virtio-%d-%s", num, 
		 vdev->type==VIRTIO_PCI_NET ? "net" :
		 vdev->type==VIRTIO_PCI_BLOCK ? "block" :
		 vdev->type==VIRTIO_PCI_OTHER ? "other" : "UNKNOWN");

        printk("%s\n", vdev->name);
	// PCI Interrupt (A..D)
	vdev->pci_intr = cfg->dev_cfg.intr_pin;
	// Figure out mapping here or look at capabilities for MSI-X
	// vdev->intr_vec = ...

	// we expect two bars exist, one for memory, one for i/o
	// and these will be bar 0 and 1
	// check to see if there are no others
	for (int i=0;i<6;i++) { 
	  uint32_t bar = pci_cfg_readl(bus->num,pdev->num, 0, 0x10 + i*4);
	  uint32_t size;
	  DEBUG("bar %d: 0x%0x\n",i, bar);
	  if (i>=2 && bar!=0) { 
	    DEBUG("Not expecting this to be a non-empty bar...\n");
	  }
	  if (!(bar & 0x1)) { 
	    // handle only 32 bit memory for now
	    uint8_t mem_bar_type = (bar & 0x6) >> 1;
	    if (mem_bar_type != 0) { 
	      ERROR("Cannot handle memory bar type 0x%x\n", mem_bar_type);
	      return -1;
	    }
	  }

	  // determine size
	  // write all 1s, get back the size mask
	  pci_cfg_writel(bus->num,pdev->num,0,0x10 + i*4, 0xffffffff);
	  // size mask comes back + info bits
	  size = pci_cfg_readl(bus->num,pdev->num,0,0x10 + i*4);

	  // mask all but size mask
	  if (bar & 0x1) { 
	    // I/O
	    size &= 0xfffffffc;
	  } else {
	    // memory
	    size &= 0xfffffff0;
	  }
	  size = ~size;
	  size++; 

	  // now we have to put back the original bar
	  pci_cfg_writel(bus->num,pdev->num,0,0x10 + i*4, bar);

	  if (!size) { 
	    // non-existent bar, skip to next one
	    continue;
	  }

	  if (size>0 && i>=2) { 
	    ERROR("unexpected virtio pci bar with size>0!\n");
	    return -1;
	  }
	  
	  if (bar & 0x1) { 
	    vdev->ioport_start = bar & 0xffffffc0;
	    vdev->ioport_end = vdev->ioport_start + size;
	  } else {
	    vdev->mem_start = bar & 0xfffffff0;
	    vdev->mem_end = vdev->mem_start + size;
	  }

	}

	// Now we need to figure out its interrupt
	if (pci_cfg_has_capability(bus->num,pdev->num,0,PCI_CAP_ID_MSIX)) { 
	  DEBUG("device supports MSI-X\n");
	} else {
	  DEBUG("device does not support MSI-X\n");
	}
	

	INFO("Adding virtio %s device with name %s : bus=%u dev=%u func=%u: pci_intr=%u intr_vec=%u ioport_start=%p ioport_end=%p mem_start=%p mem_end=%p\n",
	     vdev->type==VIRTIO_PCI_BLOCK ? "block" :
	     vdev->type==VIRTIO_PCI_NET ? "net" : "other",
	     vdev->name,
	     bus->num, pdev->num, 0,
	     vdev->pci_intr, vdev->intr_vec,
	     vdev->ioport_start, vdev->ioport_end,
	     vdev->mem_start, vdev->mem_end);
	     
        list_add_tail(&vdev->virtio_node, &dev_list);
	list_add(&vdev->virtio_node,&(bus->dev_list));
	num++;

      }
    }
  }

  return 0;
}


#define ALIGN(x) (((x) + 4095UL) & ~4095UL) 

#define NUM_PAGES(x) ((x)/4096 + !!((x)%4096))


static inline unsigned compute_size(unsigned int qsz) 
{ 
     return ALIGN(sizeof(struct virtq_desc)*qsz + sizeof(uint16_t)*(3 + qsz)) 
          + ALIGN(sizeof(uint16_t)*3 + sizeof(struct virtq_used_elem)*qsz); 
}

int virtio_ring_init(struct virtio_pci_dev *dev)
{
  uint16_t i;
  uint64_t qsz;
  uint64_t qsz_numbytes;
  uint64_t alloc_size;
  

  DEBUG("Ring init of %s\n",dev->name);

  // now let's figure out the ring sizes
  dev->num_vrings=0;
  for (i=0;i<MAX_VRINGS;i++) {
    write_regw(dev,QUEUE_SEL,i);
    qsz = read_regw(dev,QUEUE_SIZE);
    if (qsz==0) {
      // out of queues to support
      break;
    }
    INFO("Ring %u has 0x%lx slots\n", i, qsz);
    qsz_numbytes = compute_size(qsz);
    INFO("Ring %u has size 0x%lx bytes\n", i, qsz_numbytes);


    dev->vring[i].size_bytes = qsz_numbytes;
    alloc_size = 4096 * (NUM_PAGES(qsz_numbytes) + 1);

    if (!(dev->vring[i].data = malloc(alloc_size))) {
      ERROR("Cannot allocate ring\n");
      return -1;
    }

    memset(dev->vring[i].data,0,alloc_size);

    dev->vring[i].aligned_data = (uint8_t *)ALIGN((uint64_t)(dev->vring[i].data));
    dev->vring[i].vq.num = qsz;

    dev->vring[i].vq.desc = (struct virtq_desc *) (dev->vring[i].aligned_data);

    dev->vring[i].vq.avail = (struct virtq_avail *) 
      (dev->vring[i].aligned_data 
       + sizeof(struct virtq_desc)*qsz);

    dev->vring[i].vq.used = (struct virtq_used *) 
      (dev->vring[i].aligned_data
       +  ALIGN(sizeof(struct virtq_desc)*qsz + sizeof(uint16_t)*(3 + qsz))); 


    DEBUG("ring allocation at %p for 0x%lx bytes\n", dev->vring[i].data,alloc_size);
    DEBUG("ring data at %p\n", dev->vring[i].aligned_data);
    DEBUG("ring num  = 0x%lx\n",dev->vring[i].vq.num);
    DEBUG("ring desc at %p\n", dev->vring[i].vq.desc);
    DEBUG("ring avail at %p\n", dev->vring[i].vq.avail);
    DEBUG("ring used at %p\n", dev->vring[i].vq.used);
    
    // now tell device about the ring
    // note it's a 32 bit register, but the address is a page address
    // so it really represents a 44 bit address (32 bits * 4096)
    write_regl(dev,QUEUE_ADDR,(uint32_t)(((uint64_t)(dev->vring[i].aligned_data))/4096));

    dev->num_vrings++;
  }

  if (i==MAX_VRINGS) { 
    ERROR("Device needs to many rings\n");
    return -1;
  }
    
  return 0;
}

// a descriptor with len 0 will denote
// it is free
// we return -1 if the allocation cannot occur
static uint32_t allocate_descriptor(volatile struct virtq *vq)
{
  uint32_t i;

  // this is hideous
  for (i=0;i<vq->num;i++) { 
    if (!vq->desc[i].len) { 
      // set to nonzero sentinal value
      vq->desc[i].len=0xdeadbeef;
      DEBUG("Allocate descriptor %u\n",i);
      return i;
    }
  }
  return -1;
}

static void free_descriptor(volatile struct virtq *vq, uint32_t i)
{
  DEBUG("Free descriptor %u\n",i);
  if (!vq->desc[i].len) { 
    DEBUG("Warning: descriptor already appears freed\n");
  }
  vq->desc[i].len=0;
}

// Returns 0 if we are able to place the request
// into a descriptor and queue it to the avail ring
// returns nonzero if this fails
static int virtio_enque_request(struct virtio_pci_dev *dev,
				uint32_t ring, 
				uint64_t addr, 
				uint32_t len, 
				uint16_t flags)
{
  volatile struct virtq *vq = &dev->vring[ring].vq;

  uint32_t i;
  
  i = allocate_descriptor(vq);
  if (i==-1) { 
    return -1;
  }
  
  vq->desc[i].addr=addr;
  vq->desc[i].len=len;
  vq->desc[i].flags=flags;
  vq->desc[i].next= 0; //(i+1) % vq->num;
  DEBUG("addr: %x\n", addr);
  DEBUG("len: %d\n", len);
  DEBUG("flags: %x\n", flags);
  DEBUG("next: %d\n",  vq->desc[i].next);
  vq->avail->ring[vq->avail->idx % vq->num] = i;
  
  DEBUG("enqueue finished\n"); 
  return 0;
}

// Processing outstanding responses
// calling the callback function for each one.  
// the arguments to the callback are the elements of 
// the original corresponding request
static int virtio_dequeue_responses(struct virtio_pci_dev *dev,
				    uint32_t ring,
				    int (*callback)(struct virtio_pci_dev *dev,
						    uint32_t ring,
						    uint64_t addr,
						    uint32_t len,
						    uint16_t flags))
{
 
 
  struct virtio_pci_vring *vring = &dev->vring[ring];
  volatile struct virtq *vq = &vring->vq;
  uint16_t avail_flags;

  avail_flags = vq->avail->flags;

  // disable interrupts
  vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;

  while (1) { 

    if (vring->last_seen_used != vq->used->idx ) {

      __asm__ __volatile__ ("" : : : "memory"); // sw mem barrier
      __sync_synchronize(); // hw mem barrier

      // restore interrupt state to whatever it was previously
      vq->avail->flags = avail_flags;

      // check again
      if (vring->last_seen_used != vq->used->idx) {
	break;
      }
    } 
    
    struct virtq_used_elem *e = &(vq->used->ring[vring->last_seen_used % vq->num]);

    if (e->len!=1) { 
      DEBUG("Surprising len %u response\n", e->len);
    }

    
    if (callback(dev,
		 ring,
		 vq->desc[e->id].addr,
		 vq->desc[e->id].len,
		 vq->desc[e->id].flags)) {
      DEBUG("Surprising nonzero return from callback\n");
    }

    free_descriptor(vq,e->id);

    vring->last_seen_used++;
  }
  return 0;
}

static int virtio_block_init(struct virtio_pci_dev *dev)
{

  uint32_t val;

  DEBUG("Block init of %s\n",dev->name);

  write_regb(dev,DEVICE_STATUS,0x0); // driver resets device
  write_regb(dev,DEVICE_STATUS,0b1); // driver acknowledges device
  write_regb(dev,DEVICE_STATUS,0b11); // driver can drive device

  val = read_regl(dev,DEVICE_FEATURES);
  DEBUG("device features: 0x%0x\n",val);



  return 0;
}

static int rx_handler(excp_entry_t * entry, excp_vec_t vec)
{
  DEBUG("RX HANDLER  INTERRUPT FIRED;  \n");
  //IRQ_HANDLER_END();
  //write_regb(p_dev, ISR_STATUS, 0); 
   
  /* disable further interrupts */
   
  uint32_t ring = RECEIVE_QUEUE;
  struct virtio_pci_vring *vring = &p_dev->vring[ring];
  volatile struct virtq *vq = &vring->vq;


  // disable interrupts
  vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;

  // process packets
  struct virtq_used_elem *e = &(vq->used->ring[vring->last_seen_used % vq->num]);

  if (e->len < 1) { 
      DEBUG("Surprising len %u response\n", e->len);
    }

  DEBUG("received buffer:\n");
  DEBUG("id is %d, length is %d\n", e->id, e->len);
  uint32_t i = 0;
  uint64_t *p = (uint64_t *)vq->desc[e->id].addr;
  uint64_t s[vq->desc[e->id].len];
  for(i = 0; i < vq->desc[e->id].len; i++){
  	printk("%x", *p);
	p++;
  }
  //free_descriptor(vq,e->id);
  printk("\n");
  /* re-enable interrupts */ 
  vq->avail->flags = 0;
   

  return 0;
}


// merge tx and rx in a single handler
static int tx_handler(excp_entry_t* entry, excp_vec_t vec)
{
  /* read the ISR status reg, and reset it to 0 */
  DEBUG("TX HANDLER  INTERRUPT FIRED;  \n");
  IRQ_HANDLER_END();
  

  //return 0;
  //write_regb(p_dev, ISR_STATUS, 0); 
   
  /* disable further interrupts */
   
  uint32_t ring = TRANSMIT_QUEUE;
  struct virtio_pci_vring *vring = &p_dev->vring[ring];
  volatile struct virtq *vq = &vring->vq;


  // disable interrupts
  vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;

  /* free the descriptors */
  struct virtq_used_elem *e = &(vq->used->ring[vring->last_seen_used % vq->num]);

  if (e->len < 1) { 
      DEBUG("Surprising len %u response\n", e->len);
    }

  free_descriptor(vq,e->id);

  /* re-enable interrupts */ 
  vq->avail->flags = 0;
  return 0;
}

int packet_tx_async(struct virtio_net_state *state, uint64_t packet, uint32_t packet_len, int wait)
{
  
  INIT_LIST_HEAD(&tx_queue);
  struct list_head tx;
   

  struct virtio_packet* ptr = packet;
  list_add_tail(& (ptr->packet_node) , &tx_queue);
  struct list_head *curpacket;
  int num = 0; 
  list_for_each(curpacket, &tx_queue) { 
      uint64_t to_send = list_entry(curpacket, struct virtio_packet, packet_node);
      if(num == 0)
         packet_tx(state, to_send, sizeof(struct virtio_packet), 0);
      num++;
  }
     
  return 0;
}


int packet_tx(struct virtio_net_state *state, uint64_t packet, uint32_t packet_len, int wait)
//static int packet_tx(struct virtio_pci_dev *dev, uint64_t tx)
{
  struct virtio_pci_dev * dev;
  dev  = state->virtio_dev;
  uint64_t tx = (uint64_t) packet;

  uint32_t ring = TRANSMIT_QUEUE;
  uint64_t addr = (uint64_t)tx;
  uint32_t len = sizeof(struct virtio_packet_data);
  uint16_t flags = VIRTQ_DESC_F_NEXT; 
  volatile struct virtq *vq = &dev->vring[ring].vq;

  /* generate a virtio packet header */
  struct virtio_packet_hdr *hdr = malloc(sizeof(struct virtio_packet_hdr));
  memset(hdr, 0, sizeof(struct virtio_packet_hdr));
  hdr->hdr_len = sizeof(struct virtio_packet_hdr); 

  /* copy the address of the header and the payload into the avail ring */
  virtio_enque_request(dev, ring, (uint64_t)hdr,
	 (uint32_t)(sizeof(struct virtio_packet_hdr)),flags);//flags are set to be VIRTQ_DESC_F_NEXT
  
  virtio_enque_request(dev, ring, addr, len, 0);
  
  /* increment the avail ring buffer index */
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  vq->avail->idx++; // it is ok that this wraps around
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  
  /* notify the device */
  write_regw(dev, QUEUE_NOTIFY, TRANSMIT_QUEUE);
  DEBUG("notification sent %x\n", addr);
  /* debug output */
#ifdef DEBUG_MODE
  uint32_t j = 0;
  udelay(1000000);
  for(j=0; j<8; j++){
  uint32_t irr_status = apic_read(apic , APIC_GET_IRR(j));
  DEBUG("irr status: %x\n", irr_status);
  }
  for(j=0;j<8;j++){
  uint32_t isr_status = apic_read(apic , APIC_GET_ISR(j));
  DEBUG("isr status: %x\n", isr_status);
  
  }
  //nk_dump_mem(dev->mem_start, 32);

  uint32_t used_idx = dev->vring[ring].vq.used->idx;
  DEBUG("used->idx: %d\n", used_idx);	
  DEBUG("desc[0] addr: %x\n", dev->vring[ring].vq.desc[0].addr);
  DEBUG("desc[1] addr: %x\n", dev->vring[ring].vq.desc[1].addr);
  
#endif
  return 0;
}

int packet_rx_async(struct virtio_net_state *state, uint64_t packet, uint32_t packet_len, int wait)
{
   
  return 0;
}


int packet_rx(struct virtio_net_state *state, uint64_t packet, uint32_t packet_len, int wait)
//static int packet_rx(struct virtio_pci_dev *dev)
{
  struct virtio_pci_dev * dev;
  dev = state->virtio_dev;
  uint32_t ring = RECEIVE_QUEUE;
  uint32_t len = sizeof(struct virtio_packet);
  uint16_t flags = VIRTQ_DESC_F_NEXT; 
  
  uint32_t used_idx = dev->vring[ring].vq.used->idx;
  //DEBUG("RX: used->idx: %d\n", used_idx);
   
  uint32_t avail_idx = dev->vring[ring].vq.avail->idx;
  //DEBUG("RX: avail->idx: %d\n", avail_idx);
  uint32_t j = 0;
#ifdef DEBUG_MODE
 /* increment the avail ring buffer index and notify the device*/
  uint32_t num_buf = 655 / sizeof(struct virtio_packet_data) + 1;
  for(j=0; j < 1;j++ ){
    	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
 	dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  	
 }
       write_regw(dev, QUEUE_NOTIFY, RECEIVE_QUEUE);
  for(j=0; j<8; j++){
  uint32_t irr_status = apic_read(apic , APIC_GET_IRR(j));
  DEBUG("irr status: %x\n", irr_status);
  }
  for(j=0;j<8;j++){
  uint32_t isr_status = apic_read(apic , APIC_GET_ISR(j));
  DEBUG("isr status: %x\n", isr_status);
  
  }
#endif

   return 0;
}


static int virtio_net_set_mac_address(struct virtio_pci_dev *dev)
{
  DEBUG("Setting MAC address of %s\n", dev->name);

  // These values can be modified
  uint8_t MACbyte1 = 0x22, MACbyte2 = 0xf0, MACbyte3 = 0x1d,
          MACbyte4 = 0xbe, MACbyte5 = 0xfe, MACbyte6 = 0xed;

  write_regb(dev, MAC_ADDR_1, MACbyte1);
  write_regb(dev, MAC_ADDR_2, MACbyte2);
  write_regb(dev, MAC_ADDR_3, MACbyte3);
  write_regb(dev, MAC_ADDR_4, MACbyte4);
  write_regb(dev, MAC_ADDR_5, MACbyte5);
  write_regb(dev, MAC_ADDR_6, MACbyte6);

  INFO("MAC address is set to %x:%x:%x:%x:%x:%x\n", MACbyte1, MACbyte2,
                MACbyte3, MACbyte4, MACbyte5, MACbyte6);
  return 0;
}


static int virtio_net_init(struct virtio_pci_dev *dev)
{
  uint32_t val;
  printk("%x\n", dev);
  DEBUG("Net init of %s\n",dev->name);
  val = read_regl(dev,DEVICE_FEATURES);
  DEBUG("device features: 0x%0x\n",val);
  
  write_regl(dev, GUEST_FEATURES, val); 
  val = read_regl(dev,GUEST_FEATURES);
  DEBUG("guest features: 0x%0x\n",val);
  
  /* give a virtual mac address to the device if not assigned */
  
  if(!(val & 0b100000)){
    virtio_net_set_mac_address(dev);
    //exit(-1);
  }
  write_regb(dev, DEVICE_STATUS, 0x7); 
  val = read_regb(dev, DEVICE_STATUS);
  DEBUG("device status: 0x%0x\n", val);

  p_dev = dev;
  register_int_handler(0xe4, rx_handler, NULL);
  
  struct virtio_packet *tx = malloc(sizeof(struct virtio_packet));
  memset(tx, 0, sizeof(struct virtio_packet));
  memset(&(tx->data.src), 0x01, 6);
  memset(&(tx->data.dst), 0xff, 6);
  memset(&(tx->data.type), 0x01, 2);
  
  struct virtio_packet_data *data = malloc(sizeof(struct virtio_packet_data));
  memset(data, 0, sizeof(struct virtio_packet_data));
  //memset(data->src, 0x01, 6);
  data->src[0]=0x52;
  data->src[1]=0x54;
  data->src[2]=0x00;
  data->src[3]=0x12;
  data->src[4]=0x34;
  data->src[5]=0x56;
  memset(data->dst, 0xff, 6);
  
  //nk_dump_mem(data, sizeof(struct virtio_packet_data));
  struct virtio_net_state* state = malloc(sizeof(struct virtio_net_state));
  state->virtio_dev = (struct virtio_pci_dev*)dev;
  //packet_tx(state, (uint64_t)tx, (uint32_t)sizeof(struct virtio_packet) ,0);
  //packet_tx(state, (uint64_t)data, (uint32_t)sizeof(struct virtio_packet) ,0);
  

  //packet_tx(dev, (uint64_t)data);
  
  uint32_t mac1 = read_regb(dev, MAC_ADDR_1);
  uint32_t mac2 = read_regb(dev, MAC_ADDR_2);
  uint32_t mac3 = read_regb(dev, MAC_ADDR_3);
  uint32_t mac4 = read_regb(dev, MAC_ADDR_4);
  uint32_t mac5 = read_regb(dev, MAC_ADDR_5);
  uint32_t mac6 = read_regb(dev, MAC_ADDR_6);
  DEBUG("MAC address: %x:%x:%x:%x:%x:%x\n", mac1, mac2, mac3, mac4, mac5, mac6);

  uint32_t ring = RECEIVE_QUEUE;
  struct virtio_packet_hdr *hdr = malloc(sizeof(struct virtio_packet_hdr));
  memset(hdr, 0, sizeof(struct virtio_packet_hdr));
  /*  
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier	

  virtio_enque_request(dev, ring, (uint64_t)hdr, (uint32_t)(sizeof(struct virtio_packet_hdr)),VIRTQ_DESC_F_NEXT);
  */
  uint32_t j;
  uint32_t num_buf = 65535 / sizeof(struct virtio_packet) + 1;
  DEBUG("num_buf: %d\n", num_buf);
  DEBUG("packet data size: %d\n", sizeof(struct virtio_packet));
  for(j = 0; j < num_buf; j++){
  	struct virtio_packet *data = malloc(sizeof(struct virtio_packet));
 	memset(data, 0, sizeof(struct virtio_packet));
       	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
 	dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  	
    	virtio_enque_request(dev, ring, (uint64_t)data,(uint32_t)(sizeof(struct virtio_packet)), VIRTQ_DESC_F_WRITE);
  }
  uint32_t used_idx = dev->vring[ring].vq.used->idx;
  //write_regw(dev, QUEUE_NOTIFY, RECEIVE_QUEUE);
  DEBUG("used idex: %d\n", used_idx);
  DEBUG("used ring: %x\n", dev->vring[ring].vq.used->ring[used_idx]);
 
  //while(1){
//	packet_rx(dev);

 // }
  return 0;
}


static int bringup_device(struct virtio_pci_dev *dev)
{
  DEBUG("Bringing up %s\n",dev->name);
  switch (dev->type) {
  case VIRTIO_PCI_BLOCK:
    if (virtio_ring_init(dev)) { 
      ERROR("Failed to bring up device %s\n", dev->name);
      return -1;
    }
    return virtio_block_init(dev);
    break;
  case VIRTIO_PCI_NET:
    write_regb(dev,DEVICE_STATUS,0x0); // driver resets device
    write_regb(dev,DEVICE_STATUS,0x1); // driver acknowledges device
    write_regb(dev,DEVICE_STATUS,0x3); // driver can drive device

    if (virtio_ring_init(dev)) { 
      ERROR("Failed to bring up device %s\n", dev->name);
      return -1;
    }
    
    return virtio_net_init(dev);
    break;
  case VIRTIO_PCI_OTHER:
  default:
    INFO("Skipping unsupported device type\n");
    return 0;
  }
    
}

static int bringup_devices()
{
  struct list_head *curdev, tx_node;

  DEBUG("Bringing up virtio devices\n");
  int num = 0;
  list_for_each(curdev,&(dev_list)) { 
    struct virtio_pci_dev *dev = list_entry(curdev,struct virtio_pci_dev,virtio_node );
    if(!strcmp(dev->name, "virtio-0-net")){
    int ret = bringup_device(dev);
    if (ret) { 
      ERROR("Bringup of virtio devices failed\n");
      return -1;
    }
    return 0;
    }
    
  }
  return 0;
}

int virtio_pci_init(struct naut_info * naut)
{

  INFO("init\n");

  if (discover_devices(naut->sys.pci)) { 
    ERROR("Discovery failed\n");
    return -1;
  }

  apic = naut->sys.cpus[naut->sys.bsp_id]->apic;
  bringup_devices();
  
  return 0;
}

int virtio_pci_deinit()
{
  INFO("deinited\n");
  return 0;
}



