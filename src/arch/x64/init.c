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
#define __NAUTILUS_MAIN__

#include <nautilus/nautilus.h>
#include <nautilus/cga.h>
#include <nautilus/paging.h>
#include <nautilus/idt.h>
#include <nautilus/spinlock.h>
#include <nautilus/mb_utils.h>
#include <nautilus/cpu.h>
#include <nautilus/msr.h>
#include <nautilus/cpuid.h>
#include <nautilus/smp.h>
#include <nautilus/irq.h>
#include <nautilus/thread.h>
#include <nautilus/idle.h>
#include <nautilus/percpu.h>
#include <nautilus/errno.h>
#include <nautilus/fpu.h>
#include <nautilus/random.h>
#include <nautilus/acpi.h>
#include <nautilus/atomic.h>
#include <nautilus/mm.h>
#include <nautilus/libccompat.h>
#include <nautilus/barrier.h>

#include <dev/apic.h>
#include <dev/pci.h>
#include <dev/virtio_pci.h>
#include <dev/hpet.h>
#include <dev/ioapic.h>
#include <dev/timer.h>
#include <dev/i8254.h>
#include <dev/kbd.h>
#include <dev/serial.h>

#ifdef NAUT_CONFIG_NDPC_RT
#include "ndpc_preempt_threads.h"
#endif


extern spinlock_t printk_lock;

#ifdef NAUT_CONFIG_NDPC_RT
void ndpc_rt_test()
{
    printk("Testing NDPC Library and Executable\n");

    

#if 1
    // this function will be linked to nautilus
    test_ndpc();
#else
    thread_id_t tid;
    
    ndpc_init_preempt_threads();
    
    tid = ndpc_fork_preempt_thread();
    
    if (!tid) { 
        printk("Error in initial fork\n");
        return;
    } 


    if (tid!=ndpc_my_preempt_thread()) { 
        printk("Parent!\n");
        ndpc_join_preempt_thread(tid);
        printk("Joinend with foo\n");
    } else {
        printk("Child!\n");
        return;
    }

    ndpc_deinit_preempt_threads();

#endif 


}
#endif /* !NAUT_CONFIG_NDPC_RT */


static int 
sysinfo_init (struct sys_info * sys)
{
    sys->core_barrier = (nk_barrier_t*)malloc(sizeof(nk_barrier_t));
    if (!sys->core_barrier) {
        ERROR_PRINT("Could not allocate core barrier\n");
        return -1;
    }
    memset(sys->core_barrier, 0, sizeof(nk_barrier_t));

    if (nk_barrier_init(sys->core_barrier, sys->num_cpus) != 0) {
        ERROR_PRINT("Could not create core barrier\n");
        goto out_err;
    }

    return 0;

out_err:
    free(sys->core_barrier);
    return -EINVAL;
}


static void
runtime_init (void)
{

#ifdef NAUT_CONFIG_LEGION_RT
#ifdef NAUT_CONFIG_PROFILE
        nk_instrument_start();
        nk_instrument_calibrate(INSTR_CAL_LOOPS);
#endif

        extern void run_legion_tests(void);
        run_legion_tests();

#ifdef NAUT_CONFIG_PROFILE
        nk_instrument_end();
        nk_instrument_query();
#endif
#endif /* !NAUT_CONFIG_LEGION_RT */


#ifdef NAUT_CONFIG_NDPC_RT
        ndpc_rt_test();
#endif

#ifdef NAUT_CONFIG_NESL_RT
        nesl_nautilus_main();
#endif
}


extern struct naut_info * smp_ap_stack_switch(uint64_t, uint64_t, struct naut_info*);

void
init (unsigned long mbd,
      unsigned long magic)
{
    struct naut_info * naut = &nautilus_info;

    memset(naut, 0, sizeof(struct naut_info));

    term_init();

    spinlock_init(&printk_lock);

    setup_idt();

    nk_int_init(&(naut->sys));

    serial_init();

    show_splash();

    detect_cpu();

    /* setup the temporary boot-time allocator */
    mm_boot_init(mbd);

    naut->sys.mb_info = multiboot_parse(mbd, magic);
    if (!naut->sys.mb_info) {
        ERROR_PRINT("Problem parsing multiboot header\n");
    }

    /* enumerate CPUs and initialize them */
    smp_early_init(naut);

    /* this will populate NUMA-related structures and 
     * also initialize the relevant ACPI tables if they exist */
    nk_numa_init();

    /* this will finish up the identity map */
    nk_paging_init(&(naut->sys.mem), mbd);

    /* setup the main kernel memory allocator */
    nk_kmem_init();

    // setup per-core area for BSP
    msr_write(MSR_GS_BASE, (uint64_t)naut->sys.cpus[0]);

    /* now we switch to the real kernel memory allocator, pages
     * allocated in the boot mem allocator are kept reserved */
    mm_boot_kmem_init();

    disable_8259pic();

    i8254_init(naut);

    /* from this point on, we can use percpu macros (even if the APs aren't up) */

    sysinfo_init(&(naut->sys));

    ioapic_init(&(naut->sys));

    nk_timer_init(naut);

    apic_init(naut->sys.cpus[0]);

    fpu_init(naut);

    nk_rand_init(naut->sys.cpus[0]);

    kbd_init(naut);

    pci_init(naut);

    virtio_pci_init(naut);
 
    nk_sched_init();

    /* we now switch away from the boot-time stack in low memory */
    naut = smp_ap_stack_switch(get_cur_thread()->rsp, get_cur_thread()->rsp, naut);

    mm_boot_kmem_cleanup();

    smp_setup_xcall_bsp(naut->sys.cpus[0]);

    nk_cpu_topo_discover(naut->sys.cpus[0]); 
#ifdef NAUT_CONFIG_HPET
    nk_hpet_init();
#endif

#ifdef NAUT_CONFIG_PROFILE
    nk_instrument_init();
#endif

    smp_bringup_aps(naut);

    extern void nk_mwait_init(void);
    nk_mwait_init();

#ifdef NAUT_CONFIG_CXX_SUPPORT
    extern void nk_cxx_init(void);
    // Assuming we don't encounter C++ before here
    nk_cxx_init();
#endif 

    /* interrupts on */
    sti();

    runtime_init();

    printk("Nautilus boot thread yielding (indefinitely)\n");
    /* we don't come back from this */
    idle(NULL, NULL);
}
