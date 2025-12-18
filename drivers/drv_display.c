#include "drv_display.h"

#include "spi5_bus.h"
#include "cache_utils.h"
#include "ch.h"
#include "hal.h"
#include "brick_config.h"

#include <string.h>

/* ====================================================================== */
/*                        CONFIGURATION MATÉRIELLE                        */
/* ====================================================================== */

/* SPI5 - Configuration compatible ChibiOS récent */
static const SPIConfig spicfg = {
    .circular = false,
    .slave    = false,
    .data_cb  = NULL,
    .error_cb = NULL,

    /* Fréquence volontairement modérée pour la robustesse. */
    .cfg1 = SPI_CFG1_MBR_4 |               /* Prescaler */
            SPI_CFG1_DSIZE_VALUE(7),      /* 8 bits */

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

/* Buffer en RAM D2 non-cacheable (MPU) pour éviter la corruption DMA (SPI5). */
static DISPLAY_DMA_BUFFER_ATTR uint8_t buffer[BRICK_OLED_WIDTH * BRICK_OLED_HEIGHT / 8];
static const font_t *current_font = NULL;
static thread_t *display_tp = NULL;
static binary_semaphore_t refresh_sem;
static volatile bool framebuffer_dirty = false;
static volatile bool display_thread_running = false;

/*
 * Risques identifiés de corruption OLED :
 *  - préemption en plein transfert SPI5 (CS/DC incohérents),
 *  - DMA + D-Cache sur buffers cacheables.
 * Mitigation : thread propriétaire + spi5_bus_lock sur toute la frame,
 *  envoi dans RAM D2 non‑cacheable + clean D-Cache défensif avant chaque bloc.
 */

/* ====================================================================== */
/*                              UTILITAIRES GPIO                          */
/* ====================================================================== */

static inline void dc_cmd(void)  { palClearLine(LINE_SPI5_DC_OLED); }
static inline void dc_data(void) { palSetLine  (LINE_SPI5_DC_OLED); }

/* ====================================================================== */
/*                              UTILITAIRES SPI                           */
/* ====================================================================== */

static void send_bytes(bool data_mode, const uint8_t *data, size_t len) {
    if (len == 0U || data == NULL) {
        return;
    }

    /* Assure la cohérence DMA si le D-Cache est actif. */
    cache_utils_clean_dcache(data, len);

    if (data_mode) {
        dc_data();
    } else {
        dc_cmd();
    }

    palClearLine(LINE_SPI5_CS_OLED);
    spiSend(&SPID5, len, data);
    palSetLine(LINE_SPI5_CS_OLED);
}

static void send_cmd_list(const uint8_t *cmds, size_t count) {
    send_bytes(false, cmds, count);
}

static void mark_dirty_and_notify(void) {
    framebuffer_dirty = true;
    chBSemSignal(&refresh_sem);
}

/* ====================================================================== */
/*                              FRAMEBUFFER                               */
/* ====================================================================== */

uint8_t* drv_display_get_buffer(void) {
    return buffer;
}

/* ====================================================================== */
/*                              PIXELS                                    */
/* ====================================================================== */

static inline void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= BRICK_OLED_WIDTH || y < 0 || y >= BRICK_OLED_HEIGHT) return;

    const int index = x + (y >> 3) * BRICK_OLED_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 7));

    if (on) buffer[index] |=  mask;
    else    buffer[index] &= (uint8_t)~mask;
}

/* ====================================================================== */
/*                      INITIALISATION OLED                               */
/* ====================================================================== */

void drv_display_init(void) {

    spi5_bus_init();
    chBSemObjectInit(&refresh_sem, true);

    /* Reset OLED */
    palClearLine(LINE_SPI5_RES_OLED);
    chThdSleepMilliseconds(10);
    palSetLine(LINE_SPI5_RES_OLED);
    chThdSleepMilliseconds(10);

    /* Séquence init SSD1309 / SSD1306 : transaction atomique. */
    spi5_bus_lock();
    spi5_bus_start(&spicfg);

    static DISPLAY_DMA_BUFFER_ATTR const uint8_t init_seq[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0xAF
    };

    send_cmd_list(init_seq, sizeof(init_seq));

    spi5_bus_unlock();

    drv_display_clear();
    framebuffer_dirty = true;

    extern const font_t FONT_5X7;
    current_font = &FONT_5X7;
}

