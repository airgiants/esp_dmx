/*

  RDM Controller

  This example uses RDM discovery to find devices on the RDM network. If devices
  are found, it iterates through each devices and sends several RDM requests. If
  a response is received, the response is printed to the Serial Monitor.

  Discovery can take several seconds to complete, especially when there are
  several responder devices on the RDM network. For a more comprehensive
  explanation of RDM discovery, see the RDMDiscovery example.

  Created 20 June 2023
  By Mitch Weisbrod

  https://github.com/someweisguy/esp_dmx

*/
#include <Arduino.h>
#include <esp_dmx.h>
#include <rdm/controller.h>

/* First, lets define the hardware pins that we are using with our ESP32. We
  need to define which pin is transmitting data and which pin is receiving data.
  DMX circuits also often need to be told when we are transmitting and when we
  are receiving data. We can do this by defining an enable pin. */
int transmitPin = 17;
int receivePin = 16;
int enablePin = 21;
/* Make sure to double-check that these pins are compatible with your ESP32!
  Some ESP32s, such as the ESP32-WROVER series, do not allow you to read or
  write data on pins 16 or 17, so it's always good to read the manuals. */

/* Next, lets decide which DMX port to use. The ESP32 has either 2 or 3 ports.
  Port 0 is typically used to transmit serial data back to your Serial Monitor,
  so we shouldn't use that port. Lets use port 1! */
dmx_port_t dmxPort = 1;

/* Now lets allocate an array of UIDs to store the UIDs of any RDM devices that
  we may find. 32 should be more than plenty, but this can be increased if
  desired! */
rdm_uid_t uids[32];

void setup() {
  /* Start the serial connection back to the computer so that we can log
    messages to the Serial Monitor. Lets set the baud rate to 115200. */
  Serial.begin(115200);

  /* Now we will install the DMX driver! We'll tell it which DMX port to use, 
    what device configure to use, and which interrupt priority it should have. 
    If you aren't sure which configuration or interrupt priority to use, you can
    use the macros `DMX_CONFIG_DEFAULT` and `DMX_INTR_FLAGS_DEFAULT` to set the
    configuration and interrupt to their default settings. */
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  dmx_driver_install(dmxPort, &config, DMX_INTR_FLAGS_DEFAULT);

  /* Now set the DMX hardware pins to the pins that we want to use and DMX
    driver setup will be complete! */
  dmx_set_pin(dmxPort, transmitPin, receivePin, enablePin);

  /* RDM Discovery can take several seconds to complete. With just 1 RDM capable
    device in the RDM network, discovery should take around 30ms. */

  /* Call the default discovery implementation. This implementation searches for
    RDM devices on the network. When a device is found, its UID is added to an
    array of UIDs. This function will never overflow the UID array as long as
    the size argument is set properly. */
  int devicesFound = rdm_discover_devices_simple(dmxPort, uids, 32);

  /* If any devices were found during discovery, lets iterate through them. */
  for (int i = 0; i < devicesFound; ++i) {
    Serial.printf("Device %i has UID " UIDSTR "\n", i, UID2STR(uids[i]));

    /* Now we will send RDM requests to the devices we found. We first need to
      address our requests to the proper device. We can do this with an RDM
      header. We will copy the UID we found during discovery to the RDM header
      to properly address our RDM requests. We will also declare an RDM ACK to
      get information about RDM responses, but this isn't necessary if it is not
      desired. */
    rdm_header_t header = {.dest_uid = uids[i]};
    rdm_ack_t ack;

    /* First, we will send a request to get the device information of our RDM
      device. We can pass our DMX port and pointers to our header, device info,
      and ACK to our request function. If the request is successful, we will
      print out some of the device information we received. */
    rdm_device_info_t deviceInfo;
    if (rdm_send_get_device_info(dmxPort, &header, &deviceInfo, &ack)) {
      Serial.printf(
          "DMX Footprint: %i, Sub-device count: %i, Sensor count: %i\n",
          deviceInfo.footprint, deviceInfo.sub_device_count,
          deviceInfo.sensor_count);
    }

    /* Second, we will send a request to get the device's software version
      label. Strings in RDM typically have a maximum length of 32 characters. We
      should allocate space for 32 characters and one null terminator. */
    char softwareVersionLabel[33];
    if (rdm_send_get_software_version_label(
            dmxPort, &header, softwareVersionLabel, 33, &ack)) {
      Serial.printf("Software version label: %s\n", softwareVersionLabel);
    }

    /* Now we will get and set the identify device parameter. Unlike the
      previous two parameters, identify device can be both get and set. We will
      first get the identify device parameter and set it to its opposite
      state. */
    uint8_t identify;
    if (rdm_send_get_identify_device(dmxPort, &header, &identify, &ack)) {
      Serial.printf(UIDSTR " is%s identifying.\n", UID2STR(uids[i]),
                    identify ? "" : " not");

      /* Set the identify device parameter to its opposite state. */
      identify = !identify;
      if (rdm_send_set_identify_device(dmxPort, &header, identify, &ack)) {
        Serial.printf(UIDSTR " is now%s identifying.\n", UID2STR(uids[i]),
                      identify ? "" : " not");
      }
    }

    /* Finally, we will get and set the DMX start address. It is not required
      for all RDM devices to support the DMX start address parameter so it is
      possible (but unlikely) that your device does not support this parameter.
      After getting the DMX start address, we will increment it by one. */
    uint16_t dmxStartAddress;
    if (rdm_send_get_dmx_start_address(dmxPort, &header, &dmxStartAddress,
                                       &ack)) {
      Serial.printf("DMX start address is %i\n", dmxStartAddress);

      /* Increment the DMX start address and ensure it is between 1 and 512. */
      ++dmxStartAddress;
      if (dmxStartAddress > 512) {
        dmxStartAddress = 1;
      }

      /* Set the updated DMX start address! */
      if (rdm_send_set_dmx_start_address(dmxPort, &header, dmxStartAddress,
                                         &ack)) {
        Serial.printf("DMX address has been set to %i\n", dmxStartAddress);
      }
    } else if (ack.type == RDM_RESPONSE_TYPE_NACK_REASON) {
      /* In the event that the DMX start address request fails, print the reason
        for the failure. The NACK reason and other information on the response 
        can be found in the ack variable we declared earlier. */
      Serial.printf(UIDSTR " GET DMX_START_ADDRESS NACK reason: 0x%02x\n",
                    ack.src_uid, ack.nack_reason);
    }
  }

  if (devicesFound == 0) {
    /* Oops! No RDM devices were found. Double-check your DMX connections and
      try again. */
    Serial.printf("Could not find any RDM capable devices.\n");
  }
}

void loop() {
  /* The main loop has been left empty for this example. Feel free to add your
    own DMX loop here! */
}
