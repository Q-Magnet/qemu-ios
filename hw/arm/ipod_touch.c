#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/platform-bus.h"
#include "hw/block/flash.h"
#include "hw/qdev-clock.h"
#include "hw/arm/ipod_touch.h"
#include "hw/arm/ipod_touch_spi.h"
#include "hw/arm/exynos4210.h"
#include "hw/dma/pl080.h"
#include <openssl/aes.h>

#define IPOD_TOUCH_PHYS_BASE (0xc0000000)
#define IBOOT_BASE 0x18000000
#define LLB_BASE 0x22000000
#define SRAM1_MEM_BASE 0x22020000
#define CLOCK0_MEM_BASE 0x38100000
#define CLOCK1_MEM_BASE 0x3C500000
#define SYSIC_MEM_BASE 0x39A00000
#define VIC0_MEM_BASE 0x38E00000
#define VIC1_MEM_BASE 0x38E01000
#define EDGEIC_MEM_BASE 0x38E02000
#define IIS0_MEM_BASE 0x3D400000
#define IIS1_MEM_BASE 0x3CD00000
#define IIS2_MEM_BASE 0x3CA00000
#define NOR_MEM_BASE 0x24000000
#define TIMER1_MEM_BASE 0x3E200000
#define USBOTG_MEM_BASE 0x38400000
#define USBPHYS_MEM_BASE 0x3C400000
#define GPIO_MEM_BASE 0x3E400000
#define I2C0_MEM_BASE 0x3C600000
#define I2C1_MEM_BASE 0x3C900000
#define SPI0_MEM_BASE 0x3C300000
#define SPI1_MEM_BASE 0x3CE00000
#define SPI2_MEM_BASE 0x3D200000
#define WATCHDOG_MEM_BASE 0x3E300000
#define DISPLAY_MEM_BASE 0x38900000
#define CHIPID_MEM_BASE 0x3e500000
#define RAM_MEM_BASE 0x8000000
#define SHA1_MEM_BASE 0x38000000
#define AES_MEM_BASE 0x38C00000
#define VROM_MEM_BASE 0x20000000
#define NAND_MEM_BASE 0x38A00000
#define NAND_ECC_MEM_BASE 0x38F00000
#define DMAC0_MEM_BASE 0x38200000
#define DMAC1_MEM_BASE 0x39900000
#define UART0_MEM_BASE 0x3CC00000
#define UART1_MEM_BASE 0x3CC04000
#define UART2_MEM_BASE 0x3CC08000
#define UART3_MEM_BASE 0x3CC0C000
#define UART4_MEM_BASE 0x3CC10000
#define ADM_MEM_BASE 0x38800000
#define MBX_MEM_BASE 0x3B000000
#define ENGINE_8900_MEM_BASE 0x3F000000
#define KERNELCACHE_BASE 0x9000000
#define SDIO_MEM_BASE 0x38d00000
#define FRAMEBUFFER_MEM_BASE 0xfe00000
#define TVOUT1_MEM_BASE 0x39100000
#define TVOUT2_MEM_BASE 0x39200000
#define TVOUT3_MEM_BASE 0x39300000

#define TVOUT_WORKAROUND_MEM_BASE 0x8a25960  // workaround for TV Out, to make sure that it can be correctly deallocated without reverse engineering the entire TVOut protocol

static uint64_t tvout_workaround_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void tvout_workaround_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{

}

static const MemoryRegionOps tvout_workaround_ops = {
    .read = tvout_workaround_read,
    .write = tvout_workaround_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void allocate_ram(MemoryRegion *top, const char *name, uint32_t addr, uint32_t size)
{
        MemoryRegion *sec = g_new(MemoryRegion, 1);
        memory_region_init_ram(sec, NULL, name, size, &error_fatal);
        memory_region_add_subregion(top, addr, sec);
}

static uint32_t align_64k_high(uint32_t addr)
{
    return (addr + 0xffffull) & ~0xffffull;
}

static void ipod_touch_cpu_setup(MachineState *machine, MemoryRegion **sysmem, ARMCPU **cpu, AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    *sysmem = get_system_memory();

    object_property_set_link(cpuobj, "memory", OBJECT(*sysmem), &error_abort);

    object_property_set_bool(cpuobj, "has_el3", false, NULL);

    object_property_set_bool(cpuobj, "has_el2", false, NULL);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    object_unref(cpuobj);
}

static void ipod_touch_cpu_reset(void *opaque)
{
    IPodTouchMachineState *nms = IPOD_TOUCH_MACHINE((MachineState *)opaque);
    ARMCPU *cpu = nms->cpu;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);

    //env->regs[0] = nms->kbootargs_pa;
    //cpu_set_pc(CPU(cpu), 0xc00607ec);
    cpu_set_pc(CPU(cpu), IBOOT_BASE);
    //env->regs[0] = 0x9000000;
    //cpu_set_pc(CPU(cpu), LLB_BASE + 0x100);
    //cpu_set_pc(CPU(cpu), VROM_MEM_BASE);
}

