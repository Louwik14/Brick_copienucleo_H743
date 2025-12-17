#include "ch.h"
#include "hal.h"

#include "usb_device.h"
#include "midi.h"
#include "hall_midi.h"

int main(void) {

  halInit();
  chSysInit();

  /* Init MIDI (crée le thread TX USB + init UART DIN côté midi.c) */
  midi_init();

  /* Démarre l’USB device + ré-énumération */
  usb_device_start();

  /* Thread de scan Hall -> MIDI */
  hall_midi_start();

  while (true) {
    chThdSleepMilliseconds(100);
  }
}
