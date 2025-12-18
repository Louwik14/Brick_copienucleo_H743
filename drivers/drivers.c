/**
 * @file drivers.c
 * @brief Point d’entrée unique pour l’initialisation de tous les drivers matériels du firmware.
 * @ingroup drivers
 */

#include "drivers.h"
#include "spi5_bus.h"

void drivers_init_all(void) {
    spi5_bus_init();

    /* Thread propriétaire de l’écran (rafraîchissement atomique SPI5). */
    drv_display_start();

    /* Contrat de démarrage :
       - drv_display   : init + start (thread de rafraîchissement)
       - drv_leds_addr : init uniquement (rendu déclenché côté UI)
       - drv_buttons   : start = init + thread de scan
       - drv_encoders  : start = init + thread de scan
       - drv_pots      : start = init + thread de scan */

}

/* Mise à jour périodique : surtout pour l’écran (les LEDs sont rendues via ui_led_backend_refresh). */
void drivers_update_all(void) {
    /* ⚠️ Ne **pas** appeler drv_leds_addr_update() ici.
       Le pipeline LED passe par ui_led_backend_refresh() → drv_leds_addr_render().

       L’écran est rafraîchi par le thread lancé dans drv_display_start(),
       aucun appel direct à drv_display_update() n’est nécessaire ici. */
}