/*
CLOCK
*/

static void s5l8900_clock1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    s5l8900_clk1_s *s = (struct s5l8900_clk1_s *) opaque;

    switch (addr) {
        case CLOCK1_PLLLOCK:
            s->plllock = val;
            break;
      default:
            break;
    }
}

static uint64_t s5l8900_clock1_read(void *opaque, hwaddr addr, unsigned size)
{
    s5l8900_clk1_s *s = (struct s5l8900_clk1_s *) opaque;

    switch (addr) {
        case CLOCK1_CONFIG0:
            return s->config0;
        case CLOCK1_CONFIG1:
            return s->config1;
        case CLOCK1_CONFIG2:
            return s->config2;
        case CLOCK1_PLL0CON:
            return s->pll0con;
        case CLOCK1_PLL1CON:
            return s->pll1con;
        case CLOCK1_PLL2CON:
            return s->pll2con;
        case CLOCK1_PLL3CON:
            return s->pll3con;
        case CLOCK1_PLLLOCK:
            return s->plllock;
        case CLOCK1_PLLMODE:
            return 0x000a003a; // extracted from iPod Touch
      default:
            break;
    }
    return 0;
}

static const MemoryRegionOps clock1_ops = {
    .read = s5l8900_clock1_read,
    .write = s5l8900_clock1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
USB PHYS
*/
static uint64_t s5l8900_usb_phys_read(void *opaque, hwaddr addr, unsigned size)
{
    s5l8900_usb_phys_s *s = opaque;

    switch(addr)
    {
    case 0x0: // OPHYPWR
        return s->usb_ophypwr;

    case 0x4: // OPHYCLK
        return s->usb_ophyclk;

    case 0x8: // ORSTCON
        return s->usb_orstcon;

    case 0x20: // OPHYTUNE
        return s->usb_ophytune;

    default:
        fprintf(stderr, "%s: read invalid location 0x%08x\n", __func__, addr);
        return 0;
    }

    return 0;
}

static void s5l8900_usb_phys_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    s5l8900_usb_phys_s *s = opaque;

    switch(addr)
    {
    case 0x0: // OPHYPWR
        s->usb_ophypwr = val;
        return;

    case 0x4: // OPHYCLK
        s->usb_ophyclk = val;
        return;

    case 0x8: // ORSTCON
        s->usb_orstcon = val;
        return;

    case 0x20: // OPHYTUNE
        s->usb_ophytune = val;
        return;

    default:
        //hw_error("%s: write invalid location 0x%08x.\n", __func__, offset);
        fprintf(stderr, "%s: write invalid location 0x%08x\n", __func__, addr);
    }
}

static const MemoryRegionOps usb_phys_ops = {
    .read = s5l8900_usb_phys_read,
    .write = s5l8900_usb_phys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
MBX
*/
static uint64_t s5l8900_mbx_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);
    switch(addr)
    {
        case 0x12c:
            return 0x100;
        case 0xf00:
            return (1 << 0x18) | 0x10000; // seems to be some kind of identifier
        case 0x1020:
            return 0x10000;
        default:
            break;
    }
    return 0;
}

static void s5l8900_mbx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    //fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
    // do nothing
}

