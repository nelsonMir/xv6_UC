#ifndef XV6_XHCI_H
#define XV6_XHCI_H

#include "types.h"
#include "memlayout.h"


//Registros de capacidades xHCI
//Estos offsets son relativos a VF2_USB_XHCI_BASE

//Primer registro de capacidades
//Contiene CAPLENGTH en los bits 7:0 y HCIVERSION en los bits 31:16
#define XHCI_CAP_DWORD0         0x00

//Indica el número máximo de slots, interrupters y puertos
#define XHCI_HCSPARAMS1         0x04

//Contiene información sobre Event Ring Segments y Scratchpad Buffers
#define XHCI_HCSPARAMS2         0x08

//Contiene las latencias máximas del controlador
#define XHCI_HCSPARAMS3         0x0c

//Indica capacidades adicionales del controlador xHCI
#define XHCI_HCCPARAMS1         0x10

//Indica el offset donde comienzan los registros Doorbell
#define XHCI_DBOFF              0x14

//Indica el offset donde comienzan los registros Runtime
#define XHCI_RTSOFF             0x18

//Contiene capacidades adicionales de versiones posteriores de xHCI
#define XHCI_HCCPARAMS2         0x1c


//Registros operacionales xHCI
//Estos offsets son relativos a la base operacional
//La base operacional se calcula como VF2_USB_XHCI_BASE + CAPLENGTH

//Registro principal de comandos del controlador
#define XHCI_OP_USBCMD          0x00

//Registro principal de estado del controlador
#define XHCI_OP_USBSTS          0x04

//Tamaños de página admitidos por el controlador
#define XHCI_OP_PAGESIZE        0x08

//Control de notificaciones del dispositivo
#define XHCI_OP_DNCTRL          0x14

//Dirección y estado del Command Ring
#define XHCI_OP_CRCR            0x18

//Dirección física del Device Context Base Address Array
#define XHCI_OP_DCBAAP          0x30

//Configuración del número máximo de slots habilitados
#define XHCI_OP_CONFIG          0x38


//Registros de los puertos USB
//El primer puerto comienza en la base operacional más 0x400
//Cada puerto ocupa un bloque de 0x10 bytes

#define XHCI_OP_PORT_BASE       0x400
#define XHCI_PORT_STRIDE        0x10
#define XHCI_PORTSC             0x00


//Bits del registro USBCMD

//Inicia o detiene la ejecución del controlador
#define XHCI_CMD_RUN            (1U << 0)

//Reinicia internamente el controlador xHCI
#define XHCI_CMD_HCRST          (1U << 1)


//Bits del registro USBSTS

//Indica que el controlador está detenido
#define XHCI_STS_HCH            (1U << 0)

//Indica un error grave del sistema host
#define XHCI_STS_HSE            (1U << 2)

//Indica que existe una interrupción pendiente
#define XHCI_STS_EINT           (1U << 3)

//Indica que cambió el estado de algún puerto
#define XHCI_STS_PCD            (1U << 4)

//Indica que el controlador todavía no está preparado
#define XHCI_STS_CNR            (1U << 11)

//Indica un error interno del controlador host
#define XHCI_STS_HCE            (1U << 12)


//Bits del registro PORTSC

//Indica que hay un dispositivo conectado al puerto
#define XHCI_PORTSC_CCS         (1U << 0)

//Indica que el puerto está habilitado
#define XHCI_PORTSC_PED         (1U << 1)

//Indica una condición de sobrecorriente
#define XHCI_PORTSC_OCA         (1U << 3)

//Activa o indica el reset del puerto
#define XHCI_PORTSC_PR          (1U << 4)

//Posición y máscara del estado del enlace USB
#define XHCI_PORTSC_PLS_SHIFT   5
#define XHCI_PORTSC_PLS_MASK    (0xfU << XHCI_PORTSC_PLS_SHIFT)

//Indica o controla la alimentación del puerto
#define XHCI_PORTSC_PP          (1U << 9)

//Posición y máscara de la velocidad del dispositivo conectado
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  (0xfU << XHCI_PORTSC_SPEED_SHIFT)


//Lee y valida los registros de capacidades del controlador
int xhci_probe(void);

//Detiene, reinicia y deja preparado el controlador para configurar DMA
int xhci_reset_controller(void);

#endif