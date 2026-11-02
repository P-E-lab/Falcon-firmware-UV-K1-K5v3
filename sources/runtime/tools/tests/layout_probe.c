#include "settings.h"
#include <stddef.h>
const unsigned layout[] = {
 offsetof(EEPROM_Config_t, RX_MODE), offsetof(EEPROM_Config_t, VfoInfo), sizeof(VFO_Info_t),
 offsetof(VFO_Info_t, pRX), offsetof(VFO_Info_t, pTX), offsetof(VFO_Info_t, CHANNEL_SAVE),
 offsetof(VFO_Info_t, freq_config_RX), offsetof(VFO_Info_t, freq_config_TX),
 offsetof(EEPROM_Config_t, TX_VFO), offsetof(EEPROM_Config_t, RX_VFO),
 offsetof(EEPROM_Config_t, SCANLIST_PRIORITY_CH)
};
