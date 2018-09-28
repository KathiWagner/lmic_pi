/*******************************************************************************
 * Copyright (c) 2015 Thomas Telkamp and Matthijs Kooijman
 *
 * Permission is hereby granted, free of charge, to anyone
 * obtaining a copy of this document and accompanying files,
 * to do whatever they want with them without any restriction,
 * including, but not limited to, copying, modification and redistribution.
 * NO WARRANTY OF ANY KIND IS PROVIDED.
 *
 * This example sends a valid LoRaWAN packet with payload "Hello, world!", that
 * will be processed by The Things Network server.
 *
 * Note: LoRaWAN per sub-band duty-cycle limitation is enforced (1% in g1,
*  0.1% in g2).
 *
 * Change DEVADDR to a unique address!
 * See http://thethingsnetwork.org/wiki/AddressSpace
 *
 * Do not forget to define the radio type correctly in config.h, default is:
 *   #define CFG_sx1272_radio 1
 * for SX1272 and RFM92, but change to:
 *   #define CFG_sx1276_radio 1
 * for SX1276 and RFM95.
 *
 *******************************************************************************/

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <wiringPi.h>
#include <lmic.h>
#include <hal.h>
#include <local_hal.h>

// LoRaWAN Application identifier (AppEUI)
// Not used in this example
//static const u1_t APPEUI[8]  = { 0x02, 0x00, 0x00, 0x00, 0x00, 0xEE, 0xFF, 0xC0 };
static const u1_t APPEUI[8]  = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x01, 0x1A, 0x0D };

// LoRaWAN DevEUI, unique device ID (LSBF)
// Not used in this example
//static const u1_t DEVEUI[8]  = { 0x42, 0x42, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
static const u1_t DEVEUI[8]  = { 0x19, 0x28, 0x37, 0x46, 0x55, 0x56, 0x47, 0x38 };

// LoRaWAN NwkSKey, network session key
// Use this key for The Things Network
//static const u1_t DEVKEY[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };
static const u1_t DEVKEY[16] = { 0x8B, 0x1B, 0x66, 0x21, 0x93, 0x66, 0x3B, 0x65, 0x32, 0x91, 0x2F, 0x05, 0xF2, 0xB5, 0xA0, 0x8B };

// LoRaWAN AppSKey, application session key
// Use this key to get your data decrypted by The Things Network
//static const u1_t ARTKEY[16] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };
static const u1_t ARTKEY[16] = { 0x8C, 0x6E, 0x17, 0x2F, 0x98, 0xB6, 0x4E, 0xED, 0x48, 0x13, 0xF0, 0xDE, 0xBB, 0x8B, 0x85, 0xD3 }
;

// LoRaWAN end-device address (DevAddr)
// See http://thethingsnetwork.org/wiki/AddressSpace
static const u4_t DEVADDR = 0x260112B1; // <-- Change this address for every node!

//////////////////////////////////////////////////
// APPLICATION CALLBACKS
//////////////////////////////////////////////////

// provide application router ID (8 bytes, LSBF)
void os_getArtEui (u1_t* buf) {
    memcpy(buf, APPEUI, 8);
}

// provide device ID (8 bytes, LSBF)
void os_getDevEui (u1_t* buf) {
    memcpy(buf, DEVEUI, 8);
}

// provide device key (16 bytes)
void os_getDevKey (u1_t* buf) {
    memcpy(buf, DEVKEY, 16);
}

u4_t cntr=0;
u1_t senddata[] = {"No data yet!                               "};
u4_t senddatalen = 0;
long long lasttime = 0;
//FILE* howmanyprocess;
static osjob_t sendjob;

// Pin mapping
lmic_pinmap pins = {
  .nss = 6,
  .rxtx = UNUSED_PIN, // Not connected on RFM92/RFM95
  .rst = 0,  // Needed on RFM92/RFM95
  .dio = {7,4,5}
};

