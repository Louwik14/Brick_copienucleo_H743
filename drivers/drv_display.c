#include "drv_display.h"
#include "spi5_bus.h"
#include "ch.h"
#include "hal.h"
#include "brick_config.h"

#include <stdbool.h>
#include <string.h>

/* ====================================================================== */
/*                        CONFIGURATION MATÉRIELLE                        */
/* ====================================================================== */

static const SPIConfig oled_spicfg = {
  .circular = false,
  .slave    = false,
  .data_cb  = NULL,
  .error_cb = NULL,

  /* ~10–15 MHz selon l’horloge SPI. */
  .cfg1 = SPI_CFG1_MBR_2 |               /* Prescaler */
          SPI_CFG1_DSIZE_VALUE(7),       /* 8 bits */

  .cfg2 = SPI_CFG2_MASTER |
          SPI_CFG2_SSM    |
          (0U << SPI_CFG2_CPOL_Pos) |
          (0U << SPI_CFG2_CPHA_Pos) |
          (0U << SPI_CFG2_LSBFRST_Pos)
};

/* ====================================================================== */
/*                             VARIABLES INTERNES                         */
/* ====================================================================== */

#define DISPLAY_DMA_BUFFER_ATTR __attribute__((section(".ram_d2"), aligned(32)))
#define DISPLAY_FRAMEBUFFER_SIZE (BRICK_OLED_WIDTH * BRICK_OLED_HEIGHT / 8)
#define DISPLAY_NUM_BUFFERS      2U

/* Deux framebuffers statiques en RAM D2 non-cacheable pour des transferts DMA sûrs. */
static DISPLAY_DMA_BUFFER_ATTR uint8_t framebuffers[DISPLAY_NUM_BUFFERS][DISPLAY_FRAMEBUFFER_SIZE];
static DISPLAY_DMA_BUFFER_ATTR uint8_t cmd_byte;
static binary_semaphore_t frame_sem;
static const font_t *current_font = NULL;
static thread_t *display_tp = NULL;
static bool display_initialized = false;

/* Gestion de la double-bufferisation. */
static volatile uint8_t draw_index = 0U;
static volatile uint8_t pending_index = 0U;
static volatile bool frame_pending = false;

/* ====================================================================== */
/*                              UTILITAIRES GPIO                          */
/* ====================================================================== */

static inline void oled_select(void)   { palClearLine(LINE_SPI5_CS_OLED); }
static inline void oled_unselect(void) { palSetLine(LINE_SPI5_CS_OLED);   }
static inline void oled_cmd_mode(void) { palClearLine(LINE_SPI5_DC_OLED); }
static inline void oled_data_mode(void){ palSetLine  (LINE_SPI5_DC_OLED); }

/* ====================================================================== */
/*                              UTILITAIRES SPI                           */
/* ====================================================================== */

static void oled_write_command_locked(uint8_t cmd) {
  cmd_byte = cmd;
  oled_cmd_mode();
  oled_select();
  spiSend(&SPID5, 1, &cmd_byte);
  oled_unselect();
}

static void oled_write_data_locked(const uint8_t *data, size_t len) {
  oled_data_mode();
  oled_select();
  spiSend(&SPID5, len, data);
  oled_unselect();
}

/* ====================================================================== */
/*                              FRAMEBUFFER                               */
/* ====================================================================== */

static inline uint8_t *draw_buffer(void) {
  return framebuffers[draw_index];
}

uint8_t* drv_display_get_buffer(void) {
  return draw_buffer();
}

/* ====================================================================== */
/*                              PIXELS                                    */
/* ====================================================================== */

static inline void set_pixel(uint8_t *fb, int x, int y, bool on) {
  if (x < 0 || x >= BRICK_OLED_WIDTH || y < 0 || y >= BRICK_OLED_HEIGHT) {
    return;
  }

  const int index = x + (y >> 3) * BRICK_OLED_WIDTH;
  const uint8_t mask = (uint8_t)(1U << (y & 7));

  if (on) {
    fb[index] |= mask;
  } else {
    fb[index] &= (uint8_t)~mask;
  }
}

/* ====================================================================== */
/*                      INITIALISATION OLED                               */
/* ====================================================================== */

static void oled_reset_panel(void) {
  palClearLine(LINE_SPI5_RES_OLED);
  chThdSleepMilliseconds(10);
  palSetLine(LINE_SPI5_RES_OLED);
  chThdSleepMilliseconds(10);
}

static void oled_init_panel(void) {
  spi5_bus_acquire(&oled_spicfg);

  oled_write_command_locked(0xAE);
  oled_write_command_locked(0xD5); oled_write_command_locked(0x80);
  oled_write_command_locked(0xA8); oled_write_command_locked(0x3F);
  oled_write_command_locked(0xD3); oled_write_command_locked(0x00);
  oled_write_command_locked(0x40);
  oled_write_command_locked(0x8D); oled_write_command_locked(0x14);
  oled_write_command_locked(0x20); oled_write_command_locked(0x00);
  oled_write_command_locked(0xA1);
  oled_write_command_locked(0xC8);
  oled_write_command_locked(0xDA); oled_write_command_locked(0x12);
  oled_write_command_locked(0xAF);

  spi5_bus_release();
}

