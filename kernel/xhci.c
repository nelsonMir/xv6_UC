#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "xhci.h"

//Número máximo de lecturas realizadas durante una espera por polling
#define XHCI_POLL_LIMIT 5000000U

//Dirección donde comienzan los registros operacionales
static uint64 xhci_op_base;

//Número de puertos indicado por el controlador
static uint32 xhci_max_ports;

//Indica si la lectura de capacidades terminó correctamente
static int xhci_caps_valid;

static inline uint32
xhci_mmio_read32(uint64 address)
{
uint32 value;

value = *(volatile uint32 *)address;

//Evita que la CPU reordene esta lectura MMIO
__sync_synchronize();

return value;
}

static inline void
xhci_mmio_write32(uint64 address, uint32 value)
{
//Asegura que las escrituras anteriores han terminado
__sync_synchronize();

*(volatile uint32 *)address = value;

//Asegura que la escritura MMIO se completa antes de continuar
__sync_synchronize();
}

static inline uint32
xhci_cap_read32(uint64 offset)
{
return xhci_mmio_read32(VF2_USB_XHCI_BASE + offset);
}

static inline uint32
xhci_op_read32(uint64 offset)
{
return xhci_mmio_read32(xhci_op_base + offset);
}

static inline void
xhci_op_write32(uint64 offset, uint32 value)
{
xhci_mmio_write32(xhci_op_base + offset, value);
}

/*
Espera hasta que los bits seleccionados por mask tengan el valor
indicado por expected.

La función devuelve 0 si se alcanza el estado esperado.
Devuelve -1 si se supera el límite de espera.
*/
static int
xhci_wait32(uint64 address, uint32 mask, uint32 expected)
{
uint32 value;
uint32 i;

value = 0;

for(i = 0; i < XHCI_POLL_LIMIT; i++){
value = xhci_mmio_read32(address);

if((value & mask) == expected)
  return 0;

__asm__ volatile("nop");

}

printf("xhci: timeout address=%p value=0x%x\n",
(void *)address,
value);

printf("xhci: mask=0x%x expected=0x%x\n",
mask,
expected);

return -1;
}

/*
Lee y muestra el registro PORTSC de un puerto.

Los puertos xHCI se numeran desde 1, pero el primer registro PORTSC
se encuentra en la base operacional más el offset 0x400.
*/
static void
xhci_dump_port(uint32 port)
{
uint64 address;
uint32 portsc;
uint32 connected;
uint32 enabled;
uint32 power;
uint32 speed;
uint32 link_state;

address = xhci_op_base
+ XHCI_OP_PORT_BASE
+ ((uint64)(port - 1) * XHCI_PORT_STRIDE)
+ XHCI_PORTSC;

portsc = xhci_mmio_read32(address);

connected = (portsc & XHCI_PORTSC_CCS) != 0;
enabled = (portsc & XHCI_PORTSC_PED) != 0;
power = (portsc & XHCI_PORTSC_PP) != 0;

speed = (portsc & XHCI_PORTSC_SPEED_MASK)
>> XHCI_PORTSC_SPEED_SHIFT;

link_state = (portsc & XHCI_PORTSC_PLS_MASK)
>> XHCI_PORTSC_PLS_SHIFT;

printf("xhci: PORTSC%d address=%p value=0x%x\n",
(int)port,
(void *)address,
portsc);

printf("xhci: port %d connected=%d enabled=%d\n",
(int)port,
(int)connected,
(int)enabled);

printf("xhci: port %d power=%d speed=%d pls=%d\n",
(int)port,
(int)power,
(int)speed,
(int)link_state);
}

int
xhci_probe(void)
{
uint32 cap0;
uint32 caplength;
uint32 version;
uint32 hcsparams1;
uint32 hcsparams2;
uint32 hcsparams3;
uint32 hccparams1;
uint32 dboff_raw;
uint32 rtsoff_raw;
uint32 dboff;
uint32 rtsoff;
uint32 max_slots;
uint32 max_intrs;

xhci_caps_valid = 0;
xhci_op_base = 0;
xhci_max_ports = 0;

printf("xhci: probing Cadence host at %p\n",
(void *)VF2_USB_XHCI_BASE);

/*
CAPLENGTH y HCIVERSION comparten el primer registro de 32 bits.

CAPLENGTH se encuentra en los bits 7:0.
HCIVERSION se encuentra en los bits 31:16.

Se utiliza una lectura alineada de 32 bits para evitar accesos
MMIO de 8 o 16 bits.
*/
cap0 = xhci_cap_read32(XHCI_CAP_DWORD0);

caplength = cap0 & 0xffU;
version = (cap0 >> 16) & 0xffffU;

hcsparams1 = xhci_cap_read32(XHCI_HCSPARAMS1);
hcsparams2 = xhci_cap_read32(XHCI_HCSPARAMS2);
hcsparams3 = xhci_cap_read32(XHCI_HCSPARAMS3);
hccparams1 = xhci_cap_read32(XHCI_HCCPARAMS1);
dboff_raw = xhci_cap_read32(XHCI_DBOFF);
rtsoff_raw = xhci_cap_read32(XHCI_RTSOFF);

/*
Los bits bajos de DBOFF y RTSOFF están reservados.
Se eliminan antes de calcular sus direcciones.
*/
dboff = dboff_raw & ~0x3U;
rtsoff = rtsoff_raw & ~0x1fU;

max_slots = hcsparams1 & 0xffU;
max_intrs = (hcsparams1 >> 8) & 0x7ffU;
xhci_max_ports = (hcsparams1 >> 24) & 0xffU;

printf("xhci: CAP0=0x%x\n", cap0);
printf("xhci: CAPLENGTH=0x%x\n", caplength);
printf("xhci: HCIVERSION=0x%x\n", version);

printf("xhci: HCSPARAMS1=0x%x\n", hcsparams1);
printf("xhci: HCSPARAMS2=0x%x\n", hcsparams2);
printf("xhci: HCSPARAMS3=0x%x\n", hcsparams3);
printf("xhci: HCCPARAMS1=0x%x\n", hccparams1);

printf("xhci: DBOFF raw=0x%x offset=0x%x\n",
dboff_raw,
dboff);

printf("xhci: RTSOFF raw=0x%x offset=0x%x\n",
rtsoff_raw,
rtsoff);

printf("xhci: max slots=%d\n", (int)max_slots);
printf("xhci: max interrupters=%d\n", (int)max_intrs);
printf("xhci: max ports=%d\n", (int)xhci_max_ports);

/*
CAPLENGTH indica el tamaño de los registros de capacidades.
También funciona como offset hasta los registros operacionales.
*/
if(caplength < 0x20){
printf("xhci: invalid CAPLENGTH\n");
return -1;
}

if(max_slots == 0 || xhci_max_ports == 0){
printf("xhci: invalid controller capabilities\n");
return -1;
}

xhci_op_base = VF2_USB_XHCI_BASE + caplength;

printf("xhci: operational base=%p\n",
(void *)xhci_op_base);

printf("xhci: runtime base=%p\n",
(void *)(VF2_USB_XHCI_BASE + (uint64)rtsoff));

printf("xhci: doorbell base=%p\n",
(void *)(VF2_USB_XHCI_BASE + (uint64)dboff));

if(version == 0)
printf("xhci: warning: HCIVERSION reads zero\n");

xhci_caps_valid = 1;

return 0;
}

