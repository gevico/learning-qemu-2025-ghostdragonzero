/* w25q16.c - SPI Flash 设备实现 */
#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"

#define TYPE_W25Q16 "w25q16"
#define W25Q16(obj) OBJECT_CHECK(W25Q16State, (obj), TYPE_W25Q16)

/* W25Q16 命令定义 */
#define W25Q16_CMD_JEDEC_ID        0x9F
#define W25Q16_CMD_READ_DATA       0x03
#define W25Q16_CMD_WRITE_ENABLE    0x06
#define W25Q16_CMD_WRITE_DISABLE   0x04
#define W25Q16_CMD_READ_STATUS     0x05

typedef struct W25Q16State {
    SSIPeripheral parent_obj;
    
    uint8_t command;
    uint8_t state;
    uint32_t address;
    uint8_t data[256];
    
    /* JEDEC ID */
    uint8_t jedec_id[3];
} W25Q16State;

static void w25q16_write_protect_pin_irq_handler(void *opaque, int n, int level)
{
    //not
}

static void w25q16_realize(SSIPeripheral *dev, Error **errp)
{
    W25Q16State *s = W25Q16(dev);
    
    /* 初始化 JEDEC ID: Winbond 0xEF, 设备 0x40, 容量 0x15 (16Mb) */
    s->jedec_id[0] = 0xEF;  /* 制造商 ID */
    s->jedec_id[1] = 0x40;  /* 内存类型 */
    s->jedec_id[2] = 0x15;  /* 容量 */
    qdev_init_gpio_in_named(DEVICE(s),
                            w25q16_write_protect_pin_irq_handler, "WP#", 1);
}

static uint32_t w25q16_transfer(SSIPeripheral *dev, uint32_t value)
{
    W25Q16State *s = W25Q16(dev);
    uint8_t ret = 0;

    switch (s->state) {
    case 0: /* 等待命令 */
        s->command = value;
        switch (value) {
        case W25Q16_CMD_JEDEC_ID:
            s->state = 1;  /* 进入 JEDEC ID 读取状态 */
            s->address = 0;
            break;
        case W25Q16_CMD_READ_DATA:
            s->state = 2;  /* 进入地址阶段 */
            s->address = 0;
            break;
        default:
            /* 处理其他命令 */
            break;
        }
        break;
        
    case 1: /* JEDEC ID 传输 */
        if (s->address < 3) {
            ret = s->jedec_id[s->address];
            s->address++;
        } else {
            ret = 0;
        }
        break;
        
    case 2: /* 数据读取 */
        /* 模拟返回数据 */
        ret = 0xFF;  /* 返回 0xFF 表示空数据 */
        break;
        
    default:
        break;
    }

    return ret;
}

static void w25q16_class_init(ObjectClass *klass, const void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    
    k->realize = w25q16_realize;
    k->transfer = w25q16_transfer;
}

static const TypeInfo w25q16_info = {
    .name          = TYPE_W25Q16,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(W25Q16State),
    .class_init    = w25q16_class_init,
};

static void w25q16_register_types(void)
{
    type_register_static(&w25q16_info);
}

type_init(w25q16_register_types)