static const MemoryRegionOps mbx_ops = {
    .read = s5l8900_mbx_read,
    .write = s5l8900_mbx_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_init_clock(MachineState *machine, MemoryRegion *sysmem)
{
    IPodTouchMachineState *nms = IPOD_TOUCH_MACHINE(machine);
    nms->clock1 = (s5l8900_clk1_s *) g_malloc0(sizeof(struct s5l8900_clk1_s));
    nms->clock1->plllock = 1 | 2 | 4 | 8;

    uint64_t config0 = 0x0;
    config0 |= (1 << 12); // set the clock PPL to index 1
    config0 |= (1 << 24); // indicate that we have a memory divisor
    config0 |= (2 << 16); // set the memory divisor to two
    nms->clock1->config0 = config0;

    uint64_t config1 = 0x0;
    config1 |= (1 << 12); // set the bus PPL to index 1
    config1 |= (1 << 24); // indicate that we have a bus divisor
    config1 |= (3 << 16); // set the bus divisor to three
    config1 |= (1 << 8);  // indicate that unknown has a divisor
    config1 |= 3;         // set the unknown divisor 1 to 3
    config1 |= (0 << 4);  // set the unknown divisor 2 to 0
    config1 |= (1 << 20); // set the peripheral factor to 1

    config1 |= (1 << 14); // unknown
    config1 |= (1 << 28); // set some PPL index to 1
    config1 |= (1 << 30); // unknown
    nms->clock1->config1 = config1;

    uint64_t config2 = 0x0;
    config2 |= (3 << 28); // set the peripheral PPL to index 3
    config2 |= (1 << 24); // indicate that the display has a divisor
    config2 |= (1 << 16); // set the display divisor to 1
    nms->clock1->config2 = config2;

    // set the PLL configurations (MDIV, PDIV, SDIV)
    nms->clock1->pll0con = (80 << 8) | (8 << 24) | 0;
    nms->clock1->pll1con = (103 << 8) | (6 << 24) | 0;
    nms->clock1->pll2con = (156 << 8) | (53 << 24) | 2;
    nms->clock1->pll3con = (72 << 8) | (8 << 24) | 1;

    // TODO same as clock1 for now
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &clock1_ops, nms->clock1, "clock0", 0x1000);
    memory_region_add_subregion(sysmem, CLOCK0_MEM_BASE, iomem);

    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &clock1_ops, nms->clock1, "clock1", 0x1000);
    memory_region_add_subregion(sysmem, CLOCK1_MEM_BASE, iomem);
}

static void ipod_touch_memory_setup(MachineState *machine, MemoryRegion *sysmem, AddressSpace *nsas)
{
    IPodTouchMachineState *nms = IPOD_TOUCH_MACHINE(machine);
    DriveInfo *dinfo;

    allocate_ram(sysmem, "sram1", SRAM1_MEM_BASE, 0x10000);

    // allocate UART ram
    allocate_ram(sysmem, "ram", RAM_MEM_BASE, 0x8000000);

    // load the bootrom (vrom)
    uint8_t *file_data = NULL;
    unsigned long fsize;
    if (g_file_get_contents("/Users/martijndevos/Documents/ipod_touch_emulation/bootrom_s5l8900", (char **)&file_data, &fsize, NULL)) {
        allocate_ram(sysmem, "vrom", VROM_MEM_BASE, 0x10000);
        address_space_rw(nsas, VROM_MEM_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);
    }

    // patch the address table to point to our own routines
    uint32_t *data = malloc(4);
    data[0] = LLB_BASE + 0x80;
    address_space_rw(nsas, 0x2000008c, MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, 4, 1);
    data[0] = LLB_BASE + 0x100;
    address_space_rw(nsas, 0x20000090, MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, 4, 1);

    // load iBoot
    file_data = NULL;
    if (g_file_get_contents("/Users/martijndevos/Documents/ipod_touch_emulation/iboot.bin", (char **)&file_data, &fsize, NULL)) {
        allocate_ram(sysmem, "iboot", IBOOT_BASE, 0x400000);
        address_space_rw(nsas, IBOOT_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);
     }

    // // load LLB
    // file_data = NULL;
    // if (g_file_get_contents("/Users/martijndevos/Documents/ipod_touch_emulation/LLB.n45ap.RELEASE", (char **)&file_data, &fsize, NULL)) {
    //     allocate_ram(sysmem, "llb", LLB_BASE, align_64k_high(fsize));
    //     address_space_rw(nsas, LLB_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);
    //  }

    allocate_ram(sysmem, "edgeic", EDGEIC_MEM_BASE, 0x1000);
    allocate_ram(sysmem, "gpio", GPIO_MEM_BASE, align_64k_high(0x1));
    allocate_ram(sysmem, "watchdog", WATCHDOG_MEM_BASE, align_64k_high(0x1));

    allocate_ram(sysmem, "iis0", IIS0_MEM_BASE, align_64k_high(0x1));
    allocate_ram(sysmem, "iis1", IIS1_MEM_BASE, align_64k_high(0x1));
    allocate_ram(sysmem, "iis2", IIS2_MEM_BASE, align_64k_high(0x1));

    allocate_ram(sysmem, "sdio", SDIO_MEM_BASE, 4096);

    allocate_ram(sysmem, "framebuffer", FRAMEBUFFER_MEM_BASE, align_64k_high(4 * 320 * 480));

    // setup 1MB NOR
    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo) {
        printf("A NOR image must be given with the -pflash parameter\n");
        abort();
    }

    if(!pflash_cfi02_register(NOR_MEM_BASE, "nor", 1024 * 1024, dinfo ? blk_by_legacy_dinfo(dinfo) : NULL, 4096, 1, 2, 0x00bf, 0x273f, 0x0, 0x0, 0x555, 0x2aa, 0)) {
        printf("Error registering NOR flash!\n");
        abort();
    }
}

