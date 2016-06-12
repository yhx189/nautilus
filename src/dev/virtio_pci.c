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
#define PRINT(fmt, args...) printk("" fmt, ##args)

// list of virtio devices we are managing
static struct list_head dev_list;
static struct virtio_pci_dev * p_dev;
static struct list_head tx_queue;
static struct list_head rx_queue;

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
    // note it's a 32 bit xegister, but the address is a page address
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
  vq->avail->ring[vq->avail->idx % vq->num] = i;
  
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

static int irq_handler(excp_entry_t * entry, excp_vec_t vec)
{
  DEBUG("IRQ HANDLER  INTERRUPT FIRED;  \n");
  uint8_t val = read_regb(p_dev, ISR_STATUS); 

  DEBUG("ISR status : %d\n", val);

  if(!val){
     return NULL;
  }

  uint32_t ring = RECEIVE_QUEUE;
  struct virtio_pci_vring *vring = &p_dev->vring[ring];
  volatile struct virtq *vq = &vring->vq;
  vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;

  // disable interrupts
  // dumping out more
  DEBUG("rx: last seen used %d, used idx %d \n", vring->last_seen_used, vq->used->idx);
  struct virtq_used_elem *e;
  uint32_t len;
  while(vring->last_seen_used < vq->used->idx){
	DEBUG("RECEIVE INTERRUPT FIRED\n");
 	     
        __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  
	e = &(vq->used->ring[vring->last_seen_used % vq->num]);
 	len = vq->desc[e->id].len;
	uint32_t i = 0;
        uint8_t *p = (uint8_t *)vq->desc[e->id].addr;
        PRINT("header: ");   	
        for(i = 0; i < sizeof(struct virtio_packet_hdr); i++){
  	  PRINT("%02x", *p);
	  p++;
  	}
        PRINT("\n");
        PRINT("packet: ");
  	for(i = sizeof(struct virtio_packet_hdr); i < vq->desc[e->id].len; i++){
  	  PRINT("%02x", *p);
	  p++;
  	}
	PRINT("\n");
        

        // store the mac - ip address pair in cache
	PRINT("source ip:\n");
	p = (uint8_t*) vq->desc[e->id].addr;
	uint32_t offset = sizeof(struct virtio_packet_hdr) + 0x1c;
	p+= offset;
	uint8_t target_ip[4];
	for(i = 0; i < 4; i++){
          target_ip[i] = *p;
	  PRINT("%02x", *p);
	  p++;
	}
	PRINT("\n");
        PRINT("destination mac:\n");
	uint8_t target_mac[6];
	p = (uint8_t*) vq->desc[e->id].addr;
        offset = sizeof(struct virtio_packet_hdr) + 0x06;
	p += offset;	

	struct virtio_packet *resp = malloc(sizeof(struct virtio_packet));
        memset(resp, 0, sizeof(struct virtio_packet));   

	for(i = 0; i < 6; i++){
          target_mac[i] = *p;
	  PRINT("%02x", *p);
          resp->data.dst[i] = *p;
	  p++;
	}
	PRINT("\n");
        
        PRINT("source mac:\n");
	p = (uint8_t*) vq->desc[e->id].addr;
        offset = sizeof(struct virtio_packet_hdr);
	p += offset;	

 	uint8_t mac[6]; 
        mac[0] = read_regb(p_dev, MAC_ADDR_1);
 	mac[1] = read_regb(p_dev, MAC_ADDR_2);
  	mac[2] = read_regb(p_dev, MAC_ADDR_3);
  	mac[3] = read_regb(p_dev, MAC_ADDR_4);
  	mac[4] = read_regb(p_dev, MAC_ADDR_5);
  	mac[5] = read_regb(p_dev, MAC_ADDR_6);
	for(i = 0; i < 6; i++){
        
          resp->data.src[i] = mac[i];
	  PRINT("%02x", mac[i]);
	  p++;
	}
	PRINT("\n");
	
        resp->data.type[0] = 0x08;
        resp->data.type[1] = 0x06;

	set_arp_response(resp->data.data, mac, target_mac, target_ip);
	set_crc(&resp->data);
        
        // send response
        struct virtio_net_state * state = malloc(sizeof(struct virtio_net_state));
        memset(state, 0, sizeof(*state));
        state->dev = p_dev;
	packet_tx((void *)state, (uint64_t) resp, sizeof(*resp), 0);


  	free_descriptor(vq,e->id);
  	vring->last_seen_used += 1;
        vq->avail->flags = 0;
  	DEBUG("used index: %d\n", vq->used->idx);
  	DEBUG("avail index: %d\n", vq->avail->idx);
  
  }
  if(ring == RECEIVE_QUEUE && vq->avail->idx == 0){
     // add some new descriptors
     uint32_t j = 0, num_buf = 30;
     for(j = 0; j < num_buf; j++){
  	struct virtio_packet *data = malloc(sizeof(struct virtio_packet));
 	memset(data, 0, sizeof(struct virtio_packet));
       	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
 	p_dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  	
    	virtio_enque_request(p_dev, ring, (uint64_t)data,(uint32_t)(sizeof(struct virtio_packet)), VIRTQ_DESC_F_WRITE);
  	}

  }
  vq->avail->flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;
 
  ring = TRANSMIT_QUEUE;
  vring = &p_dev->vring[ring];
  vq = &vring->vq;
  vq->avail->flags |= VIRTQ_AVAIL_F_NO_INTERRUPT;

  DEBUG("tx: last seen used %d, used idx %d \n", vring->last_seen_used, vq->used->idx);
  while(vring->last_seen_used < vq->used->idx ){
	e = &(vq->used->ring[vring->last_seen_used % vq->num]);
        len = vq->desc[e->id].len;
 
        DEBUG("TRANSMIT INTERRUPT FIRED\n");
 	uint32_t i = 0;
  	uint8_t *p = (uint8_t *)vq->desc[e->id].addr;
  	//uint64_t s[vq->desc[e->id].len];
        PRINT("header: ");   	
        for(i = 0; i < sizeof(struct virtio_packet_hdr); i++){
  	  PRINT("%02x", *p);
	  p++;
  	}
        PRINT("\n");
        PRINT("packet: ");
  	for(i = sizeof(struct virtio_packet_hdr); i < vq->desc[e->id].len; i++){
  	  PRINT("%02x", *p);
	  p++;
  	}
	PRINT("\n");
  
  	free_descriptor(vq, e->id);
  	vring->last_seen_used += 1;
        vq->avail->flags =0;
  	DEBUG("used index: %x\n", vq->used->idx);
  	DEBUG("avail index: %x\n", vq->avail->idx);
  }
  vq->avail->flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;

  goto out;

out:
  DEBUG("returning from interrupt\n");
  IRQ_HANDLER_END();
  return 0;
}

