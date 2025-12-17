#include "hall_midi.h"

#include "ch.h"
#include "drv_hall.h"
#include "usb_device.h"
#include "midi.h"

#include <stdbool.h>
#include <string.h>

#define HALL_MIDI_CHANNEL               0U     /* Canal MIDI 1 (0-indexé) */
#define HALL_MIDI_BASE_NOTE             60U    /* C4 */
#define HALL_MIDI_VELOCITY_ON           100U
#define HALL_MIDI_VELOCITY_OFF          0U
#define HALL_MIDI_POLL_INTERVAL_MS      1U     /* Boucle rapide 1–2 ms */
#define HALL_MIDI_NOTE_START            24U    /* C1  */
#define HALL_MIDI_NOTE_FIRST_TASK       25U    /* C#1 */
#define HALL_MIDI_NOTE_ACTIVITY         26U    /* D1  */
#define HALL_MIDI_USB_WARMUP_MS         300U
#define HALL_MIDI_THREAD_WA             1024

static THD_WORKING_AREA(hall_midi_wa, HALL_MIDI_THREAD_WA);
static thread_t *hall_midi_thread = NULL;
static systime_t usb_active_since = 0;
static bool usb_ready_for_midi = false;

static inline uint8_t clamp_note(uint32_t note) {
  if (note > 127U) {
    return 127U;
  }
  return (uint8_t)note;
}

static inline uint8_t sensor_to_note(uint8_t sensor_index) {
  return clamp_note((uint32_t)HALL_MIDI_BASE_NOTE + (uint32_t)sensor_index);
}

static void reset_sensor_states(bool *states) {
  memset(states, 0, sizeof(bool) * BRICK_NUM_HALL_SENSORS);
}

static inline bool any_raw_activity(void) {
  for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
    if (drv_hall_get_raw(i) != 0U) {
      return true;
    }
  }
  return false;
}

static void send_all_notes_off(void) {
  if (usb_device_active()) {
    midi_all_notes_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL);
  }
}

static THD_FUNCTION(hall_midi_thread_func, arg) {
  (void)arg;
  chRegSetThreadName("hall_midi");

  bool prev_state[BRICK_NUM_HALL_SENSORS];
  bool first_task_notified = false;
  bool raw_activity_notified = false;
  bool usb_was_active = false;

  drv_hall_init();
  reset_sensor_states(prev_state);

  while (true) {
    drv_hall_task();

    const bool usb_active = usb_device_active();
    const systime_t now = chVTGetSystemTimeX();

    if (usb_active && !usb_was_active) {
      usb_active_since = now;
      usb_ready_for_midi = false;
    }

    if (!usb_active) {
      usb_ready_for_midi = false;
      if (usb_was_active) {
        send_all_notes_off();
        reset_sensor_states(prev_state);
      } else {
        for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
          prev_state[i] = drv_hall_is_pressed(i);
        }
      }
    }

    if (usb_active && !usb_ready_for_midi &&
        (chVTTimeElapsedSinceX(usb_active_since) >= TIME_MS2I(HALL_MIDI_USB_WARMUP_MS))) {
      usb_ready_for_midi = true;
      send_all_notes_off();
      reset_sensor_states(prev_state);
      first_task_notified = false;
      raw_activity_notified = false;
    }

    if (usb_active && usb_ready_for_midi) {
      if (!first_task_notified) {
        midi_note_on(MIDI_DEST_USB, HALL_MIDI_CHANNEL, HALL_MIDI_NOTE_FIRST_TASK, HALL_MIDI_VELOCITY_ON);
        first_task_notified = true;
      }

      if (!raw_activity_notified && any_raw_activity()) {
        midi_note_on(MIDI_DEST_USB, HALL_MIDI_CHANNEL, HALL_MIDI_NOTE_ACTIVITY, HALL_MIDI_VELOCITY_ON);
        raw_activity_notified = true;
      }

      for (uint8_t i = 0; i < BRICK_NUM_HALL_SENSORS; ++i) {
        const bool pressed = drv_hall_is_pressed(i);

        if (pressed != prev_state[i]) {
          const uint8_t note = sensor_to_note(i);

          if (pressed) {
            midi_note_on(MIDI_DEST_USB, HALL_MIDI_CHANNEL, note, HALL_MIDI_VELOCITY_ON);
          } else {
            midi_note_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL, note, HALL_MIDI_VELOCITY_OFF);
          }
        }

        prev_state[i] = pressed;
      }
    }

    usb_was_active = usb_active;
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