void LMIC_setup() {
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();
    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    LMIC_setSession (0x1, DEVADDR, (u1_t*)DEVKEY, (u1_t*)ARTKEY);
    //Get framecounters from persistent storage
    FILE* fp = fopen("/framectrdata/framectrs.txt", "r");
    char buf[1024];
    if(fp != NULL) {
        // Get output line by line
        while(fgets(buf, sizeof(buf), fp) != NULL) {
            char* pEnd;
            LMIC.seqnoUp = (u4_t) strtol(buf, &pEnd, 10);
            LMIC.seqnoDn = (u4_t) strtol(pEnd, NULL, 10);
            fprintf(stdout, "Got up frames %u and downframes %u from file!\n", LMIC.seqnoUp, LMIC.seqnoDn);
        }
        fclose(fp);
    } else {
        fprintf(stdout, "Could not find stroed framecounters, starting with default 0!\n");
    }
    // Disable data rate adaptation
    LMIC_setAdrMode(0);
    // Disable link check validation
    LMIC_setLinkCheckMode(0);
    // Disable beacon tracking
    LMIC_disableTracking ();
    // Stop listening for downstream data (periodical reception)
    LMIC_stopPingable();
    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;
    // Set data rate and transmit power (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF9,14);
    //
}

void updateFramectrs() {
    FILE* fp = fopen("/framectrdata/framectrs.txt", "w");
    fprintf(fp, "%u %u", LMIC.seqnoUp, LMIC.seqnoDn);
    fprintf(stdout, "Updated framecounters upframes %u, downframes %u\n", LMIC.seqnoUp, LMIC.seqnoDn);
    fclose(fp);
}

void processReceivedData(char* data, int len) {
    fprintf(stdout, "Got data bytes: ");
    for(int i=0; i<len; i++) {
        fprintf(stdout, "%02X", data[i]);
    }
    fprintf(stdout, "\n");

    /* Turn bluetooth on or off */
    if(strncmp(data, "bluetooth:", 10) == 0) {
        char* token;
        char  datacpy[len];
        memcpy(datacpy, data, len);
        //Get the 2nd part of the string
        token = strtok(datacpy, " ");
        token = strtok(NULL, " ");
        if(token == NULL) {
            fprintf(stdout, "Invalid command received via LoRaWAN!\n");
        } else {
            if(strncmp(token, "on", 2) == 0 || strncmp(token, "off", 3) == 0) {
                //Valid command -> update command file
                FILE* updatefp = fopen("/framectrdata/update.command", "w");
                if(updatefp == NULL) {
                    fprintf(stdout, "Error: Could not open /framectrdata/update.command for writing!\n");
                } else {
                    fprintf(updatefp, "%s", data);
                    fprintf(stdout, "Wrote to command file: '%s'\n", data);
                }
                fclose(updatefp);
            } else {
                fprintf(stdout, "Invalid option '%s' for 'bluetooth' command received!\n", token);
            }
        }
    }
}

