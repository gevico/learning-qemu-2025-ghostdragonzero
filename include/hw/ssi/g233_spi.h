#ifndef G233_SPI_H
#define G233_SPI_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define G233_SPI_CR1     0x00
#define G233_SPI_CR2     0x04
#define G233_SPI_SR      0x08
#define G233_SPI_DR      0x0C
#define G233_SPI_CSCTRL   0x10

#define TYPE_G233_SPI "g233-spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

struct G233SPIState {
    /* <private> */
    SysBusDevice parent_obj;
    qemu_irq irq;
    SSIBus *spi;
    /* <public> */
    uint32_t num_cs;
    MemoryRegion mmio;

    uint32_t spi_cr1;
    uint32_t spi_cr2;
    uint32_t spi_sr;
    uint32_t spi_dr;
    uint32_t spi_csctrl;

    qemu_irq *cs_lines;


};

#endif 