#ifndef XV6_VF2_USB_H
#define XV6_VF2_USB_H

//Inicializa el wrapper USB del JH7110
//Configura los relojes, los resets y el PHY USB
void vf2_usb_init(void);

//Solicita al controlador Cadence que active el funcionamiento como host
//Espera hasta que el bloque xHCI indique que está preparado
int vf2_usb_start_host(void);


#endif