static uint32_t crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t crc32(uint32_t crc, const void *buf, size_t size)
{
	const uint8_t *p;

	p = buf;
	crc = crc ^ ~0U;

	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return crc ^ ~0U;
}
void set_crc(struct virtio_packet_data* packet)
{
  uint8_t data[1014];
  uint32_t i;
  for(i = 0; i < 6; i++)
    data[i] = packet->dst[i];
  for(i = 0; i < 6; i++)
    data[i+6] = packet->src[i];
  for(i = 0; i < 2; i++)
    data[i+12] = packet->type[i];
  for(i = 0; i < 1000; i++)
    data[i+14] = packet->data[i];
  uint32_t crc = crc32(0, (void*) data, 1000);
  DEBUG("CRC: %x\n", crc);
  packet->data[989] = (crc & 0xff000000) >> 24;
  packet->data[988] = (crc & 0xff0000) >> 16;
  packet->data[987] = (crc & 0xff00) >> 8;
  packet->data[986] = crc & 0xff;
}


void set_arp_response(uint8_t* data, uint8_t * mac, uint8_t* target_mac, uint8_t* target_ip)
{
   uint8_t i = 0;
   data[i++] = 0x00;
   data[i++] = 0x01;
   data[i++] = 0x08;
   data[i++] = 0x00;
   data[i++] = 0x06;
   data[i++] = 0x04;
   data[i++] = 0x00;
   data[i++] = 0x02;
   data[i++] = mac[0];
   data[i++] = mac[1];
   data[i++] = mac[2];
   data[i++] = mac[3];
   data[i++] = mac[4];
   data[i++] = mac[5];
   data[i++] = 0x0a;
   data[i++] = 0x0a;
   data[i++] = 0x0a;
   data[i++] = 0x0a;
   data[i++] = target_mac[0];
   data[i++] = target_mac[1];
   data[i++] = target_mac[2];
   data[i++] = target_mac[3];
   data[i++] = target_mac[4];
   data[i++] = target_mac[5];
   data[i++] = target_ip[0];
   data[i++] = target_ip[1];
   data[i++] = target_ip[2];
   data[i++] = target_ip[3];

}


