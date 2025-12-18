#ifndef DRV_DISPLAY_H
#define DRV_DISPLAY_H

#include "ch.h"
#include "hal.h"
#include "brick_config.h"
#include <stdint.h>
#include "font.h"

/* Taille de l'écran issue de brick_config */
#define OLED_WIDTH   BRICK_OLED_WIDTH
#define OLED_HEIGHT  BRICK_OLED_HEIGHT

/* ====================================================================== */
/*                              API PUBLIQUE                              */
/* ====================================================================== */

void drv_display_init(void);
void drv_display_clear(void);
void drv_display_update(void);
void drv_display_request_refresh(void);
uint8_t* drv_display_get_buffer(void);
void drv_display_start(void);
void drv_display_stop(void);

/* ====================================================================== */
/*                           GESTION DES POLICES                          */
/* ====================================================================== */

void drv_display_set_font(const font_t *font);

/* ====================================================================== */
/*                           DESSIN ET TEXTE                              */
/* ====================================================================== */

void drv_display_draw_char(uint8_t x, uint8_t y, char c);
void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt);

void drv_display_draw_text_with_font(const font_t *font,
                                     uint8_t x, uint8_t y,
                                     const char *txt);

void drv_display_draw_text_at_baseline(const font_t *font,
                                       uint8_t x, uint8_t baseline_y,
                                       const char *txt);

void drv_display_draw_char_in_box(const font_t *font,
                                  uint8_t x, uint8_t y,
                                  uint8_t box_w, uint8_t box_h,
                                  char c);

/*
 * Guide rapide (multi-thread temps réel) :
 *  - drivers_init_all() démarre le thread propriétaire OLED (SPI5 locké frame entière).
 *  - Thread audio (haute prio) : traite audio, ne touche jamais SPI5.
 *  - Thread UI : écrit dans le framebuffer via drv_display_draw_* ou directement
 *    sur drv_display_get_buffer(), puis appelle drv_display_request_refresh().
 *  - Thread hall : lit les capteurs, envoie MIDI/LEDs ; l’écran reste stable car
 *    seul le thread display manipule SPI5 + DC/CS.
 */

#endif /* DRV_DISPLAY_H */
