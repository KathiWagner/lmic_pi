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


// This EUI must be in little-endian format, so least-significant-byte
// first. When copying an EUI from ttnctl output, this means to reverse
// the bytes. For TTN issued EUIs the last bytes should be 0xD5, 0xB3,
// 0x70.
//static const u1_t APPEUI[8]={ 0x70, 0xB3, 0xD5, 0x12, 0x94, 0x19, 0x02, 0x20 };
static const u1_t APPEUI[8]={ 0x20, 0x02, 0x19, 0x94, 0x12, 0xD5, 0xB3, 0x70 };
void os_getArtEui (u1_t* buf) { memcpy(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
//static const u1_t DEVEUI[8]={ 0x0D, 0x1A, 0x01, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
static const u1_t DEVEUI[8]={ 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x01, 0x1A, 0x0D };
void os_getDevEui (u1_t* buf) { memcpy(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
// The key shown here is the semtech default key.
static const u1_t APPKEY[16] = { 0x82, 0xE8, 0x70, 0xB5, 0xA1, 0x1F, 0x33, 0xDB, 0x06, 0xE9, 0xCF, 0x5E, 0xC1, 0xDE, 0x7E, 0x67 };
void os_getDevKey (u1_t* buf) { memcpy(buf, APPKEY, 16);}

u4_t cntr=0;
u1_t senddata[] = {"No data yet!                               "};
u4_t senddatalen = 0;
long long lasttime = 0;
FILE* howmanyprocess;
static osjob_t sendjob;

// Pin mapping
lmic_pinmap pins = {
  .nss = 6,
  .rxtx = UNUSED_PIN, // Not connected on RFM92/RFM95
  .rst = 0,  // Needed on RFM92/RFM95
  .dio = {7,4,5}
};

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
                    fprintf(stdout, "Received ack");
                }
                if(LMIC.dataLen) { // data received in rx slot after tx
                    //debug_buf(LMIC.frame+LMIC.dataBeg, LMIC.dataLen);
                    fprintf(stdout, "Data Received!\n");
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
      //LMIC_reset();
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

  LMIC_setDrTxpow(DR_SF9,14);

  LMIC_reset();
}

void loop() {

do_send(&sendjob);
while(true) {
    os_runloop();
}


}

void intHandler(int dummy) {
    if(howmanyprocess != NULL) {
        pclose(howmanyprocess);
    }
    LMIC_shutdown();
    exit(0);
}


int main() {
  setup();

  signal(SIGINT, intHandler);
  howmanyprocess = popen("python3 write_people_to_file.py", "r");

  while (true) {
    loop();
  }
  return 0;
}