int packet_tx_async(void *v_state, uint64_t packet, uint32_t packet_len, int wait)
{ 
  struct virtio_net_state* state = (struct virtio_net_state*) v_state;
  INIT_LIST_HEAD(&tx_queue);
  struct list_head tx;
  struct virtio_packet* ptr = (struct virtio_packet*)packet;
  list_add_tail(& (ptr->packet_node) , &tx_queue);
  struct list_head *curpacket;
   
  struct virtio_pci_dev * dev;
  dev = state->dev;
  uint32_t ring = TRANSMIT_QUEUE;
  struct virtio_pci_vring* vring = &dev->vring[ring];
  volatile struct virtq *vq = &dev->vring[ring].vq;


  int num = 0;  
  DEBUG("sending packet: ");
  list_for_each(curpacket, &tx_queue) { 
      struct virtio_packet* to_send = list_entry(curpacket, struct virtio_packet, packet_node);
      DEBUG("at %x\n", to_send);
      if(num == 0 && (vring->last_seen_used < (vq->used->idx - 10)) )
         packet_tx(state, (uint64_t)to_send, sizeof(struct virtio_packet), 0);
      
      num++;
  }
     
  return 0;
}


int packet_tx(void *v_state, uint64_t packet, uint32_t packet_len, int wait)
{
  DEBUG("transmitting packet %x of len %d\n", packet, packet_len);
  struct virtio_net_state * state = (struct virtio_net_state *)v_state;
  struct virtio_pci_dev * dev;
  dev  = state->dev;
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
  virtio_enque_request(dev, ring, addr, len, 0);
   __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  vq->avail->idx+=1; // it is ok that this wraps around
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
  
  DEBUG("flag : %d\n ", dev->vring[ring].vq.avail->flags);
  /* notify the device */
  write_regw(dev, QUEUE_NOTIFY, TRANSMIT_QUEUE);
  DEBUG("transmit buffer notification sent %x\n", addr);
  uint32_t used_idx = dev->vring[ring].vq.used->idx;
  DEBUG("used->idx: %d\n", used_idx);	
 
   return 0;
}

int packet_rx_async(void *v_state, uint64_t packet, uint32_t packet_len, int wait)
{
  struct virtio_net_state* state = (struct virtio_net_state*) v_state;
  INIT_LIST_HEAD(&rx_queue);
  struct list_head rx;
   

  struct virtio_packet* ptr = (struct virtio_packet*)packet;
  list_add_tail(& (ptr->packet_node) , &rx_queue);
  struct list_head *curpacket;
  int num = 0;  
  DEBUG("receiving packet: ");
  list_for_each(curpacket, &rx_queue) { 
      struct virtio_packet* received = list_entry(curpacket, struct virtio_packet, packet_node);
      DEBUG("at %x\n", received);
      if(num == 0)
         packet_rx(state, (uint64_t)received, sizeof(struct virtio_packet), 0);
      
      num++;
  }
  
  return 0;
}


