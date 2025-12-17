#include "hall_midi.h"

#include "ch.h"
#include "drv_hall.h"
#include "midi.h"
#include "usb_device.h"

#include <stdbool.h>
#include <string.h>

#define HALL_MIDI_CHANNEL               0U     /* Canal MIDI 1 (0-indexé) */
#define HALL_MIDI_BASE_NOTE             60U    /* C4 */
#define HALL_MIDI_VELOCITY_ON           100U
#define HALL_MIDI_VELOCITY_OFF          0U
#define HALL_MIDI_POLL_INTERVAL_MS      2U     /* Boucle rapide 1–5 ms */
#define HALL_MIDI_USB_CHECK_INTERVAL_MS 5U
#define HALL_MIDI_USB_STABLE_MS         200U   /* USB doit rester actif pendant 200 ms */
#define HALL_MIDI_THREAD_WA             1024

static THD_WORKING_AREA(hall_midi_wa, HALL_MIDI_THREAD_WA);
static thread_t *hall_midi_thread = NULL;

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

static void send_all_notes_off(void) {
  midi_all_notes_off(MIDI_DEST_USB, HALL_MIDI_CHANNEL);
}

static void wait_for_usb_ready(void) {
  systime_t active_since = 0;
  bool tracking = false;

  while (true) {
    if (usb_device_active()) {
      if (!tracking) {
        active_since = chVTGetSystemTimeX();
        tracking = true;
      } else if (chVTTimeElapsedSinceX(active_since) >= TIME_MS2I(HALL_MIDI_USB_STABLE_MS)) {
        return;
      }
    } else {
      tracking = false;
    }

    chThdSleepMilliseconds(HALL_MIDI_USB_CHECK_INTERVAL_MS);
  }
}

static THD_FUNCTION(hall_midi_thread_func, arg) {
  (void)arg;
  chRegSetThreadName("hall_midi");

  bool prev_state[BRICK_NUM_HALL_SENSORS];
  bool hall_initialized = false;

  reset_sensor_states(prev_state);

  while (true) {
    /* Attendre une liaison USB stable (200 ms consécutives) avant toute activité Hall. */
    wait_for_usb_ready();

    if (!hall_initialized) {
      drv_hall_init();
      hall_initialized = true;
    }

    /* Synchronise l'état des capteurs avant de commencer à émettre des notes. */
    drv_hall_task();
    reset_sensor_states(prev_state);
    send_all_notes_off();

    /* Boucle principale pendant que l'USB reste actif. */
    while (usb_device_active()) {
      drv_hall_task();

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

      chThdSleepMilliseconds(HALL_MIDI_POLL_INTERVAL_MS);
    }

    /* USB hors ligne : couper toutes les notes et suspendre le polling Hall. */
    send_all_notes_off();
    reset_sensor_states(prev_state);
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