void onEvent (ev_t ev) {
    //debug_event(ev);

    switch(ev) {
            case EV_SCAN_TIMEOUT:
                fprintf(stdout, "EV_SCAN_TIMEOUT\n");
                break;
            case EV_BEACON_FOUND:
                fprintf(stdout, "EV_BEACON_FOUND\n");
                break;
            case EV_BEACON_MISSED:
                fprintf(stdout, "EV_BEACON_MISSED\n");
                break;
            case EV_BEACON_TRACKED:
                fprintf(stdout, "EV_BEACON_TRACKED\n");
                break;
            case EV_JOINING:
                fprintf(stdout, "EV_JOINING\n");
                break;
            case EV_JOINED:
                fprintf(stdout, "EV_JOINED\n");

                // Disable link check validation (automatically enabled
                // during join, but not supported by TTN at this time).
                LMIC_setLinkCheckMode(0);
                break;
            case EV_RFU1:
                fprintf(stdout, "EV_RFU1\n");
                break;
            case EV_JOIN_FAILED:
                fprintf(stdout, "EV_JOIN_FAILED\n");
                break;
            case EV_REJOIN_FAILED:
                fprintf(stdout, "EV_REJOIN_FAILED\n");
                break;
            case EV_TXCOMPLETE:
                // use this event to keep track of actual transmissions
                fprintf(stdout, "Event EV_TXCOMPLETE, time: %d\n", millis() / 1000);
                if (LMIC.txrxFlags & TXRX_ACK) {
                    fprintf(stdout, "Received ack\n");
                }
                //update framecounters in persistent storage
                updateFramectrs();
                if(LMIC.dataLen) { // data received in rx slot after tx
                    //debug_buf(LMIC.frame+LMIC.dataBeg, LMIC.dataLen);
                    char receivedData[LMIC.dataLen];
                    memcpy(receivedData, LMIC.frame+LMIC.dataBeg, LMIC.dataLen);
                    fprintf(stdout, "Data Received!\n");
                    processReceivedData(receivedData, sizeof(receivedData));
                }
                break;
            case EV_LOST_TSYNC:
                fprintf(stdout, "EV_LOST_TSYNC\n");
                break;
            case EV_RESET:
                fprintf(stdout, "EV_RESET\n");
                break;
            case EV_RXCOMPLETE:
                // data received in ping slot
                fprintf(stdout, "EV_RXCOMPLETE\n");
                break;
            case EV_LINK_DEAD:
                fprintf(stdout, "EV_LINK_DEAD\n");
                break;
            case EV_LINK_ALIVE:
                fprintf(stdout, "EV_LINK_ALIVE\n");
                break;
             default:
                fprintf(stdout, "Unknown event\n");
                break;
    }
}

static void do_send(osjob_t* j){
      time_t t=time(NULL);
      fprintf(stdout, "[%x] (%ld) %s\n", hal_ticks(), t, ctime(&t));

      /* Gather data */
     /* FILE* fp;
      char buf[1024];

      fp = popen("howmanypeoplearearound -a wlan0 --number --allmacaddresses", "r");
      if (fp == NULL) {
          printf("Failure running howmanypeoplearearound!");
      }
      // Get output line by line
      bool numline = false;
      bool numfound = false;
      unsigned char numpeople;
      while(fgets(buf, sizeof(buf), fp) != NULL) {
          if(!numline) {
              numline = true;
          } else {
              numpeople = atoi(buf);
              printf("Found %d people\n", numpeople);
              numfound = true;
          }
      }

      pclose(fp); */

      FILE* fp;
      fp = fopen("people.txt", "r");
      char buf[1024];
      long long time = -1;
      int people = -1;
      if(fp != NULL) {
          // Get output line by line
          while(fgets(buf, sizeof(buf), fp) != NULL) {
              char* pEnd;
              time = strtoll(buf, &pEnd, 10);
              people = (int) strtol(pEnd, NULL, 10);
          }
          fclose(fp);
      }
      printf("Got time: %lld people: %d from file\n", time, people);

      /* Write data to send */
      if(time > lasttime && people >= 0) {
          senddatalen = 1;
          senddata[0] = (unsigned char)people;
      } else {
          senddatalen = 0;
      }

      /* Send data */

      // Show TX channel (channel numbers are local to LMIC)
      // Check if there is not a current TX/RX job running
    if (LMIC.opmode & (1 << 7)) {
      fprintf(stdout, "OP_TXRXPEND, not sending. Resetting...");
      LMIC_setup();
    } else if(senddatalen > 0) {
      // Prepare upstream data transmission at the next possible time.
      LMIC_setTxData2(1, senddata, senddatalen, 0);
      //set last transmitted time
      lasttime = time;
    }
    // Schedule a timed job to run at the given timestamp (absolute system time)
    os_setTimedCallback(j, os_getTime()+sec2osticks(30), do_send);

}

void setup() {
  // LMIC init
  wiringPiSetup();

  os_init();

  LMIC_setup();
}

void loop() {

do_send(&sendjob);
while(true) {
    os_runloop();
}


}

/*void initHandler(int dummy) {
    if(howmanyprocess != NULL) {
        pclose(howmanyprocess);
    }
    LMIC_shutdown();
    exit(0);
}*/


int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  setup();

  //signal(SIGINT, initHandler);
  //howmanyprocess = popen("python3 write_people_to_file.py", "r");

  while (true) {
    loop();
  }
  return 0;
}

