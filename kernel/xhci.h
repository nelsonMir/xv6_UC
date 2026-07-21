#ifndef XV6_XHCI_H
#define XV6_XHCI_H

#include "types.h"
#include "memlayout.h"


//Registros de capacidades xHCI
//Estos offsets son relativos a USB_XHCI_BASE

#define XHCI_CAPLENGTH       0x00
#define XHCI_HCIVERSION      0x02
#define XHCI_HCSPARAMS1      0x04
#define XHCI_HCSPARAMS2      0x08
#define XHCI_HCSPARAMS3      0x0c
#define XHCI_HCCPARAMS1      0x10
#define XHCI_DBOFF           0x14
#define XHCI_RTSOFF          0x18
#define XHCI_HCCPARAMS2      0x1c

void xhci_probe(void);

#endif