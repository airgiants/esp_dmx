/**
 * @file utils.h
 * @author Mitch Weisbrod
 * @brief This file contains some functions that can be helpful when performing
 * advanced operations on RDM packets. It is used throughout this library, but
 * is not included by default in esp_dmx.h. It may be manually included by the
 * user when it is needed.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dmx/types.h"
#include "rdm/responder.h"
#include "rdm/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A function type for RDM responder callbacks. This is the type of
 * function that is called when responding to RDM requests.
 */
typedef int (*rdm_driver_cb_t)(dmx_port_t dmx_num, const rdm_header_t *header,
                               void *pd, uint8_t *pdl_out, void *param,
                               const rdm_pid_description_t *desc,
                               const char *param_str);

/**
 * @brief Copies RDM UID from a source buffer directly into a destination
 * buffer. This function swaps endianness, allowing for UIDs to be copied from
 * RDM packet buffers into ESP32 memory. Either the source or the destination
 * should point to an rdm_uid_t type.
 *
 * To avoid overflows, the size of the arrays pointed to by both the destination
 * and source parameters shall be at least six bytes and should not overlap.
 *
 * @param[out] destination A pointer to the destination buffer.
 * @param[in] source A pointer to the source buffer of the UID.
 * @return A pointer to the destination buffer.
 */
void *uidcpy(void *restrict destination, const void *restrict source);

/**
 * @brief Copies RDM UID from a source buffer into a destination buffer. Copying
 * takes place as if an intermediate buffer were used, allowing the destination
 * and source to overlap. This function swaps endianness, allowing for UIDs to
 * be copied from RDM packet buffers into ESP32 memory. Either the source or the
 * destination should point to an rdm_uid_t type.
 *
 * To avoid overflows, the size of the arrays pointed to by both the destination
 * and source parameters shall be at least six bytes.
 *
 * @param[out] destination A pointer to the destination buffer.
 * @param[in] source A pointer to the source buffer of the UID.
 * @return A pointer to the destination buffer.
 */
void *uidmove(void *destination, const void *source);

/**
 * @brief Returns the 48-bit unique ID of the desired DMX port.
 *
 * @param dmx_num The DMX port number.
 * @param[out] uid A pointer to a rdm_uid_t type to store the received UID.
 */
void uid_get(dmx_port_t dmx_num, rdm_uid_t *uid);

/**
 * @brief Returns true if the UIDs are equal to each other. Is equivalent to
 * a == b.
 *
 * @param[in] a A pointer to the first operand.
 * @param[in] b A pointer to the second operand.
 * @return true if the UIDs are equal.
 * @return false if the UIDs are not equal.
 */
static inline bool uid_is_eq(const rdm_uid_t *a, const rdm_uid_t *b) {
  return a->man_id == b->man_id && a->dev_id == b->dev_id;
}

/**
 * @brief Returns true if the first UID is less than the second UID. Is
 * equivalent to a < b.
 *
 * @param[in] a A pointer to the first operand.
 * @param[in] b A pointer to the second operand.
 * @return true if a is less than b.
 * @return false if a is not less than b.
 */
static inline bool uid_is_lt(const rdm_uid_t *a, const rdm_uid_t *b) {
  return a->man_id < b->man_id ||
         (a->man_id == b->man_id && a->dev_id < b->dev_id);
}

/**
 * @brief Returns true if the first UID is greater than the second UID. Is
 * equivalent to a > b.
 *
 * @param[in] a A pointer to the first operand.
 * @param[in] b A pointer to the second operand.
 * @return true if a is greater than b.
 * @return false if a is not greater than b.
 */
static inline bool uid_is_gt(const rdm_uid_t *a, const rdm_uid_t *b) {
  return a->man_id > b->man_id ||
         (a->man_id == b->man_id && a->dev_id > b->dev_id);
}

/**
 * @brief Returns true if the first UID is less than or equal to the second
 * UID. Is equivalent to a <= b.
 *
 * @param[in] a A pointer to the first operand.
 * @param[in] b A pointer to the second operand.
 * @return true if a is less than or equal to b.
 * @return false if a is not less than or equal to b.
 */
static inline bool uid_is_le(const rdm_uid_t *a, const rdm_uid_t *b) {
  return !uid_is_gt(a, b);
}

/**
 * @brief Returns true if the first UID is greater than or equal to the second
 * UID. Is equivalent to a >= b.
 *
 * @param[in] a A pointer to the first operand.
 * @param[in] b A pointer to the second operand.
 * @return true if a is greater than or equal to b.
 * @return false if a is not greater than or equal to b.
 */
static inline bool uid_is_ge(const rdm_uid_t *a, const rdm_uid_t *b) {
  return !uid_is_lt(a, b);
}

