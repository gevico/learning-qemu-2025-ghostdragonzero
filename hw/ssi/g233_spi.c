#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/g233_spi.h"
#include "qemu/fifo8.h"

#ifndef STM_SPI_ERR_DEBUG
#define STM_SPI_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (STM_SPI_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)
#define FIFO_CAPACITY   1

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);
//写入复位值
    s->spi_cr1 = 0x00000000;
    s->spi_cr2 = 0x00000000;
    s->spi_sr = 0x00000002;
    s->spi_dr = 0x0000000C;
    s->spi_csctrl = 0x00000000;
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);
    
}

static void g233_spi_update_cs(G233SPIState *s)
{
    int i;

    for (i = 0; i < s->num_cs; i++) {
        if (s->spi_csctrl & (1 << i)) {
            qemu_set_irq(s->cs_lines[i], 0);
        }
    }
}

static void g233_spi_update_irq(G233SPIState *s)
{
    int level;

    if (!fifo8_is_empty(&s->rx_fifo)) {
        s->spi_sr |= G233_SPI_SR_RXNE;
        if (s->spi_cr2 & G233_SPI_CR2_RXNEIE) {
            level = 1;
        }
    } else {
        s->spi_sr &= ~G233_SPI_SR_RXNE;
    }

    if (!fifo8_is_empty(&s->tx_fifo)) {
        s->spi_sr |= G233_SPI_SR_TXE;
        if (s->spi_cr2 & G233_SPI_CR2_TXEIE) {
            level = 1;
        }
    } else {
        s->spi_sr &= ~G233_SPI_SR_TXE;
    }


    if (s->spi_sr &(G233_SPI_SR_OVERRUN | G233_SPI_SR_UNDERRUN)) {
        if (s->spi_cr2 & G233_SPI_CR2_ERRIE) {
            level = 1;
        }
    }

    qemu_set_irq(s->irq, level);
}

static void g233_spi_transfer(G233SPIState *s)
{

    uint8_t tx;
    uint8_t rx;


    while (!fifo8_is_empty(&s->tx_fifo)) {
        tx = fifo8_pop(&s->tx_fifo);
        rx = ssi_transfer(s->spi, tx);

        if (!fifo8_is_full(&s->rx_fifo)) {
            fifo8_push(&s->rx_fifo, rx);
        }else{
            s->spi_sr |= G233_SPI_SR_OVERRUN;
        }
    }
    s->spi_sr |= G233_SPI_SR_RXNE;

}



static uint64_t g233_spi_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    G233SPIState *s = opaque;
     uint32_t r;

    DB_PRINT("Address: 0x%" HWADDR_PRIx "\n", addr);

    switch (addr) {
   
    case G233_SPI_CR1:
        r=s->spi_cr1;
        break;
    case G233_SPI_CR2:
        r=s->spi_cr2;
        break;
    
    case G233_SPI_SR:
        r=s->spi_sr;
        break;
    case G233_SPI_DR:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            g233_spi_transfer(s);
            r = fifo8_pop(&s->rx_fifo);
            break;
        } else {
            s->spi_sr |= G233_SPI_SR_UNDERRUN;
            return 0;
        }
    case G233_SPI_CSCTRL:
        r=s->spi_csctrl;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }

    g233_spi_update_irq(s);
    return r;
}

static void g233_spi_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    G233SPIState *s = opaque;
    uint32_t value = val64;

    DB_PRINT("Address: 0x%" HWADDR_PRIx ", Value: 0x%x\n", addr, value);

    switch (addr) {
        
    case G233_SPI_CR1:
        s->spi_cr1 = value;
        break;
    case G233_SPI_CR2:
        qemu_log_mask(LOG_UNIMP, "%s: " \
                      "Interrupts and DMA are not implemented\n", __func__);
        s->spi_cr2 = value;
        break;
    
    case G233_SPI_SR:

        break;
    case G233_SPI_DR:
        if (!fifo8_is_full(&s->tx_fifo)) {
            fifo8_push(&s->tx_fifo, (uint8_t)value);
            g233_spi_transfer(s);
        }else {
            s->spi_sr |= G233_SPI_SR_OVERRUN;
        }
        break;
    case G233_SPI_CSCTRL:
        qemu_log_mask(LOG_UNIMP, "%s: CRC is not implemented\n", __func__);
        s->spi_csctrl = value;
        g233_spi_update_cs(s);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n", __func__, addr);
    }

    g233_spi_update_irq(s);

}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    G233SPIState *s = G233_SPI(dev);

    int i;

    s->spi = ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);

    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (i = 0; i < s->num_cs; i++) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_ops, s,
                          TYPE_G233_SPI, 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);

    fifo8_create(&s->tx_fifo, FIFO_CAPACITY);
    fifo8_create(&s->rx_fifo, FIFO_CAPACITY);

}

static const Property g233_spi_properties[] = {
    DEFINE_PROP_UINT32("num-cs", G233SPIState, num_cs, 4),
};
//是支持num_cs为4

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, g233_spi_properties);
    device_class_set_legacy_reset(dc, g233_spi_reset);
    dc->realize = g233_spi_realize;
}

static const TypeInfo g233_spi_info = {
    .name           = TYPE_G233_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(G233SPIState),
    .class_init     = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)
