#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "qemu/bitops.h"

// IPC
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#define SHMSZ     40
#include <signal.h>

#define TYPE_ADDER "adder"
#define ADDER(obj) \
    OBJECT_CHECK(AdderState, (obj), TYPE_ADDER)

/* Register map */
#define REG_ID                 0x0
#define CHIP_ID                0xf001

#define REG_INIT               0x4
#define CHIP_EN                BIT(1)

#define REG_A                  0x8
#define REG_B                  0xc
#define REG_O                  0x10

#define REG_INT_STATUS         0x14
#define INT_ENABLED            BIT(0)
#define INT_BUFFER_DEQ         BIT(1)

typedef struct AdderState {
    SysBusDevice parent_obj; // is a system Bus device (MMIO)
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t input_A;
    uint32_t input_B;
    uint32_t output;
    uint32_t id;
    uint32_t init;
    uint32_t status;
    // IPC
    int shmid;
    key_t key;
    void* shm;
    int *mmio;
} AdderState;

/* Design functionality */
static void adder_set_irq(AdderState *s, int irq)
{
    s->status = irq;
    qemu_set_irq(s->irq, 1);
}

static void adder_clr_irq(AdderState *s)
{
    qemu_set_irq(s->irq, 0);
}

AdderState *glob_s;
/* Adder interrupt service routine */
void adder_set_isr(int signum)
{
    printf("[DEBUG] adder_set_isr signum = %d\n",signum);
    adder_set_irq(glob_s, INT_BUFFER_DEQ);
}

static uint64_t adder_read(void *opaque, hwaddr offset, unsigned size)
{
    AdderState *s = (AdderState *)opaque;
    bool is_enabled = s->init & CHIP_EN;

    if (!is_enabled) {
        fprintf(stderr, "Device is disabled\\n");
        return 0;
    }

    switch (offset) {
    case REG_ID:
        return s->id;
    case REG_INIT:
        return s->init;
    case REG_O:
        //return s->output;
        return s->mmio[2];
    case REG_A:
        //return s->input_A;
        return s->mmio[0];
    case REG_B:
        //return s->input_B;
        return s->mmio[1];
    case REG_INT_STATUS:
        adder_clr_irq(s);
        return s->status;
    default:
        break;
    }

    return 0;
}

static void adder_write(void *opaque, hwaddr offset, uint64_t value,
                          unsigned size)
{
    AdderState *s = (AdderState *)opaque;

    switch (offset) {
    case REG_INIT:
        s->init = (int)value;

        if (value)
            adder_set_irq(s, INT_ENABLED);

        break;
    case REG_A:
        //s->input_A = (int)value;
        //s->output = s->input_A + s-> input_B;
        s->mmio[0] = (int)value;
        glob_s=s;
        kill(SIGINT, s->mmio[4]);
        //adder_set_irq(s, INT_BUFFER_DEQ);
        break;
    case REG_B:
        //s->input_B = (int)value;
        //s->output = s->input_A + s-> input_B;
        s->mmio[1] = (int)value;
        glob_s=s;
        kill(SIGINT, s->mmio[4]);
        //adder_set_irq(s, INT_BUFFER_DEQ);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps adder_ops = {
    .read = adder_read,
    .write = adder_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* How to realize the device */
static void adder_realize(DeviceState *dev, Error **errp)
{
    printf("[DEBUG] adder_realize\n");
    AdderState *s = ADDER(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    memory_region_init_io(&s->iomem, OBJECT(s), &adder_ops, s,
                          TYPE_ADDER, 0x200);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    
    // Initialize the device
    s->input_A = 0;
    s->input_B = 0;
    s->output = 0;
    // IPC init
    s->key = 5678;
    if ((s->shmid = shmget(s->key, SHMSZ, 0666)) < 0) {
        perror("shmget");
        exit(1);
    }
    if ((s->shm = shmat(s->shmid, NULL, 0)) == (char*) - 1) {
        perror("shmat");
        exit(1);
    }
    s->mmio = (int*)(s->shm);
    s->mmio[3] = getpid();
    printf("[DEBUG] pid = %d\n",s->mmio[3]);
    // siganl
    signal(SIGINT,adder_set_isr);

}

/* Enclosure the device into class */
static void adder_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = adder_realize;
}

/* setting device info and class link */
static const TypeInfo adder_info = {
    .name          = TYPE_ADDER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AdderState),
    .class_init    = adder_class_init,
};

/* register device */
static void adder_register_types(void)
{
    type_register_static(&adder_info);
}

type_init(adder_register_types);