/**
 * @brief Returns true if the specified UID is a broadcast address.
 *
 * @param[in] uid A pointer to the unary UID operand.
 * @return true if the UID is a broadcast address.
 * @return false if the UID is not a broadcast address.
 */
static inline bool uid_is_broadcast(const rdm_uid_t *uid) {
  return uid->dev_id == 0xffffffff;
}

/**
 * @brief Returns true if the specified UID is null.
 *
 * @param[in] uid A pointer to the unary UID operand.
 * @return true if the UID is null.
 * @return false if the UID is not null.
 */
static inline bool uid_is_null(const rdm_uid_t *uid) {
  return uid->man_id == 0 && uid->dev_id == 0;
}

/**
 * @brief Returns true if the first UID is targeted by the second UID. A
 * common usage of this function is `uid_is_target(&my_uid,
 * &destination_uid)`.
 *
 * @param[in] uid A pointer to a UID which to check is targeted.
 * @param[in] alias A pointer to a UID which may alias the first UID.
 * @return true if the UID is targeted by the alias UID.
 * @return false if the UID is not targeted by the alias UID.
 */
static inline bool uid_is_target(const rdm_uid_t *uid, const rdm_uid_t *alias) {
  return ((alias->man_id == 0xffff || alias->man_id == uid->man_id) &&
          alias->dev_id == 0xffffffff) ||
         uid_is_eq(uid, alias);
}

/**
 * @brief Emplaces parameter data from a source buffer to a destination buffer.
 * It is necessary to emplace parameter data before it is written and read to
 * ensure it is formatted correctly for the RDM data bus or for the ESP32's
 * memory. Emplacing data swaps the endianness of each parameter field and also
 * optionally writes null terminators for strings and writes optional UID
 * fields.
 *
 * Parameter fields are emplaced using a format string. This provides the
 * instructions on how data is written. Fields are written in the order provided
 * in the format string. The following characters can be used to write parameter
 * data:
 * - 'b' writes an 8-bit byte of data.
 * - 'w' writes a 16-bit word of data.
 * - 'd' writes a 32-bit dword of data.
 * - 'u' writes a 48-bit UID.
 * - 'v' writes an optional 48-bit UID if the UID is not 0000:00000000. Optional
 *   UIDs must be at the end of the format string.
 * - 'a' writes an ASCII string. ASCII strings may be up to 32 characters long
 *   and may or may not be null-terminated. An ASCII string must be at the end
 *   of the format string.
 *
 * Integer literals may be written by beginning the integer with '#' and writing
 * the literal in hexadecimal form. Integer literals must be terminated with an
 * 'h' character. For example, the integer 0xbeef is represented as "#beefh".
 * Integer literals are written regardless of what the underlying value is. This
 * is used for situations such as emplacing a rdm_device_info_t wherein the
 * first two bytes are 0x01 and 0x00.
 *
 * Parameters will continue to be emplaced as long as the number of bytes
 * written does not exceed the size of the destination buffer, as provided in
 * the num argument. A single parameter may be emplaced instead of multiple by
 * including a '$' character at the end of the format string.
 *
 * Null terminators are not used for strings sent on the RDM data bus. When
 * emplacing data onto the RDM data bus, the emplace_nulls argument should be
 * set to false. When emplacing into ESP32 memory to be read by the caller,
 * emplace_nulls should be set to true to ensure that strings are null
 * terminated. Setting emplace_nulls to true will also affect optional UID
 * fields by emplacing a 0000:00000000 into the destination buffer when an
 * optional UID is not present in the source buffer. When emplace_nulls is
 * false, optional UIDs will not be emplaced when its value is 0000:00000000. It
 * is considered good practice to set emplace_nulls to true when the destination
 * buffer is intended to be read by the user, and false when the destination
 * buffer will be sent on the RDM data bus.
 *
 * Example format strings and their corresponding PIDs are included below.
 *
 * RDM_PID_DISC_UNIQUE_BRANCH: "uu$"
 * RDM_PID_DISC_MUTE: "wv$"
 * RDM_PID_DEVICE_INFO: "#0100hwwdwbbwwb$"
 * RDM_PID_SOFTWARE_VERSION_LABEL: "a$"
 * RDM_PID_DMX_START_ADDRESS: "w$"
 *
 * @param[out] destination The destination into which to emplace the data.
 * @param[in] format The format string which instructs the function how to
 * emplace data.
 * @param[in] source The source buffer which is emplaced into the destination.
 * @param num The maximum number of bytes to emplace.
 * @param emplace_nulls True to emplace null terminators and optional UIDs into
 * the source buffer.
 * @return The size of the data that was emplaced.
 */
size_t pd_emplace(void *destination, const char *format, const void *source,
                  size_t num, bool emplace_nulls);

/**
 * @brief Emplaces a 16-bit word into a destination. Used as a convenience
 * function for quickly emplacing NACK reasons and timer values.
 *
 * @param[out] destination A pointer to a destination buffer.
 * @param word The word to emplace.
 * @return The size of the word which was emplaced. Is always 2.
 */
