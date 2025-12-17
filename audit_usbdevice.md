# Audit USB Device (STM32H743 + ChibiOS) — périphérique USB MIDI

## 1. Résumé exécutif
La cause la plus probable de l’échec d’énumération est une configuration matérielle USB incohérente :
- La pile est compilée pour l’instance **OTG1** uniquement, alors que le connecteur FS des cartes H743 Nucleo est généralement câblé sur l’instance **OTG2/FS** ; si c’est le cas sur la carte cible, le contrôleur écouté n’est pas celui raccordé au port physique, ce qui provoque un « device not recognized » dès la phase de reset.【F:cfg/mcuconf.h†L475-L484】
- La désactivation de la détection VBUS (indispensable si PA9 n’est pas câblé) est optionnelle et **désactivée par défaut** ; sur une carte sans VBUS sensing, le contrôleur reste hors-ligne et n’annonce jamais son pull‑up DP.【F:drivers/usb/usb_device.c†L19-L75】

## 2. Points conformes
- Séquence de démarrage correcte : `halInit()` puis `chSysInit()`, démarrage MIDI, puis `usb_device_start()` qui réalise un disconnect logiciel, un délai de ré-énumération et le démarrage du driver ChibiOS avant connexion.【F:main.c†L10-L20】【F:drivers/usb/usb_device.c†L48-L75】
- Descripteurs cohérents pour un périphérique Audio/MIDI FS : configuration totale 82 octets, 2 interfaces (AC + MS), EP1 OUT/EP2 IN en Bulk 64 octets, chaînes définies et `bMaxPacketSize0=64`.【F:drivers/usb/usbcfg.c†L75-L138】
- Callbacks ISR courts : EP OUT réarme immédiatement la réception, invalide le D‑Cache, et poste en mailbox ; EP IN ne fait qu’un signal de sémaphore ; le callback de configuration prépare les endpoints puis signale la disponibilité TX.【F:drivers/usb/usbcfg.c†L186-L216】【F:drivers/usb/usbcfg.c†L270-L300】
- Gestion D‑Cache : buffer RX aligné 32 octets et invalidé avant usage ; chemins TX nettoient explicitement le cache avant `usbStartTransmitI()`.【F:drivers/usb/usbcfg.c†L38-L49】【F:drivers/usb/usbcfg.c†L174-L200】【F:drivers/midi/midi.c†L50-L62】【F:drivers/midi/midi.c†L157-L162】
- Signalisation d’état propre : `usb_midi_tx_ready` activé à la configuration et remis à `false` sur reset/unconfigure/suspend, évitant les envois tant que l’USB n’est pas prêt.【F:drivers/usb/usbcfg.c†L270-L300】

## 3. Points suspects / dangereux
- **VBUS sensing non forcé OFF** : `USB_FORCE_NOVBUS` vaut 0 par défaut ; si la broche VBUS (PA9) n’est pas reliée au contrôleur, le périphérique reste inactif et l’hôte ne voit jamais le device. Le commentaire indique que la désactivation est nécessaire sur certaines Nucleo, mais elle n’est pas activée dans la build actuelle.【F:drivers/usb/usb_device.c†L19-L75】
- **Mauvais contrôleur USB sélectionné ?** La configuration compile uniquement OTG1 (HS) et désactive OTG2. Sur H743, le port micro‑USB FS des cartes Nucleo/Discovery est généralement câblé sur OTG2_FS. Si le hardware cible suit ce câblage, le firmware écoute le mauvais périphérique et l’énumération échoue systématiquement.【F:cfg/mcuconf.h†L475-L484】
- **Priorité d’IRQ très basse** : les IRQ OTG1/OTG2 sont au niveau 14 (le plus faible). Sous charge (ADC, TIM, display…), les transactions de reset/setup peuvent être retardées ou perdues, provoquant des « device not recognized » lors des handshakes initiaux.【F:cfg/mcuconf.h†L478-L483】
- **Horloge 48 MHz non vérifiable** : le dépôt ne contient pas la configuration horloge (board.c/hal_lld.c) qui garantit la disponibilité de PLLQ/HSI48 pour l’USB. Toute modification du clock tree pourrait avoir cassé l’USB sans que le code USB n’en fasse la vérification préalable.
- **VBUS/PHY non surveillés** : aucun log/assert n’indique si VBUS est détecté ou si le transceiver est alimenté (`PWRDWN`). En cas de problème matériel (câble, alimentation 5 V), l’application continue sans signaler l’absence de bus, compliquant le diagnostic de terrain.【F:drivers/usb/usb_device.c†L48-L75】

