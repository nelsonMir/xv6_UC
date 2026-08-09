// SPDX-License-Identifier: GPL-2.0+
/*
vf2_pcie.h

Interfaz del controlador PCIe0 de la VisionFive 2 y de la detección mínima
del VIA VL805.
*/

#ifndef XV6_VF2_PCIE_H
#define XV6_VF2_PCIE_H

#include "types.h"

//Función existente de la etapa 1 ya validada en la placa
int vf2_pcie0_init(void);
int vf2_pcie0_link_up(void);

//Funciones añadidas para ATR, ECAM, BAR y activación del VL805
int    vf2_pcie0_probe_vl805(void);
uint64 vf2_pcie0_vl805_bar(void);

//Inicializa toda la ruta PCIe, xHCI, USB y teclado
int vf2_usb_keyboard_init(void);

#endif