static void ipod_touch_instance_init(Object *obj)
{
	
}

static inline qemu_irq s5l8900_get_irq(IPodTouchMachineState *s, int n)
{
    return s->irq[n / S5L8900_VIC_SIZE][n % S5L8900_VIC_SIZE];
}

static uint32_t s5l8900_usb_hwcfg[] = {
    0,
    0x7a8f60d0,
    0x082000e8,
    0x01f08024
};

static void ipod_touch_machine_init(MachineState *machine)
{
	IPodTouchMachineState *nms = IPOD_TOUCH_MACHINE(machine);
	MemoryRegion *sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;

    ipod_touch_cpu_setup(machine, &sysmem, &cpu, &nsas);

    // setup clock
    nms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(nms->sysclk, 12000000ULL);

    nms->cpu = cpu;

    // setup VICs
    nms->irq = g_malloc0(sizeof(qemu_irq *) * 2);
    DeviceState *dev = pl192_manual_init("vic0", qdev_get_gpio_in(DEVICE(nms->cpu), ARM_CPU_IRQ), qdev_get_gpio_in(DEVICE(nms->cpu), ARM_CPU_FIQ), NULL);
    PL192State *s = PL192(dev);
    nms->vic0 = s;
    memory_region_add_subregion(sysmem, VIC0_MEM_BASE, &nms->vic0->iomem);
    nms->irq[0] = g_malloc0(sizeof(qemu_irq) * 32);
    for (int i = 0; i < 32; i++) { nms->irq[0][i] = qdev_get_gpio_in(dev, i); }

    dev = pl192_manual_init("vic1", NULL);
    s = PL192(dev);
    nms->vic1 = s;
    memory_region_add_subregion(sysmem, VIC1_MEM_BASE, &nms->vic1->iomem);
    nms->irq[1] = g_malloc0(sizeof(qemu_irq) * 32);
    for (int i = 0; i < 32; i++) { nms->irq[1][i] = qdev_get_gpio_in(dev, i); }

    // // chain VICs together
    nms->vic1->daisy = nms->vic0;

    // init clock
    ipod_touch_init_clock(machine, sysmem);

    // init the timer
    dev = qdev_new("ipodtouch.timer");
    IPodTouchTimerState *timer_state = IPOD_TOUCH_TIMER(dev);
    nms->timer1 = timer_state;
    memory_region_add_subregion(sysmem, TIMER1_MEM_BASE, &timer_state->iomem);
    SysBusDevice *busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_TIMER1_IRQ));
    timer_state->sysclk = nms->sysclk;

    // init sysic
    dev = qdev_new("ipodtouch.sysic");
    IPodTouchSYSICState *sysic_state = IPOD_TOUCH_SYSIC(dev);
    nms->sysic = (IPodTouchSYSICState *) g_malloc0(sizeof(struct IPodTouchSYSICState));
    memory_region_add_subregion(sysmem, SYSIC_MEM_BASE, &sysic_state->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    for(int grp = 0; grp < GPIO_NUMINTGROUPS; grp++) {
        sysbus_connect_irq(busdev, grp, s5l8900_get_irq(nms, S5L8900_GPIO_IRQS[grp]));
    }

    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_GPIO_G4_IRQ));

    dev = exynos4210_uart_create(UART0_MEM_BASE, 256, 0, serial_hd(0), nms->irq[0][24]);
    if (!dev) {
        printf("Failed to create uart0 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART1_MEM_BASE, 256, 1, serial_hd(1), nms->irq[0][25]);
    if (!dev) {
        printf("Failed to create uart1 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART2_MEM_BASE, 256, 2, serial_hd(2), nms->irq[0][26]);
    if (!dev) {
        printf("Failed to create uart2 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART3_MEM_BASE, 256, 3, serial_hd(3), nms->irq[0][27]);
    if (!dev) {
        printf("Failed to create uart3 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART4_MEM_BASE, 256, 4, serial_hd(4), nms->irq[0][28]);
    if (!dev) {
        printf("Failed to create uart4 device!\n");
        abort();
    }

    // init spis
    set_spi_base(0);
    sysbus_create_simple("s5l8900spi", SPI0_MEM_BASE, s5l8900_get_irq(nms, S5L8900_SPI0_IRQ));

    set_spi_base(1);
    sysbus_create_simple("s5l8900spi", SPI1_MEM_BASE, s5l8900_get_irq(nms, S5L8900_SPI1_IRQ));

    set_spi_base(2);
    dev = sysbus_create_simple("s5l8900spi", SPI2_MEM_BASE, s5l8900_get_irq(nms, S5L8900_SPI2_IRQ));
    S5L8900SPIState *spi2_state = S5L8900SPI(dev);
    nms->spi2_state = spi2_state;

    ipod_touch_memory_setup(machine, sysmem, nsas);

    // init LCD
    dev = qdev_new("ipodtouch.lcd");
    IPodTouchLCDState *lcd_state = IPOD_TOUCH_LCD(dev);
    lcd_state->sysmem = sysmem;
    lcd_state->sysic = sysic_state;
    lcd_state->mt = spi2_state->mt;
    nms->lcd_state = lcd_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_LCD_IRQ));
    memory_region_add_subregion(sysmem, DISPLAY_MEM_BASE, &lcd_state->iomem);
    sysbus_realize(busdev, &error_fatal);

    // init AES engine
    dev = qdev_new("ipodtouch.aes");
    S5L8900AESState *aes_state = IPOD_TOUCH_AES(dev);
    nms->aes_state = aes_state;
    memory_region_add_subregion(sysmem, AES_MEM_BASE, &aes_state->iomem);

    // init SHA1 engine
    dev = qdev_new("ipodtouch.sha1");
    S5L8900SHA1State *sha1_state = IPOD_TOUCH_SHA1(dev);
    nms->sha1_state = sha1_state;
    memory_region_add_subregion(sysmem, SHA1_MEM_BASE, &sha1_state->iomem);

    // init 8900 engine
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(s), &engine_8900_ops, nsas, "8900engine", 0x100);
    memory_region_add_subregion(sysmem, ENGINE_8900_MEM_BASE, iomem);

    // init NAND flash
    dev = qdev_new("itnand");
    ITNandState *nand_state = ITNAND(dev);
    nms->nand_state = nand_state;
    memory_region_add_subregion(sysmem, NAND_MEM_BASE, &nand_state->iomem);

    // init NAND ECC module
    dev = qdev_new("itnand_ecc");
    ITNandECCState *nand_ecc_state = ITNANDECC(dev);
    nms->nand_ecc_state = nand_ecc_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_NAND_ECC_IRQ));
    memory_region_add_subregion(sysmem, NAND_ECC_MEM_BASE, &nand_ecc_state->iomem);

    // init USB OTG
    dev = ipod_touch_init_usb_otg(nms->irq[0][13], s5l8900_usb_hwcfg);
    synopsys_usb_state *usb_otg = S5L8900USBOTG(dev);
    nms->usb_otg = usb_otg;
    memory_region_add_subregion(sysmem, USBOTG_MEM_BASE, &nms->usb_otg->iomem);

    // init USB PHYS
    s5l8900_usb_phys_s *usb_state = malloc(sizeof(s5l8900_usb_phys_s));
    nms->usb_phys = usb_state;
    usb_state->usb_ophypwr = 0;
    usb_state->usb_ophyclk = 0;
    usb_state->usb_orstcon = 0;
    usb_state->usb_ophytune = 0;

    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(s), &usb_phys_ops, usb_state, "usbphys", 0x40);
    memory_region_add_subregion(sysmem, USBPHYS_MEM_BASE, iomem);

    // init 8900 OPS
    allocate_ram(sysmem, "8900ops", LLB_BASE, 0x1000);

    // patch the instructions related to 8900 decryption
    uint32_t *data = malloc(8);
    data[0] = 0xe3b00001; // MOVS R0, #1
    data[1] = 0xe12fff1e; // BX LR
    address_space_rw(nsas, LLB_BASE + 0x80, MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, 8, 1);

    // load the decryption logic in memory
    unsigned long fsize;
    uint8_t *file_data = NULL;
    if (g_file_get_contents("/Users/martijndevos/Documents/ipod_touch_emulation/asm/8900.bin", (char **)&file_data, &fsize, NULL)) {
        address_space_rw(nsas, LLB_BASE + 0x100, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);
    }

    // contains some constants
    data = malloc(4);
    data[0] = ENGINE_8900_MEM_BASE; // engine base address
    address_space_rw(nsas, LLB_BASE + 0x208, MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, 4, 1);

    // init two pl080 DMAC0 devices
    dev = qdev_new("pl080");
    PL080State *pl080_1 = PL080(dev);
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    memory_region_add_subregion(sysmem, DMAC0_MEM_BASE, &pl080_1->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_DMAC0_IRQ));

    dev = qdev_new("pl080");
    PL080State *pl080_2 = PL080(dev);
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    memory_region_add_subregion(sysmem, DMAC1_MEM_BASE, &pl080_2->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_DMAC1_IRQ));

    // Init I2C
    dev = qdev_new("ipodtouch.i2c");
    IPodTouchI2CState *i2c_state = IPOD_TOUCH_I2C(dev);
    nms->i2c0_state = i2c_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_I2C0_IRQ));
    memory_region_add_subregion(sysmem, I2C0_MEM_BASE, &i2c_state->iomem);

    // init the accelerometer
    I2CSlave *accelerometer = i2c_slave_create_simple(i2c_state->bus, "lis302dl", 0x1D);

    dev = qdev_new("ipodtouch.i2c");
    i2c_state = IPOD_TOUCH_I2C(dev);
    nms->i2c1_state = i2c_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_I2C1_IRQ));
    memory_region_add_subregion(sysmem, I2C1_MEM_BASE, &i2c_state->iomem);

    // init the PMU
    I2CSlave *pmu = i2c_slave_create_simple(i2c_state->bus, "pcf50633", 0x73);

    // init the ADM
    dev = qdev_new("ipodtouch.adm");
    IPodTouchADMState *adm_state = IPOD_TOUCH_ADM(dev);
    adm_state->nand_state = nand_state;
    nms->adm_state = adm_state;
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_ADM_IRQ));
    memory_region_add_subregion(sysmem, ADM_MEM_BASE, &adm_state->iomem);

    // init MBX
    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &mbx_ops, NULL, "mbx", 0x1000000);
    memory_region_add_subregion(sysmem, MBX_MEM_BASE, iomem);

    // init the chip ID module
    dev = qdev_new("ipodtouch.chipid");
    IPodTouchChipIDState *chipid_state = IPOD_TOUCH_CHIPID(dev);
    nms->chipid_state = chipid_state;
    memory_region_add_subregion(sysmem, CHIPID_MEM_BASE, &chipid_state->iomem);

    // init the TVOut instances
    dev = qdev_new("ipodtouch.tvout");
    IPodTouchTVOutState *tvout_state = IPOD_TOUCH_TVOUT(dev);
    tvout_state->index = 1;
    nms->tvout1_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT1_MEM_BASE, &tvout_state->iomem);

    dev = qdev_new("ipodtouch.tvout");
    tvout_state = IPOD_TOUCH_TVOUT(dev);
    tvout_state->index = 2;
    nms->tvout2_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT2_MEM_BASE, &tvout_state->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, s5l8900_get_irq(nms, S5L8900_TVOUT_SDO_IRQ));

    dev = qdev_new("ipodtouch.tvout");
    tvout_state = IPOD_TOUCH_TVOUT(dev);
    tvout_state->index = 3;
    nms->tvout3_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT3_MEM_BASE, &tvout_state->iomem);

    // setup workaround for TVOut
    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &tvout_workaround_ops, NULL, "tvoutworkaround", 0x4);
    memory_region_add_subregion(sysmem, TVOUT_WORKAROUND_MEM_BASE, iomem);

    qemu_register_reset(ipod_touch_cpu_reset, nms);
}

static void ipod_touch_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    mc->desc = "iPod Touch";
    mc->init = ipod_touch_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm1176");
}

static const TypeInfo ipod_touch_machine_info = {
    .name          = TYPE_IPOD_TOUCH_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(IPodTouchMachineState),
    .class_size    = sizeof(IPodTouchMachineClass),
    .class_init    = ipod_touch_machine_class_init,
    .instance_init = ipod_touch_instance_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_machine_info);
}

type_init(ipod_touch_machine_types)