## 4. Hypothèses (par probabilité décroissante)
1) **VBUS sensing actif sur carte sans PA9** ⇒ USBD1 reste en « Not Connected » et ne lance jamais le pull‑up DP ; énumération impossible.【F:drivers/usb/usb_device.c†L19-L75】
2) **Contrôleur OTG1 sélectionné alors que le port physique est OTG2_FS** ⇒ la pile attend sur le mauvais périphérique, aucun évènement de reset n’est reçu par l’instance active.【F:cfg/mcuconf.h†L475-L484】
3) **Horloge USB invalide (PLLQ/HSI48 non verrouillée)** ⇒ PCD démarre mais répond avec des timings erratiques, menant à des échecs de setup ou à un reset permanent (à confirmer par inspection de la config d’horloge runtime).
4) **IRQ USB préemptées (prio 14)** ⇒ sous activité forte, les interruptions SOF/RESET/EP peuvent être servies trop tard, l’hôte abandonne l’énumération.
5) **Transceiver/PHY pas alimenté ou PWRDWN non activé** ⇒ si l’initialisation basse couche échoue silencieusement, aucun signal n’est envoyé à l’hôte ; à vérifier via registre GCCFG/PWRDWN et un log au boot.

## 5. Recommandations concrètes (patchs/tests)
- **Forcer le mode device sans VBUS sensing** : définir `USB_FORCE_NOVBUS=1` (ou ajouter une config de build) et vérifier que `GCCFG.VBDEN` est bien à 0 en sortie de `usb_device_start()`. Tester avec un analyseur USB ou `dmesg` pour confirmer l’apparition du pull‑up DP.【F:drivers/usb/usb_device.c†L19-L75】
- **Vérifier l’instance USB utilisée** : contrôler le schéma de la carte ; si le port micro‑USB est relié à OTG2_FS (PA11/PA12/PA9), passer `STM32_USB_USE_OTG1` à `FALSE` et `STM32_USB_USE_OTG2` à `TRUE`, puis refaire un essai d’énumération. Instrumenter `usb_lld_init()` pour confirmer que les bons GPIO/clock sont activés.【F:cfg/mcuconf.h†L475-L484】
- **Remonter la priorité NVIC USB** : fixer `STM32_USB_OTG1_IRQ_PRIORITY` (et OTG2 le cas échéant) à une valeur plus haute (p.ex. 6–8) pour éviter les timeouts de handshakes, et vérifier qu’elle reste compatible avec ChibiOS et la politique d’accès aux API non I-class.【F:cfg/mcuconf.h†L478-L483】
- **Valider la source 48 MHz** : durant l’amorçage, lire et tracer la fréquence PLL/HSI48 utilisée par l’USB (registre RCC) et ajouter un assert si la source n’est pas verrouillée. Contrôler que `stm32_clock_init()` active bien la sortie PLLQ à 48 MHz ou HSI48 selon le design.
- **Ajouter une télémétrie minimale** : log en boot du statut VBUS/GCCFG, de la configuration des endpoints lors de `USB_EVENT_CONFIGURED`, et des erreurs RX (`usb_midi_rx_invalid_size`, `usb_rx_drops`) pour distinguer un problème d’énumération d’un problème de trafic.【F:drivers/usb/usbcfg.c†L170-L200】