int packet_rx(void *v_state, uint64_t packet, uint32_t packet_len, int wait)
{
  struct virtio_net_state* state = (struct virtio_net_state*) v_state;
  struct virtio_pci_dev * dev;
  dev = state->dev;
  uint32_t ring = RECEIVE_QUEUE;
  uint32_t len = sizeof(struct virtio_packet);
  uint16_t flags = VIRTQ_DESC_F_NEXT; 
  uint32_t j = 0, num_buf = 30;
  for(j = 0; j < num_buf; j++){
  	struct virtio_packet *data = malloc(sizeof(struct virtio_packet));
 	memset(data, 0, sizeof(struct virtio_packet));
       	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
 	p_dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  	
    	virtio_enque_request(p_dev, ring, (uint64_t)data,(uint32_t)(sizeof(struct virtio_packet)), VIRTQ_DESC_F_WRITE);
  }
  __asm__ __volatile__ ("" : : : "memory"); // software memory barrier
  __sync_synchronize(); // hardware memory barrier
   DEBUG("flag : %d\n ", dev->vring[ring].vq.avail->flags);
  	
  write_regw(dev, QUEUE_NOTIFY, RECEIVE_QUEUE);
  uint32_t used_idx = dev->vring[ring].vq.used->idx;
  DEBUG("RX: used->idx: %d\n", used_idx);
   
  uint32_t avail_idx = dev->vring[ring].vq.avail->idx;
  DEBUG("RX: avail->idx: %d\n", avail_idx);
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
#define VIRTIO_NET_F_MAC 0x20
#define VIRTIO_F_RING_EVENT_IDX 0x00000000
#define MY_GUEST_FEATURES (VIRTIO_NET_F_MAC | VIRTIO_F_RING_EVENT_IDX)
static int virtio_net_init(struct virtio_pci_dev *dev)
{
  uint32_t val;
  PRINT("%x\n", dev);
  DEBUG("Net init of %s\n",dev->name);
  // FEATURE_SELECT 


  val = read_regl(dev,DEVICE_FEATURES);
  DEBUG("device features: 0x%0x\n",val);
  DEBUG("guest features: 0x%0x\n", MY_GUEST_FEATURES); 
  val &= MY_GUEST_FEATURES;
  DEBUG("negotiated features: 0x%0x\n", val);
  
  write_regl(dev, GUEST_FEATURES, val); 
  
  /* give a virtual mac address to the device if not assigned */
  
  if(!(val & 0b100000)){
    virtio_net_set_mac_address(dev);
    //exit(-1);
  }
  write_regb(dev, DEVICE_STATUS, 0x7); 
  val = read_regb(dev, DEVICE_STATUS);
  DEBUG("device status: 0x%0x\n", val);

  p_dev = dev;
  register_int_handler(0xe4, irq_handler, NULL);
  nk_unmask_irq(0xb);
   
  struct virtio_net_state* state = malloc(sizeof(struct virtio_net_state));
  state->dev = (struct virtio_pci_dev*)dev;
  
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
  uint32_t j;
  uint32_t num_buf = 65535 / sizeof(struct virtio_packet) + 1;
  DEBUG("num_buf: %d\n", num_buf);
  DEBUG("packet data size: %d\n", sizeof(struct virtio_packet));
  for(j = 0; j < num_buf; j++){
  	struct virtio_packet *data = malloc(sizeof(struct virtio_packet));
 	memset(data, 0, sizeof(struct virtio_packet));
       	virtio_enque_request(dev, ring, (uint64_t)data,(uint32_t)(sizeof(struct virtio_packet)), VIRTQ_DESC_F_WRITE);
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
 	dev->vring[ring].vq.avail->idx += 1; // it is ok that this wraps around
 	__asm__ __volatile__ ("" : : : "memory"); // software memory barrier
 	__sync_synchronize(); // hardware memory barrier
  }
  write_regw(dev, QUEUE_NOTIFY, RECEIVE_QUEUE);
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



