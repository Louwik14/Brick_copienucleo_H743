#include "hall_midi.h"

#include "ch.h"
#include "drv_hall.h"
#include "midi.h"
#include "usb_device.h"

#include <stdbool.h>

#define HALL_MIDI_CHANNEL          0U
#define HALL_MIDI_BASE_NOTE        60U
#define HALL_MIDI_VELOCITY_ON      100U
#define HALL_MIDI_VELOCITY_OFF     0U
#define HALL_MIDI_POLL_INTERVAL_MS 2U
#define HALL_MIDI_THREAD_WA        1024

static THD_WORKING_AREA(hall_midi_wa, HALL_MIDI_THREAD_WA);
static thread_t *hall_midi_thread = NULL;

static inline uint8_t clamp_note(uint32_t note) {
  if (note > 127U) {
    return 127U;
  }
  return (uint8_t)note;
}

static THD_FUNCTION(hall_midi_thread_func, arg) {
  (void)arg;
  chRegSetThreadName("hall_midi");

  drv_hall_init();
  drv_hall_task();

  bool prev_state[BRICK_NUM_HALL_SENSORS];
  for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
    prev_state[i] = drv_hall_is_pressed(i);
  }

  bool usb_prev_active = usb_device_active();

  if (usb_prev_active) {
    midi_all_notes_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL);
  }

  while (true) {
    drv_hall_task();

    const bool usb_active = usb_device_active();

    if (usb_active && !usb_prev_active) {
      midi_all_notes_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL);
    } else if (!usb_active && usb_prev_active) {
      midi_all_notes_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL);
    }

    for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
      const bool pressed = drv_hall_is_pressed(i);

      if (usb_active && (pressed != prev_state[i])) {
        const uint8_t note = clamp_note((uint32_t)HALL_MIDI_BASE_NOTE + (uint32_t)i);

        if (pressed) {
          midi_note_on(MIDI_DEST_USB, HALL_MIDI_CHANNEL, note, HALL_MIDI_VELOCITY_ON);
        } else {
          midi_note_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL, note, HALL_MIDI_VELOCITY_OFF);
        }
      }

      prev_state[i] = pressed;
    }

    usb_prev_active = usb_active;
    chThdSleepMilliseconds(HALL_MIDI_POLL_INTERVAL_MS);
  }
}

void hall_midi_start(void) {
  if (hall_midi_thread != NULL) {
    return;
  }

  hall_midi_thread = chThdCreateStatic(
      hall_midi_wa,
      sizeof(hall_midi_wa),
      LOWPRIO,
      hall_midi_thread_func,
      NULL
  );
}