/* ====================================================================== */
/*                               CLEAR                                    */
/* ====================================================================== */

void drv_display_clear(void) {
    memset(buffer, 0x00, sizeof(buffer));
    mark_dirty_and_notify();
}

/* ====================================================================== */
/*                               UPDATE                                   */
/* ====================================================================== */

static void drv_display_flush_frame(void) {
    spi5_bus_lock();
    spi5_bus_start(&spicfg);

    for (uint8_t page = 0; page < 8; page++) {
        const uint8_t cmds[] = { (uint8_t)(0xB0 + page), 0x00, 0x10 };
        send_cmd_list(cmds, sizeof(cmds));

        const uint8_t *page_ptr = &buffer[page * BRICK_OLED_WIDTH];
        send_bytes(true, page_ptr, BRICK_OLED_WIDTH);
    }

    spi5_bus_unlock();
}

void drv_display_update(void) {
    if (display_thread_running) {
        mark_dirty_and_notify();
        return;
    }

    framebuffer_dirty = false;
    drv_display_flush_frame();
}

void drv_display_request_refresh(void) {
    mark_dirty_and_notify();
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

static void draw_char_pixels(uint8_t x, uint8_t y, char c, const font_t *font) {
    if (!font) return;

    if ((uint8_t)c < font->first || (uint8_t)c > font->last)
        c = '?';

    for (uint8_t col = 0; col < font->width; col++) {
        uint8_t bits = font->get_col(c, col);
        for (uint8_t row = 0; row < font->height; row++) {
            if (bits & (1U << row))
                set_pixel(x + col, y + row, true);
        }
    }
}

void drv_display_draw_char(uint8_t x, uint8_t y, char c) {

    if (!current_font) return;

    draw_char_pixels(x, y, c, current_font);
    mark_dirty_and_notify();
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt) {

    if (!current_font || !txt) return;

    const uint8_t adv = font_advance(current_font);

    while (*txt && x < BRICK_OLED_WIDTH) {
        draw_char_pixels(x, y, *txt++, current_font);
        x = (uint8_t)(x + adv);
    }

    mark_dirty_and_notify();
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

void drv_display_draw_char_in_box(const font_t *font,
                                  uint8_t x, uint8_t y,
                                  uint8_t box_w, uint8_t box_h,
                                  char c) {

    if (!font) return;

    const font_t *save = current_font;
    current_font = font;

    const uint8_t start_x = (box_w > font->width) ? (uint8_t)(x + (box_w - font->width) / 2U) : x;
    const uint8_t start_y = (box_h > font->height) ? (uint8_t)(y + (box_h - font->height) / 2U) : y;

    draw_char_pixels(start_x, start_y, c, font);

    current_font = save;
    mark_dirty_and_notify();
}

/* ====================================================================== */
/*                         THREAD DE RAFRAÎCHISSEMENT                      */
/* ====================================================================== */

static THD_WORKING_AREA(waDisplay, 512);

static THD_FUNCTION(displayThread, arg) {
    (void)arg;
    chRegSetThreadName("Display");

    display_thread_running = true;

    while (!chThdShouldTerminateX()) {
        chBSemWaitTimeout(&refresh_sem, TIME_MS2I(33));

        if (framebuffer_dirty) {
            framebuffer_dirty = false;
            drv_display_flush_frame();
        }
    }

    display_thread_running = false;
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
                                   NORMALPRIO, displayThread, NULL);
}

void drv_display_stop(void) {
    if (display_tp == NULL)
        return;

    chThdTerminate(display_tp);
    chThdWait(display_tp);
    display_tp = NULL;
}
