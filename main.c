#include "ch.h"
#include "hal.h"

#include "usb_device.h"
#include "midi.h"

/*
 * Envoie un "C4" (note 60) toutes les secondes sur USB-MIDI.
 * Canal = 0 (MIDI CH1), vélocité = 100.
 */

#define MIDI_CH     0U
#define NOTE_C4     60U
#define NOTE_VEL    100U
#define NOTE_OFFVEL 0U

int main(void) {

  halInit();
  chSysInit();

  /* Init MIDI (crée le thread TX USB + init UART DIN côté midi.c) */
  midi_init();

  /* Démarre l’USB device + ré-énumération */
  usb_device_start();

  while (true) {

    /* Attendre que l’hôte ait configuré le device (endpoint IN prêt) */
    if (!usb_device_active()) {
      chThdSleepMilliseconds(50);
      continue;
    }

    /* Note ON */
    midi_note_on(MIDI_DEST_USB, MIDI_CH, NOTE_C4, NOTE_VEL);
    chThdSleepMilliseconds(100);

    /* Note OFF */
    midi_note_off(MIDI_DEST_USB, MIDI_CH, NOTE_C4, NOTE_OFFVEL);

    /* 1 seconde entre débuts de notes */
    chThdSleepMilliseconds(900);
  }
}