size_t pd_emplace_word(void *destination, uint16_t word);

// TODO: docs
void *pd_alloc(dmx_port_t dmx_num, size_t size);

// TODO: docs
void *pd_find(dmx_port_t dmx_num, rdm_pid_t pid);

// TODO: docs
esp_err_t pd_get_from_nvs(dmx_port_t dmx_num, rdm_pid_t pid, rdm_ds_t ds,
                          void *param, size_t *size);

// TODO: docs
esp_err_t pd_set_to_nvs(dmx_port_t dmx_num, rdm_pid_t pid, rdm_ds_t ds,
                        const void *param, size_t size);

/**
 * @brief Registers a response callback to be called when a request is received
 * for a specified PID for this device. Callbacks may be overwritten, but they
 * may not be deleted. The pointers to the parameter and context are copied by
 * reference and must be valid throughout the lifetime of the DMX driver. The
 * maximum number of response callbacks that may be registered are defined by
 * "Max RDM responder PIDs" found in the kconfig.
 *
 * @param dmx_num The DMX port number.
 * @param sub_device The sub-device to which to register the response callback.
 * @param[in] desc A pointer to a descriptor for the PID to be registered.
 * @param[in] callback A pointer to a callback function which is called when a
 * request for the specified PID is received.
 * @param[in] param A pointer to the parameter which can be used in the response
 * callback.
 * @param[in] context A pointer to a user-defined context.
 * @return true if the response was successfully registered.
 * @return false if the response was not registered.
 * // TODO: update docs
 */
bool rdm_register_parameter(dmx_port_t dmx_num, rdm_sub_device_t sub_device,
                            const rdm_pid_description_t *desc,
                            const char *param_str, rdm_driver_cb_t driver_cb,
                            void *param, rdm_responder_cb_t user_cb,
                            void *context);

// TODO: docs
bool rdm_get_parameter(dmx_port_t dmx_num, rdm_pid_t pid, void *param,
                       size_t size);

// TODO: docs
bool rdm_set_parameter(dmx_port_t dmx_num, rdm_pid_t pid, const void *param,
                       size_t size, bool nvs);

/**
 * @brief Sends an RDM controller request and processes the response. This
 * function writes, sends, receives, and reads a request and response RDM
 * packet. It performs error checking on the written packet to ensure that it
 * adheres to RDM specification and prevents RDM bus errors. An rdm_ack_t is
 * provided to process DMX and RDM errors.
 * - ack.err will evaluate to true if an error occurred during the sending or
 *   receiving of raw DMX data. RDM data will not be processed if an error
 *   occurred. If a response was expected but none was received, ack.err will
 *   evaluate to ESP_ERR_TIMEOUT. If no response was expected, ack.err will be
 *   set to ESP_OK.
 * - ack.size is the size of the received RDM packet, including the RDM
 *   checksum.
 * - ack.src_uid is the UID of the device which responds to the request.
 * - ack.type will evaluate to RDM_RESPONSE_TYPE_INVALID if an invalid
 *   response is received but does not necessarily indicate a DMX error
 *   occurred. If no response is received ack.type will be set to
 *   RDM_RESPONSE_TYPE_NONE whether or not a response was expected. Otherwise,
 *   ack.type will be set to the ack type received in the RDM response.
 * - ack.message_count indicates the number of messages that are waiting to be
 *   retrieved from the responder's message queue.
 * - ack.timer and ack.nack_reason are a union which should be read depending on
 *   the value of ack.type. If ack.type is RDM_RESPONSE_TYPE_ACK_TIMER,
 *   ack.timer should be read. ack.timer is the estimated amount of time in
 *   FreeRTOS ticks until the responder is able to provide a response to the
 *   request. If ack.type is RDM_RESPONSE_TYPE_NACK_REASON, ack.nack_reason
 *   should be read to get the NACK reason.
 *
 * @param dmx_num The DMX port number.
 * @param[inout] header A pointer which stores header information for the RDM
 * request.
 * @param[in] pd_in A pointer which stores parameter data to be written.
 * @param[out] pd_out A pointer which stores parameter data which was read, if a
 * response was received.
 * @param[inout] pdl The size of the pd_out buffer. When receiving data, this is
 * set to the PDL of the received data. Used to prevent buffer overflows.
 * @param[out] ack A pointer to an rdm_ack_t which stores information about the
 * RDM response.
 * @return true if an RDM_RESPONSE_TYPE_ACK response was received.
 * @return false if any other response type was received.
 */
bool rdm_send_request(dmx_port_t dmx_num, rdm_header_t *header,
                      const void *pd_in, void *pd_out, size_t *num,
                      rdm_ack_t *ack);

#ifdef __cplusplus
}
#endif