static void oled_flush_frame(const uint8_t *frame) {
  spi5_bus_acquire(&oled_spicfg);

  for (uint8_t page = 0; page < 8; page++) {
    oled_write_command_locked(0xB0 + page);
    oled_write_command_locked(0x00);
    oled_write_command_locked(0x10);
    oled_write_data_locked(&frame[page * BRICK_OLED_WIDTH], BRICK_OLED_WIDTH);
  }

  spi5_bus_release();
}

void drv_display_init(void) {
  if (display_initialized) {
    return;
  }

  spi5_bus_init();
  chBSemObjectInit(&frame_sem, false);

  /* Reset buffers deterministically. */
  for (uint8_t i = 0; i < DISPLAY_NUM_BUFFERS; ++i) {
    memset(framebuffers[i], 0x00, DISPLAY_FRAMEBUFFER_SIZE);
  }

  draw_index = 0U;
  pending_index = 0U;
  frame_pending = false;

  oled_reset_panel();
  oled_init_panel();

  extern const font_t FONT_5X7;
  current_font = &FONT_5X7;

  display_initialized = true;
}

/* ====================================================================== */
/*                               CLEAR                                    */
/* ====================================================================== */

void drv_display_clear(void) {
  memset(draw_buffer(), 0x00, DISPLAY_FRAMEBUFFER_SIZE);
}

/* ====================================================================== */
/*                         SOUMISSION DE FRAME                            */
/* ====================================================================== */

static bool drv_display_submit_frame(void) {
  bool queued = false;
  uint8_t src_index = 0U;
  uint8_t dst_index = 0U;

  chSysLock();
  if (!frame_pending) {
    src_index = draw_index;
    dst_index = draw_index ^ 1U;
    pending_index = src_index;
    draw_index = dst_index; /* bascule sur l’autre buffer pour le dessin suivant */
    frame_pending = true;
    chBSemSignalI(&frame_sem);
    queued = true;
  }
  chSysUnlock();

  /* Maintient le nouvel espace de dessin cohérent avec la dernière frame
     soumise pour permettre les mises à jour incrémentales sans artefact. */
  if (queued) {
    memcpy(framebuffers[dst_index], framebuffers[src_index], DISPLAY_FRAMEBUFFER_SIZE);
  }

  return queued;
}

void drv_display_update(void) {
  (void)drv_display_submit_frame();
}

/* ====================================================================== */
/*                              TEXTE                                     */
/* ====================================================================== */

void drv_display_set_font(const font_t *font) {
  current_font = font;
}

static inline uint8_t font_advance(const font_t *f) {
  return (uint8_t)(f->width + f->spacing);
}

void drv_display_draw_char(uint8_t x, uint8_t y, char c) {
  uint8_t *fb = draw_buffer();

  if (!current_font) return;

  if ((uint8_t)c < current_font->first || (uint8_t)c > current_font->last) {
    c = '?';
  }

  for (uint8_t col = 0; col < current_font->width; col++) {
    const uint8_t bits = current_font->get_col(c, col);
    for (uint8_t row = 0; row < current_font->height; row++) {
      if (bits & (1U << row)) {
        set_pixel(fb, x + col, y + row, true);
      }
    }
  }
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt) {

  if (!current_font || !txt) return;

  const uint8_t adv = font_advance(current_font);

  while (*txt && x < BRICK_OLED_WIDTH) {
    drv_display_draw_char(x, y, *txt++);
    x = (uint8_t)(x + adv);
  }
}

void drv_display_draw_text_with_font(const font_t *font,
                                     uint8_t x, uint8_t y,
                                     const char *txt) {

  if (!font || !txt) return;

  const font_t *save = current_font;
  current_font = font;
  drv_display_draw_text(x, y, txt);
  current_font = save;
}

void drv_display_draw_text_at_baseline(const font_t *font,
                                       uint8_t x, uint8_t baseline_y,
                                       const char *txt) {

  if (!font || !txt) return;

  const font_t *save = current_font;
  current_font = font;

  uint8_t y = (baseline_y >= font->height)
              ? (baseline_y - font->height)
              : 0;

  drv_display_draw_text(x, y, txt);
  current_font = save;
}

/* ====================================================================== */
/*                         THREAD DE RAFRAÎCHISSEMENT                     */
/* ====================================================================== */

static THD_WORKING_AREA(waDisplay, 768);

static THD_FUNCTION(displayThread, arg) {
  (void)arg;
  chRegSetThreadName("Display");

  while (!chThdShouldTerminateX()) {
    chBSemWait(&frame_sem);

    if (chThdShouldTerminateX()) {
      break;
    }

    uint8_t tx_index;

    chSysLock();
    if (!frame_pending) {
      chSysUnlock();
      continue;
    }
    tx_index = pending_index;
    frame_pending = false;
    chSysUnlock();

    oled_flush_frame(framebuffers[tx_index]);
  }

  chThdExit(MSG_OK);
}

void drv_display_start(void) {
  if (display_tp != NULL) {
    if (chThdTerminatedX(display_tp)) {
      chThdWait(display_tp);
      display_tp = NULL;
    } else {
      return;
    }
  }

  drv_display_init();

  display_tp = chThdCreateStatic(waDisplay, sizeof(waDisplay),
                                 LOWPRIO, displayThread, NULL);

  /* Pousse immédiatement le contenu courant (écran clear) sans bloquer l’UI. */
  drv_display_update();
}

void drv_display_stop(void) {
  if (display_tp == NULL) {
    return;
  }

  chThdTerminate(display_tp);
  chBSemSignal(&frame_sem); /* Réveille le thread si bloqué. */
  chThdWait(display_tp);
  display_tp = NULL;
}
