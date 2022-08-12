#include "esp_rdm.h"

#include <string.h>

#include "dmx_caps.h"
#include "endian.h"
#include "esp_system.h"

#define DMX_DEFAULT_MANUFACTURER_ID (0xbeef)  // TODO: Use real manufactuer ID

static rdm_uid_t rdm_uid = {};  // The 48-bit unique ID of this device.

rdm_uid_t rdm_get_uid() { 
  // Initialize the RDM UID
  if (rdm_uid.raw == 0) {
    uint8_t mac[8];
    esp_efuse_mac_get_default(mac);
    rdm_uid.manufacturer_id = DMX_DEFAULT_MANUFACTURER_ID;
    rdm_uid.device_id = bswap32(*(uint32_t *)(mac + 2));  // Don't use MAC OUI
  }

  return rdm_uid;
}

void rdm_set_uid(rdm_uid_t uid) { 
  rdm_uid = uid;
}

void *rdm_parse(void *data, size_t size, dmx_event_t *event) {

  const rdm_data_t *const rdm = (rdm_data_t *)data;
  void *parameter_data = NULL;
  
  if (rdm->sc == RDM_PREAMBLE || rdm->sc == RDM_DELIMITER) {
    // Find the length of the discovery response preamble (0-7 bytes)
    int preamble_len = 0;
    const uint8_t *response = data;
    for (; preamble_len < 7; ++preamble_len) {
      if (response[preamble_len] == RDM_DELIMITER) {
        break;
      }
    }
    if (response[preamble_len] != RDM_DELIMITER) {
      return NULL;  // Not a valid discovery response
    }

    // Decode the 6-byte UID and get the packet sum
    uint64_t uid = 0;
    uint16_t sum = 0;
    response = &data[preamble_len + 1];
    for (int i = 5, j = 0; i >= 0; --i, j += 2) {
      ((uint8_t *)&uid)[i] = response[j] & 0x55;
      ((uint8_t *)&uid)[i] |= response[j + 1] & 0xaa;
      sum += ((uint8_t *)&uid)[i] + 0xff;
    }

    // Decode the checksum received in the response
    uint16_t checksum;
    for (int i = 1, j = 12; i >= 0; --i, j += 2) {
      ((uint8_t *)&checksum)[i] = response[j] & 0x55;
      ((uint8_t *)&checksum)[i] |= response[j + 1] & 0xaa;
    }
  } else if (rdm->sc == RDM_SC && rdm->sub_sc == RDM_SUB_SC) {
    // TODO:
  }


  return parameter_data;
}