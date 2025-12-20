#include "drv_display.h"
#include "spi5_bus.h"
#include "ch.h"
#include "hal.h"
#include "brick_config.h"
#include <string.h>

/* ====================================================================== */
/*                        CONFIGURATION MATÉRIELLE                        */
/* ====================================================================== */

/* === FORCE SPI5 PINS (CRITIQUE) === */
static void force_spi5_pins(void) {
    /* SPI5 = PF7 (SCK), PF8 (MISO), PF9 (MOSI), AF5 */
    palSetPadMode(GPIOF, 7U, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
    palSetPadMode(GPIOF, 8U, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
    palSetPadMode(GPIOF, 9U, PAL_MODE_ALTERNATE(5) | PAL_STM32_OSPEED_HIGHEST);
}

/* SPI5 – MODE 0, 8 bits */
static const SPIConfig spicfg = {
    .circular = false,
    .slave    = false,
    .data_cb  = NULL,
    .error_cb = NULL,
    .cfg1     = SPI_CFG1_MBR_2 | SPI_CFG1_DSIZE_VALUE(7),
    .cfg2     = SPI_CFG2_MASTER |
                SPI_CFG2_SSM
};


/* ====================================================================== */
/*                             VARIABLES INTERNES                         */
/* ====================================================================== */

static uint8_t buffer[BRICK_OLED_WIDTH * BRICK_OLED_HEIGHT / 8];
static const font_t *current_font = NULL;
static thread_t *display_tp = NULL;

/* ====================================================================== */
/*                         SPI TIMING BARRIER (H7)                        */
/* ====================================================================== */

/* Attendre que le périphérique SPI ait réellement fini de shifter avant CS high. */
static inline void spi5_wait_idle(void) {
#if defined(SPI_SR_BSY)
    while ((SPID5.spi->SR & SPI_SR_BSY) != 0U) {
        /* wait */
    }
#else
    /* Fallback si le nom du bit diffère selon la lib : on ne fait rien. */
    (void)SPID5;
#endif
    __DSB();
}

/* ====================================================================== */
/*                              SPI LOW LEVEL                             */
/* ====================================================================== */

static inline void oled_select_cmd(void) {
    palClearLine(LINE_SPI5_DC_OLED);   /* CMD */
    palClearLine(LINE_SPI5_CS_OLED);   /* CS low */
}

static inline void oled_select_data(void) {
    palSetLine(LINE_SPI5_DC_OLED);     /* DATA */
    palClearLine(LINE_SPI5_CS_OLED);   /* CS low */
}

static inline void oled_unselect(void) {
    spi5_wait_idle();                 /* barrière critique H7 */
    palSetLine(LINE_SPI5_CS_OLED);    /* CS high */
}

static void send_cmd(uint8_t c) {
    chMtxLock(&spi5_mutex);

    /* CMD */
    palClearLine(LINE_SPI5_DC_OLED);

    palClearLine(LINE_SPI5_CS_OLED);
    spiPolledExchange(&SPID5, c);
    palSetLine(LINE_SPI5_CS_OLED);

    chMtxUnlock(&spi5_mutex);
}

static void send_data(const uint8_t *data, size_t len) {
    chMtxLock(&spi5_mutex);

    /* DATA */
    palSetLine(LINE_SPI5_DC_OLED);

    for (size_t i = 0; i < len; i++) {
        palClearLine(LINE_SPI5_CS_OLED);
        spiPolledExchange(&SPID5, data[i]);
        palSetLine(LINE_SPI5_CS_OLED);
    }

    chMtxUnlock(&spi5_mutex);
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
    if (x < 0 || x >= BRICK_OLED_WIDTH || y < 0 || y >= BRICK_OLED_HEIGHT)
        return;

    uint16_t index = x + (y >> 3) * BRICK_OLED_WIDTH;
    uint8_t  mask  = 1U << (y & 7);

    if (on) buffer[index] |= mask;
    else    buffer[index] &= (uint8_t)~mask;
}

/* ====================================================================== */
/*                      INITIALISATION OLED                               */
/* ====================================================================== */

void drv_display_init(void) {

    spi5_bus_init();
    force_spi5_pins();

    /* GPIO OLED */
    palSetLineMode(LINE_SPI5_CS_OLED,  PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(LINE_SPI5_DC_OLED,  PAL_MODE_OUTPUT_PUSHPULL);
    palSetLineMode(LINE_SPI5_RES_OLED, PAL_MODE_OUTPUT_PUSHPULL);

    palSetLine(LINE_SPI5_CS_OLED);   /* CS idle high */
    palSetLine(LINE_SPI5_DC_OLED);   /* DATA default */

    /* SPI START : UNE SEULE FOIS */
    spiStart(&SPID5, &spicfg);

    /* Reset OLED */
    palClearLine(LINE_SPI5_RES_OLED);
    chThdSleepMilliseconds(50);
    palSetLine(LINE_SPI5_RES_OLED);
    chThdSleepMilliseconds(50);

    /* Init SSD1309 */
    send_cmd(0xAE);
    send_cmd(0xD5); send_cmd(0x80);
    send_cmd(0xA8); send_cmd(0x3F);
    send_cmd(0xD3); send_cmd(0x00);
    send_cmd(0x40);
    send_cmd(0x8D); send_cmd(0x14);
    send_cmd(0x20); send_cmd(0x00);
    send_cmd(0xA1);
    send_cmd(0xC8);
    send_cmd(0xDA); send_cmd(0x12);
    send_cmd(0xAF);

    drv_display_clear();

    extern const font_t FONT_5X7;
    current_font = &FONT_5X7;
}

/* ====================================================================== */
/*                               CLEAR                                    */
/* ====================================================================== */

void drv_display_clear(void) {
    memset(buffer, 0x00, sizeof(buffer));
}

/* ====================================================================== */
/*                               UPDATE                                   */
/* ====================================================================== */

void drv_display_update(void) {
    for (uint8_t page = 0; page < 8; page++) {
        send_cmd(0xB0 + page);
        send_cmd(0x00);
        send_cmd(0x10);
        send_data(&buffer[page * BRICK_OLED_WIDTH], BRICK_OLED_WIDTH);
    }
}

/* ====================================================================== */
/*                              TEXTE                                     */
/* ====================================================================== */

void drv_display_set_font(const font_t *font) {
    current_font = font;
}

void drv_display_draw_char(uint8_t x, uint8_t y, char c) {
    if (!current_font) return;

    if ((uint8_t)c < current_font->first || (uint8_t)c > current_font->last)
        c = '?';

    for (uint8_t col = 0; col < current_font->width; col++) {
        uint8_t bits = current_font->get_col(c, col);
        for (uint8_t row = 0; row < current_font->height; row++) {
            if (bits & (1U << row))
                set_pixel(x + col, y + row, true);
        }
    }
}

void drv_display_draw_text(uint8_t x, uint8_t y, const char *txt) {
    if (!current_font || !txt) return;

    uint8_t adv = current_font->width + current_font->spacing;

    while (*txt && x < BRICK_OLED_WIDTH) {
        drv_display_draw_char(x, y, *txt++);
        x += adv;
    }
}

/* ====================================================================== */
/*                         THREAD DE RAFRAÎCHISSEMENT                      */
/* ====================================================================== */

static THD_WORKING_AREA(waDisplay, 512);

static THD_FUNCTION(displayThread, arg) {
    (void)arg;
    chRegSetThreadName("Display");

    while (!chThdShouldTerminateX()) {
        drv_display_update();
        chThdSleepMilliseconds(33);
    }
}

void drv_display_start(void) {
    if (display_tp != NULL)
        return;

    drv_display_init();
    drv_display_clear();
    drv_display_update();

    display_tp = chThdCreateStatic(
        waDisplay,
        sizeof(waDisplay),
        NORMALPRIO,
        displayThread,
        NULL
    );
}

void drv_display_stop(void) {
    if (!display_tp)
        return;

    chThdTerminate(display_tp);
    chThdWait(display_tp);
    display_tp = NULL;
}
