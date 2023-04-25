#include "esp_rdm.h"

#include <stdint.h>
#include <string.h>

#include "dmx_types.h"
#include "endian.h"
#include "esp_check.h"
#include "esp_dmx.h"
#include "esp_log.h"
#include "esp_system.h"
#include "private/driver.h"
#include "private/rdm_encode/functions.h"
#include "private/rdm_encode/types.h"

static const char *TAG = "rdm";  // The log tagline for the file.

size_t rdm_send(dmx_port_t dmx_num, rdm_header_t *header,
                const rdm_encode_t *encode, rdm_decode_t *decode,
                rdm_ack_t *ack) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(header != NULL, 0, "header is null");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  // Validate required header information
  if (header->dest_uid == 0 || (header->dest_uid > RDM_MAX_UID &&
                                !rdm_uid_is_broadcast(header->dest_uid))) {
    ESP_LOGE(TAG, "dest_uid is invalid");
    return 0;
  }
  if (header->cc != RDM_CC_DISC_COMMAND && header->cc != RDM_CC_GET_COMMAND &&
      header->cc != RDM_CC_SET_COMMAND) {
    ESP_LOGE(TAG, "cc is invalid");
    return 0;
  }
  if (header->pid == 0 || header->pid > 0xffff) {
    ESP_LOGE(TAG, "pid is invalid");
    return 0;
  }
  if (header->sub_device > 512 && header->sub_device != RDM_ALL_SUB_DEVICES) {
    ESP_LOGE(TAG, "sub_device is invalid");
    return 0;
  } else if (header->sub_device == RDM_ALL_SUB_DEVICES &&
             header->cc == RDM_CC_GET_COMMAND) {
    ESP_LOGE(TAG, "cannot send RDM_CC_GET_COMMAND to RDM_ALL_SUB_DEVICES");
    return 0;
  }

  // Validate header values that the user doesn't need to include
  if (header->src_uid > RDM_MAX_UID || rdm_uid_is_broadcast(header->src_uid)) {
    ESP_LOGE(TAG, "src_uid is invalid");
    return 0;
  } else if (header->src_uid == 0) {
    header->src_uid = rdm_get_uid(dmx_num);
  }
  if (header->port_id < 0 || header->port_id > 255) {
    ESP_LOGE(TAG, "port_id is invalid");
    return 0;
  } else if (header->port_id == 0) {
    header->port_id = dmx_num + 1;
  }

  // Set header values that the user cannot set themselves
  header->message_count = 0;
  header->tn = 0;  // TODO

  // Encode parameter data
  rdm_mdb_t mdb;
  if (encode && encode->function && encode->params && encode->num) {
    encode->function(&mdb, encode->params, encode->num);
  } else {
    mdb.pdl = 0;
  }

  // Take mutex so driver values may be accessed
  dmx_driver_t *const driver = dmx_driver[dmx_num];
  xSemaphoreTakeRecursive(driver->mux, portMAX_DELAY);
  dmx_wait_sent(dmx_num, portMAX_DELAY);

  // Send the request and await the response
  size_t packet_size = rdm_write(dmx_num, header, &mdb);
  dmx_send(dmx_num, packet_size);
  dmx_packet_t packet = {};
  if (!rdm_uid_is_broadcast(header->dest_uid) ||
      (header->pid == RDM_PID_DISC_UNIQUE_BRANCH &&
       header->cc == RDM_CC_DISC_COMMAND)) {
    packet_size = dmx_receive(dmx_num, &packet, 2);
  }

  // Return early if an error occurred
  if (packet.err) {
    if (ack != NULL) {
      ack->err = packet.err;
      ack->type = RDM_RESPONSE_TYPE_NONE;
      ack->num = 0;
    }
    return packet_size;
  }

  // Process the response data
  if (packet.size > 0) {
    esp_err_t err;
    const rdm_header_t req = *header;
    if (packet.err) {
      err = packet.err;  // Error pass-through
    } else if (!packet.is_rdm) {
      err = ESP_ERR_INVALID_RESPONSE;  // Packet is not RDM
    } else if (!rdm_read(dmx_num, header, &mdb)) {
      err = ESP_ERR_INVALID_RESPONSE;  // Packet is invalid
    } else if (header->response_type != RDM_RESPONSE_TYPE_ACK &&
               header->response_type != RDM_RESPONSE_TYPE_ACK_TIMER &&
               header->response_type != RDM_RESPONSE_TYPE_NACK_REASON &&
               header->response_type != RDM_RESPONSE_TYPE_ACK_OVERFLOW) {
      err = ESP_ERR_INVALID_RESPONSE;  // Response type is invalid
    } else if (!(req.cc == RDM_CC_DISC_COMMAND &&
                 req.pid == RDM_PID_DISC_UNIQUE_BRANCH) &&
               (req.cc != (header->cc & 0x1) || req.pid != header->pid ||
                req.sub_device != header->sub_device || req.tn != header->tn ||
                req.dest_uid != header->src_uid ||
                req.src_uid != header->src_uid)) {
      err = ESP_ERR_INVALID_RESPONSE;  // Response is invalid
    } else {
      err = ESP_OK;
    }

    uint32_t decoded = 0;
    rdm_response_type_t response_type;
    if (!err) {
      response_type = header->response_type;
      if (response_type == RDM_RESPONSE_TYPE_ACK) {
        // Decode the parameter data if requested
        if (mdb.pdl > 0) {
          if (decode && decode->function && decode->params &&
              decode->num) {
            decoded = decode->function(&mdb, decode->params, decode->num);
          } else {
            ESP_LOGW(TAG, "received parameter data but decoder is null");
          }
        }
      } else if (response_type == RDM_RESPONSE_TYPE_ACK_TIMER) {
        // Get the estimated response time and convert it to FreeRTOS ticks
        rdm_decode_16bit(&mdb, &decoded, 1);
        decoded = pdMS_TO_TICKS(decoded * 10);
      } else if (response_type == RDM_RESPONSE_TYPE_NACK_REASON) {
        // Get the reported NACK reason
        rdm_decode_16bit(&mdb, &decoded, 1);
      } else {
        // Received RDM_RESPONSE_TYPE_ACK_OVERFLOW
        err = ESP_ERR_NOT_SUPPORTED;  // TODO: implement overflow support
      }
    } else {
      response_type = RDM_RESPONSE_TYPE_NONE;  // Report no response on errors
    }

    // Report the ACK back to the user
    if (ack != NULL) {
      ack->err = err;
      ack->type = response_type;
      ack->num = decoded;
    }

  } else {
    // Wait for request to finish sending if no response is expected
    if (ack != NULL) {
      ack->err = ESP_OK;
      ack->type = RDM_RESPONSE_TYPE_NONE;
      ack->num = 0;
    }
    dmx_wait_sent(dmx_num, 2);
  }

  xSemaphoreGiveRecursive(driver->mux);
  return packet_size;
}