int
xhci_reset_controller(void)
{
uint32 command;
uint32 status;
uint32 pagesize;
uint32 config;
uint32 port;

if(!xhci_caps_valid){
printf("xhci: reset requested before successful probe\n");
return -1;
}

printf("xhci: waiting for controller ready\n");

/*
El bit CNR permanece activado mientras el controlador todavía
no está preparado para aceptar accesos a sus registros operacionales.
*/
if(xhci_wait32(xhci_op_base + XHCI_OP_USBSTS,
XHCI_STS_CNR,
0) < 0){
printf("xhci: controller remained not-ready\n");
return -1;
}

command = xhci_op_read32(XHCI_OP_USBCMD);
status = xhci_op_read32(XHCI_OP_USBSTS);

printf("xhci: before halt USBCMD=0x%x USBSTS=0x%x\n",
command,
status);

/*
El reset interno HCRST debe realizarse con el controlador detenido.

Si HCHalted no está activado, se limpia el bit Run/Stop y se espera
hasta que el hardware indique que ha terminado su ejecución.
*/
if((status & XHCI_STS_HCH) == 0){
printf("xhci: controller running; requesting halt\n");


command &= ~XHCI_CMD_RUN;
xhci_op_write32(XHCI_OP_USBCMD, command);

if(xhci_wait32(xhci_op_base + XHCI_OP_USBSTS,
               XHCI_STS_HCH,
               XHCI_STS_HCH) < 0){
  printf("xhci: controller did not halt\n");
  return -1;
}


}

status = xhci_op_read32(XHCI_OP_USBSTS);

printf("xhci: controller halted USBSTS=0x%x\n",
status);

/*
Se activa HCRST para reiniciar el estado interno del xHCI.

El propio hardware limpia el bit cuando termina el reset.
*/
command = xhci_op_read32(XHCI_OP_USBCMD);
command |= XHCI_CMD_HCRST;

printf("xhci: asserting HCRST\n");

xhci_op_write32(XHCI_OP_USBCMD, command);

if(xhci_wait32(xhci_op_base + XHCI_OP_USBCMD,
XHCI_CMD_HCRST,
0) < 0){
printf("xhci: HCRST did not clear\n");
return -1;
}

/*
Después del reset, el controlador puede volver a activar CNR
durante su inicialización interna.
*/
if(xhci_wait32(xhci_op_base + XHCI_OP_USBSTS,
XHCI_STS_CNR,
0) < 0){
printf("xhci: controller not ready after reset\n");
return -1;
}

command = xhci_op_read32(XHCI_OP_USBCMD);
status = xhci_op_read32(XHCI_OP_USBSTS);
pagesize = xhci_op_read32(XHCI_OP_PAGESIZE);
config = xhci_op_read32(XHCI_OP_CONFIG);

printf("xhci: reset complete\n");
printf("xhci: USBCMD=0x%x\n", command);
printf("xhci: USBSTS=0x%x\n", status);
printf("xhci: PAGESIZE=0x%x\n", pagesize);
printf("xhci: CONFIG=0x%x\n", config);

/*
El bit 0 de PAGESIZE indica que el controlador admite páginas
de 4096 bytes, que es el tamaño de página utilizado por xv6.
*/
if((pagesize & 1U) == 0){
printf("xhci: controller does not advertise 4 KiB pages\n");
return -1;
}

if((status & XHCI_STS_HCH) == 0){
printf("xhci: controller is unexpectedly running after reset\n");
return -1;
}

if(status & (XHCI_STS_HSE | XHCI_STS_HCE)){
printf("xhci: controller reports an error after reset\n");
return -1;
}

for(port = 1; port <= xhci_max_ports; port++)
xhci_dump_port(port);

printf("xhci: controller reset and ready for DMA setup\n");

//Todavía no se activa el bit Run/Stop
return 0;
}
