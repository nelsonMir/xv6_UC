// SPDX-License-Identifier: GPL-2.0+
/*
Parte derivada del driver de teclado USB de U-Boot.
(C) Copyright 2001 Denis Peter, MPL AG Switzerland.
Partes derivadas del proyecto USB de Linux.
*/

/*
usb_kbd.c

Conversión de informes HID Boot de ocho bytes a caracteres de consola.

La primera tabla utiliza distribución US porque permite validar rápidamente
letras, números, símbolos, Ctrl y las secuencias ANSI de rvnano. La tabla
española y AltGr quedan aislados para una ampliación posterior.
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "usb_xhci.h"

#define HID_MOD_LEFT_CTRL   USB_BIT(0)
#define HID_MOD_LEFT_SHIFT  USB_BIT(1)
#define HID_MOD_RIGHT_CTRL  USB_BIT(4)
#define HID_MOD_RIGHT_SHIFT USB_BIT(5)

struct usb_keyboard usb_keyboard;
static int usb_caps_lock;
static volatile int usb_kbd_poll_busy;

static int
usb_key_already_pressed(const uchar previous[8], uchar keycode)
{
  uint32 index;

  for(index = 2; index < 8; index++){
    if(previous[index] == keycode)
      return 1;
  }

  return 0;
}

static void
usb_kbd_emit(int character)
{
  if(character != 0)
    consoleintr(character);
}

static void
usb_kbd_emit_sequence(const char *sequence)
{
  while(*sequence != 0){
    consoleintr((uchar)*sequence);
    sequence++;
  }
}

static int
usb_hid_letter(uchar keycode, uchar modifiers)
{
  int character;
  int shift;
  int control;

  character = 'a' + (keycode - 0x04U);
  shift = (modifiers &
           (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;
  control = (modifiers &
             (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL)) != 0;

  if(control)
    return character - 'a' + 1;

  if(shift ^ usb_caps_lock)
    character = character - 'a' + 'A';

  return character;
}

static int
usb_hid_number(uchar keycode, int shift)
{
  static const char normal[] = "1234567890";
  static const char shifted[] = "!@#$%^&*()";
  uint32 index;

  index = keycode - 0x1eU;
  return shift ? shifted[index] : normal[index];
}

static int
usb_hid_punctuation(uchar keycode, int shift)
{
  switch(keycode){
  case 0x2d:
    return shift ? '_' : '-';
  case 0x2e:
    return shift ? '+' : '=';
  case 0x2f:
    return shift ? '{' : '[';
  case 0x30:
    return shift ? '}' : ']';
  case 0x31:
    return shift ? '|' : '\\';
  case 0x33:
    return shift ? ':' : ';';
  case 0x34:
    return shift ? '"' : '\'';
  case 0x35:
    return shift ? '~' : '`';
  case 0x36:
    return shift ? '<' : ',';
  case 0x37:
    return shift ? '>' : '.';
  case 0x38:
    return shift ? '?' : '/';
  default:
    return 0;
  }
}

static void
usb_kbd_process_key(uchar keycode, uchar modifiers)
{
  int shift;
  int character;

  shift = (modifiers &
           (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;

  if(keycode >= 0x04U && keycode <= 0x1dU){
    usb_kbd_emit(usb_hid_letter(keycode, modifiers));
    return;
  }

  if(keycode >= 0x1eU && keycode <= 0x27U){
    usb_kbd_emit(usb_hid_number(keycode, shift));
    return;
  }

  character = usb_hid_punctuation(keycode, shift);
  if(character != 0){
    usb_kbd_emit(character);
    return;
  }

  switch(keycode){
  case 0x28:
    usb_kbd_emit('\n');
    break;
  case 0x29:
    usb_kbd_emit(0x1b);
    break;
  case 0x2a:
    usb_kbd_emit(0x7f);
    break;
  case 0x2b:
    usb_kbd_emit('\t');
    break;
  case 0x2c:
    usb_kbd_emit(' ');
    break;
  case 0x39:
    usb_caps_lock ^= 1;
    break;
  case 0x4a:
    usb_kbd_emit_sequence("\x1b[H");
    break;
  case 0x4c:
    usb_kbd_emit_sequence("\x1b[3~");
    break;
  case 0x4d:
    usb_kbd_emit_sequence("\x1b[F");
    break;
  case 0x4f:
    usb_kbd_emit_sequence("\x1b[C");
    break;
  case 0x50:
    usb_kbd_emit_sequence("\x1b[D");
    break;
  case 0x51:
    usb_kbd_emit_sequence("\x1b[B");
    break;
  case 0x52:
    usb_kbd_emit_sequence("\x1b[A");
    break;
  default:
    break;
  }
}

static void
usb_kbd_process_report(const uchar previous[8], const uchar current[8])
{
  uint32 index;
  uchar keycode;

  //Los códigos 1, 2 y 3 indican ErrorRollOver o errores similares
  for(index = 2; index < 8; index++){
    if(current[index] >= 1U && current[index] <= 3U)
      return;
  }

  for(index = 2; index < 8; index++){
    keycode = current[index];

    if(keycode == 0)
      continue;

    if(!usb_key_already_pressed(previous, keycode))
      usb_kbd_process_key(keycode, current[0]);
  }
}

void
usb_kbd_attach(uint32 slot_id,
               uint32 endpoint_id,
               uint32 interface_number,
               uint32 interval,
               uint32 max_packet,
               uchar *report_buffer)
{
  memset(&usb_keyboard, 0, sizeof(usb_keyboard));
  usb_caps_lock = 0;

  usb_keyboard.present = 1;
  usb_keyboard.slot_id = slot_id;
  usb_keyboard.endpoint_id = endpoint_id;
  usb_keyboard.interface_number = interface_number;
  usb_keyboard.interval = interval;
  usb_keyboard.max_packet = max_packet;
  usb_keyboard.report_buffer = report_buffer;

  printf("usb-kbd: attached slot=%d dci=%d packet=%d interval=%d\n",
         (int)slot_id,
         (int)endpoint_id,
         (int)max_packet,
         (int)interval);
}

int
usb_kbd_present(void)
{
  return usb_keyboard.present;
}

void
usb_kbd_poll(void)
{
  uint32 actual_length;
  int result;

  //Solo un hart puede consumir el Event Ring y volver a encolar el informe
  if(__sync_lock_test_and_set(&usb_kbd_poll_busy, 1) != 0)
    return;

  if(!usb_keyboard.present || !usb_keyboard.transfer_queued)
    goto out;

  result = xhci_poll_keyboard_transfer(usb_keyboard.slot_id,
                                       usb_keyboard.endpoint_id,
                                       &actual_length);

  if(result == 0)
    goto out;

  usb_keyboard.transfer_queued = 0;

  if(result < 0){
    usb_keyboard.present = 0;
    printf("usb-kbd: polling stopped after transfer error\n");
    goto out;
  }

  usb_dma_sync_for_cpu(usb_keyboard.report_buffer,
                       usb_keyboard.max_packet);

  if(actual_length >= 8U){
    usb_kbd_process_report(usb_keyboard.previous,
                           usb_keyboard.report_buffer);
    memmove(usb_keyboard.previous,
            usb_keyboard.report_buffer,
            sizeof(usb_keyboard.previous));
  }

  memset(usb_keyboard.report_buffer, 0, usb_keyboard.max_packet);

  if(xhci_queue_interrupt_in(usb_keyboard.slot_id,
                             usb_keyboard.endpoint_id,
                             usb_keyboard.report_buffer,
                             usb_keyboard.max_packet) < 0){
    usb_keyboard.present = 0;
    printf("usb-kbd: could not requeue Interrupt IN transfer\n");
    goto out;
  }

  usb_keyboard.transfer_queued = 1;

out:
  __sync_lock_release(&usb_kbd_poll_busy);
}