size_t rdm_send_disc_unique_branch(dmx_port_t dmx_num, rdm_header_t *header,
                                   const rdm_disc_unique_branch_t *param,
                                   rdm_ack_t *ack) {
  // TODO: check args

  header->dest_uid = RDM_BROADCAST_ALL_UID;
  header->sub_device = RDM_ROOT_DEVICE;
  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_UNIQUE_BRANCH;
  header->src_uid = rdm_get_uid(dmx_num);
  header->port_id = dmx_num + 1;
  
  const rdm_encode_t encode = {
    .function = rdm_encode_uids,
    .params = param,
    .num = 2
  };
  
  return rdm_send(dmx_num, header, &encode, NULL, ack);
}

size_t rdm_send_disc_mute(dmx_port_t dmx_num, rdm_header_t *header,
                          rdm_ack_t *ack, rdm_disc_mute_t *param) {
  // TODO: check args

  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_MUTE;
  header->src_uid = rdm_get_uid(dmx_num);
  header->port_id = dmx_num + 1;
  
  rdm_decode_t decode = {
    .function = rdm_decode_mute,
    .params = param,
    .num = 1,
  };
  
  return rdm_send(dmx_num, header, NULL, &decode, ack);
}

size_t rdm_send_disc_un_mute(dmx_port_t dmx_num, rdm_header_t *header,
                             rdm_ack_t *ack, rdm_disc_mute_t *param) {
  // TODO: check args

  header->cc = RDM_CC_DISC_COMMAND;
  header->pid = RDM_PID_DISC_UN_MUTE;
  header->src_uid = rdm_get_uid(dmx_num);
  header->port_id = dmx_num + 1;
  
  rdm_decode_t decode = {
    .function = rdm_decode_mute,
    .params = param,
    .num = 1,
  };
  
  return rdm_send(dmx_num, header, NULL, &decode, ack);
}

/*
size_t rdm_discover_with_callback(dmx_port_t dmx_num, rdm_discovery_cb_t cb,
                                  void *context) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  // Allocate the instruction stack. The max binary tree depth is 49
#ifndef CONFIG_RDM_STATIC_DEVICE_DISCOVERY
  rdm_disc_unique_branch_t *stack;
  stack = malloc(sizeof(rdm_disc_unique_branch_t) * 49);
  if (stack == NULL) {
    ESP_LOGE(TAG, "Discovery malloc error");
    return 0;
  }
#else
  rdm_disc_unique_branch_t stack[49];  // 784B - use with caution!
#endif

  dmx_driver_t *restrict const driver = dmx_driver[dmx_num];
  xSemaphoreTakeRecursive(driver->mux, portMAX_DELAY);

  // Un-mute all devices
  rdm_send_disc_mute(dmx_num, RDM_BROADCAST_ALL_UID, false, NULL, NULL);

  // Initialize the stack with the initial branch instruction
  size_t stack_size = 1;
  stack[0].lower_bound = 0;
  stack[0].upper_bound = RDM_MAX_UID;

  rdm_disc_mute_t mute;     // Mute parameters returned from devices.
  rdm_response_t response;  // Request response information.
  bool dev_muted;           // Is true if the responding device was muted.
  rdm_uid_t uid;            // The UID of the responding device.

  size_t num_found = 0;
  while (stack_size > 0) {
    rdm_disc_unique_branch_t *branch = &stack[--stack_size];
    size_t attempts = 0;

    if (branch->lower_bound == branch->upper_bound) {
      // Can't branch further so attempt to mute the device
      uid = branch->lower_bound;
      do {
        dev_muted = rdm_send_disc_mute(dmx_num, uid, true, &response, &mute);
      } while (!dev_muted && ++attempts < 3);

      // Attempt to fix possible error where responder is flipping its own UID
      if (!dev_muted) {
        uid = bswap64(uid) >> 16;  // Flip UID
        dev_muted = rdm_send_disc_mute(dmx_num, uid, true, NULL, &mute);
      }

      // Call the callback function and report a device has been found
      if (dev_muted && !response.err) {
        cb(dmx_num, uid, num_found, &mute, context);
        ++num_found;
      }
    } else {
      // Search the current branch in the RDM address space
      do {
        uid = rdm_send_disc_unique_branch(dmx_num, branch, &response);
      } while (uid == 0 && ++attempts < 3);
      if (uid != 0) {
        bool devices_remaining = true;

#ifndef CONFIG_RDM_DEBUG_DEVICE_DISCOVERY
        
        Stop the RDM controller from branching all the way down to the
        individual address if it is not necessary. When debugging, this code
        should not be called as it can hide bugs in the discovery algorithm.
        Users can use the sdkconfig to enable or disable discovery debugging.
        
        if (!response.err) {
          for (int quick_finds = 0; quick_finds < 3; ++quick_finds) {
            // Attempt to mute the device
            attempts = 0;
            do {
              dev_muted = rdm_send_disc_mute(dmx_num, uid, true, NULL, &mute);
            } while (!dev_muted && ++attempts < 3);

            // Call the callback function and report a device has been found
            if (dev_muted) {
              cb(dmx_num, uid, num_found, &mute, context);
              ++num_found;
            }

            // Check if there are more devices in this branch
            attempts = 0;
            do {
              uid = rdm_send_disc_unique_branch(dmx_num, branch, &response);
            } while (uid == 0 && ++attempts < 3);
            if (uid != 0 && response.err) {
              // There are more devices in this branch - branch further
              devices_remaining = true;
              break;
            } else {
              // There are no more devices in this branch
              devices_remaining = false;
              break;
            }
          }
        }
#endif

        // Recursively search the next two RDM address spaces
        if (devices_remaining) {
          const rdm_uid_t lower_bound = branch->lower_bound;
          const rdm_uid_t mid = (lower_bound + branch->upper_bound) / 2;

          // Add the upper branch so that it gets handled second
          stack[stack_size].lower_bound = mid + 1;
          ++stack_size;

          // Add the lower branch so it gets handled first
          stack[stack_size].lower_bound = lower_bound;
          stack[stack_size].upper_bound = mid;
          ++stack_size;
        }
      }
    }
  }

  xSemaphoreGiveRecursive(driver->mux);

#ifndef CONFIG_RDM_STATIC_DEVICE_DISCOVERY
  free(stack);
#endif

  return num_found;
}

struct rdm_disc_default_ctx {
  size_t size;
  rdm_uid_t *uids;
};

static void rdm_disc_cb(dmx_port_t dmx_num, rdm_uid_t uid, size_t num_found,
                        rdm_disc_mute_t *mute, void *context) {
  struct rdm_disc_default_ctx *c = (struct rdm_disc_default_ctx *)context;
  if (num_found < c->size && c->uids != NULL) {
    c->uids[num_found] = uid;
  }
}

size_t rdm_discover_devices_simple(dmx_port_t dmx_num, rdm_uid_t *uids,
                                   const size_t size) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  struct rdm_disc_default_ctx context = {.size = size, .uids = uids};
  size_t found = rdm_discover_with_callback(dmx_num, &rdm_disc_cb, &context);

  return found;
}

static size_t rdm_send_generic_request(
    dmx_port_t dmx_num, rdm_uid_t uid, rdm_sub_device_t sub_device,
    const rdm_cc_t cc, const rdm_pid_t pid,
    size_t (*encode)(void *, const void *, int), void *encode_params,
    size_t num_encode_params,
    int (*decode)(const void *, void *, int), void *decode_params,
    size_t num_decode_params, rdm_response_t *response) {
  // Take mutex so driver values may be accessed
  dmx_driver_t *const driver = dmx_driver[dmx_num];
  xSemaphoreTakeRecursive(driver->mux, portMAX_DELAY);
  dmx_wait_sent(dmx_num, portMAX_DELAY);

  // Encode and send the initial RDM request
  const uint8_t tn = driver->rdm.tn;
  rdm_data_t *const rdm = (rdm_data_t *)driver->data.buffer;
  size_t written;
  if (encode && encode_params && num_encode_params) {
    written = encode(&rdm->pd, encode_params, num_encode_params);
  } else {
    written = 0;
  }
  rdm_header_t req_header = {.destination_uid = uid,
                             .source_uid = rdm_get_uid(dmx_num),
                             .tn = tn,
                             .port_id = dmx_num + 1,
                             .message_count = 0,
                             .sub_device = sub_device,
                             .cc = cc,
                             .pid = pid,
                             .pdl = written};
  written += rdm_encode_header(rdm, &req_header);
  dmx_send(dmx_num, written);

  // Receive and decode the RDM response
  uint32_t return_val = 0;
  if (!rdm_uid_is_broadcast(uid)) {
    dmx_packet_t event;
    const size_t read = dmx_receive(dmx_num, &event, pdMS_TO_TICKS(20));
    if (!read) {
      if (response != NULL) {
        response->err = event.err;
        response->type = RDM_RESPONSE_TYPE_NONE;
        response->num_params = 0;
      }
    } else {
      // Parse the response to ensure it is valid
      esp_err_t err;
      rdm_header_t resp_header;
      if (!rdm_decode_header(driver->data.buffer, &resp_header)) {
        err = ESP_ERR_INVALID_RESPONSE;
      } else if (!resp_header.checksum_is_valid) {
        err = ESP_ERR_INVALID_CRC;
      } else if (resp_header.cc != req_header.cc + 1 ||
                 resp_header.pid != req_header.pid ||
                 resp_header.destination_uid != req_header.source_uid ||
                 resp_header.source_uid != req_header.destination_uid ||
                 resp_header.sub_device != req_header.sub_device ||
                 resp_header.tn != req_header.tn) {
        err = ESP_ERR_INVALID_RESPONSE;
      } else {
        err = ESP_OK;

        // Handle the parameter data
        uint32_t response_val;
        if (resp_header.response_type == RDM_RESPONSE_TYPE_ACK) {
          // Decode the parameter data
          if (decode) {
            // Return the number of params available when response is received
            return_val = decode(&rdm->pd, decode_params, num_decode_params);
            response_val = return_val;
          } else {
            // Return true when no response parameters are expected
            return_val = true;
            response_val = 0;
          }
        } else if (resp_header.response_type == RDM_RESPONSE_TYPE_ACK_TIMER) {
          // Get the estimated response time and convert it to FreeRTOS ticks
          rdm_decode_16bit(&rdm->pd, &response_val, 1);
          response_val = pdMS_TO_TICKS(response_val * 10);
        } else if (resp_header.response_type == RDM_RESPONSE_TYPE_NACK_REASON) {
          // Report the NACK reason
          rdm_decode_16bit(&rdm->pd, &response_val, 1);
        } else if (resp_header.response_type ==
                   RDM_RESPONSE_TYPE_ACK_OVERFLOW) {
          // TODO: implement overflow support
          err = ESP_ERR_NOT_SUPPORTED;
          response_val = 0;
        } else {
          // An unknown response type was received
          err = ESP_ERR_INVALID_RESPONSE;
          response_val = 0;
        }

        // Report response back to user
        if (response != NULL) {
          response->err = err;
          response->type = resp_header.response_type;
          response->num_params = response_val;
        }
      }
    }
  } else {
    if (response != NULL) {
      response->err = ESP_OK;
      response->type = RDM_RESPONSE_TYPE_NONE;
      response->num_params = 0;
    }
    dmx_wait_sent(dmx_num, pdMS_TO_TICKS(20));
  }

  xSemaphoreGiveRecursive(driver->mux);
  return return_val;
}

size_t rdm_get_supported_parameters(dmx_port_t dmx_num, rdm_uid_t uid,
                                    rdm_sub_device_t sub_device,
                                    rdm_response_t *response, rdm_pid_t *pids,
                                    size_t size) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID, 0, "uid error");
  DMX_CHECK(sub_device != RDM_ALL_SUB_DEVICES, 0, "sub_device error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_GET_COMMAND,
                                  RDM_PID_SUPPORTED_PARAMETERS, NULL, NULL, 0,
                                  rdm_decode_16bit, pids, size, response);
}

size_t rdm_get_device_info(dmx_port_t dmx_num, rdm_uid_t uid,
                           rdm_sub_device_t sub_device,
                           rdm_response_t *response,
                           rdm_device_info_t *device_info) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID, 0, "uid error");
  DMX_CHECK(sub_device != RDM_ALL_SUB_DEVICES, 0, "sub_device error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  return rdm_send_generic_request(
      dmx_num, uid, sub_device, RDM_CC_GET_COMMAND, RDM_PID_DEVICE_INFO, NULL,
      NULL, 0, rdm_decode_device_info, device_info, 1, response);
}

size_t rdm_get_software_version_label(dmx_port_t dmx_num, rdm_uid_t uid,
                                      rdm_sub_device_t sub_device,
                                      rdm_response_t *response, char *label,
                                      size_t size) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID, 0, "uid error");
  DMX_CHECK(sub_device != RDM_ALL_SUB_DEVICES, 0, "sub_device error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_GET_COMMAND,
                                  RDM_PID_SOFTWARE_VERSION_LABEL, NULL, NULL, 0,
                                  rdm_decode_string, label, size, response);
}

size_t rdm_get_dmx_start_address(dmx_port_t dmx_num, rdm_uid_t uid,
                                 rdm_sub_device_t sub_device,
                                 rdm_response_t *response, int *start_address) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID, 0, "uid error");
  DMX_CHECK(sub_device != RDM_ALL_SUB_DEVICES, 0, "sub_device error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_GET_COMMAND,
                                  RDM_PID_DMX_START_ADDRESS, NULL, NULL, 0,
                                  rdm_decode_16bit, start_address, 1, response);
}

bool rdm_set_dmx_start_address(dmx_port_t dmx_num, rdm_uid_t uid,
                               rdm_sub_device_t sub_device,
                               rdm_response_t *response, int start_address) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID || uid == RDM_BROADCAST_ALL_UID, 0, "uid error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");
  DMX_CHECK(start_address > 0 && start_address < DMX_MAX_PACKET_SIZE, 0,
            "start_address must be >0 and <513");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_SET_COMMAND,
                                  RDM_PID_DMX_START_ADDRESS, rdm_encode_16bit,
                                  &start_address, 1, NULL, 0, 0, response);
}

size_t rdm_get_identify_device(dmx_port_t dmx_num, rdm_uid_t uid,
                               rdm_sub_device_t sub_device,
                               rdm_response_t *response, bool *identify) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID, 0, "uid error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");
  DMX_CHECK(!rdm_uid_is_broadcast(uid), 0, "uid cannot be broadcast");
  DMX_CHECK(sub_device != RDM_ALL_SUB_DEVICES, 0,
            "cannot send to all sub-devices");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_GET_COMMAND,
                                  RDM_PID_IDENTIFY_DEVICE, NULL, NULL, 0,
                                  rdm_decode_8bit, identify, 1, response);
}

bool rdm_set_identify_device(dmx_port_t dmx_num, rdm_uid_t uid,
                             rdm_sub_device_t sub_device,
                             rdm_response_t *response, bool identify) {
  DMX_CHECK(dmx_num < DMX_NUM_MAX, 0, "dmx_num error");
  DMX_CHECK(uid <= RDM_MAX_UID || uid == RDM_BROADCAST_ALL_UID, 0, "uid error");
  DMX_CHECK(dmx_driver_is_installed(dmx_num), 0, "driver is not installed");

  return rdm_send_generic_request(dmx_num, uid, sub_device, RDM_CC_SET_COMMAND,
                                  RDM_PID_IDENTIFY_DEVICE, rdm_encode_8bit,
                                  &identify, 1, NULL, NULL, 0, response);
}
*/