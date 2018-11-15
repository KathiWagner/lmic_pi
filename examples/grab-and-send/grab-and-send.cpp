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
#include <unistd.h>
#include <regex.h>
#include <wiringPi.h>
#include <lmic.h>
#include <hal.h>
#include <local_hal.h>

// LoRaWAN Application identifier (AppEUI)
// Not used in this example
static u1_t APPEUI[8];

// LoRaWAN DevEUI, unique device ID (LSBF)
// Not used in this example
static u1_t DEVEUI[8];

// LoRaWAN NwkSKey, network session key
// Use this key for The Things Network
static u1_t DEVKEY[16];

// LoRaWAN AppSKey, application session key
// Use this key to get your data decrypted by The Things Network
static u1_t ARTKEY[16];

// LoRaWAN end-device address (DevAddr)
// See http://thethingsnetwork.org/wiki/AddressSpace
static u4_t DEVADDR; // <-- Change this address for every node!

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

const char optionfilepath[] = "/framectrdata/grab-and-send.conf";
char measurementpath[1024];
u4_t sendinterval = 30;
u4_t cntr=0;
u1_t senddata[51] = "No data yet!                               ";
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

void readTTNConnectionLine(char* line, const char* id, u1_t* container, size_t containersize) {
    char* pEnd;
    fprintf(stdout, "%s ", id);
    container[0] = (u1_t) strtol(line, &pEnd, 0);
    fprintf(stdout, "%02X", container[0]);
    for(size_t i=1; i<containersize; i++) {
        container[i] = (u1_t) strtol(pEnd, &pEnd, 0);
        fprintf(stdout, "%02X", container[i]);
    }
    fprintf(stdout, "\n");
}

/*
 * File format: must contain 5 lines, each defining a distinct idientifier.
 * Identifiers are APPEUI, DEVEUI, DEVKEY, ARTKEY, DEVADDR.
 * Each identifier is followed by the actual ids as hex-digits (0xYY)
 * separated by spaces.
 * */
int readTTNConnectionDataFromFile() {
    FILE* fp = fopen("/framectrdata/ttnConnection.conf", "r");
    if(fp == NULL) {
        fprintf(stderr, "Error: Could not find /framectrdata/ttnConnection.conf! Exiting...\n");
        return 1;
    }
    fprintf(stdout, "Reading TTN connection data...\n");
    char* line;
    size_t len = 0;
    ssize_t read;
    bool idreceived[5] = {false, false, false, false, false};
    while ((read = getline(&line, &len, fp)) != -1) {
        if(strncmp(line, "APPEUI", 6) == 0) {
            char tmp[len - 6];
            memcpy(tmp, line + 6, len - 6);
            readTTNConnectionLine(tmp, "APPEUI", APPEUI, sizeof(APPEUI));
            idreceived[0] = true;
        } else if(strncmp(line, "DEVEUI", 6) == 0) {
            char tmp[len - 6];
            memcpy(tmp, line + 6, len - 6);
            readTTNConnectionLine(tmp, "DEVEUI", DEVEUI, sizeof(DEVEUI));
            idreceived[1] = true;
        } else if(strncmp(line, "DEVKEY", 6) == 0) {
            char tmp[len - 6];
            memcpy(tmp, line + 6, len - 6);
            readTTNConnectionLine(tmp, "DEVKEY", DEVKEY, sizeof(DEVKEY));
            idreceived[2] = true;
        } else if(strncmp(line, "ARTKEY", 6) == 0) {
            char tmp[len - 6];
            memcpy(tmp, line + 6, len - 6);
            readTTNConnectionLine(tmp, "ARTKEY", ARTKEY, sizeof(ARTKEY));
            idreceived[3] = true;
        } else if(strncmp(line, "DEVADDR", 7) == 0) {
            u1_t tempdevaddr[4];
            char tmp[len - 7];
            memcpy(tmp, line + 7, len - 7);
            readTTNConnectionLine(tmp, "DEVADDR", tempdevaddr, sizeof(tempdevaddr));
            DEVADDR = (u4_t)tempdevaddr[3] | (u4_t)tempdevaddr[2] << 8
                | (u4_t)tempdevaddr[1] << 16 | (u4_t)tempdevaddr[0] << 24;

            idreceived[4] = true;
        } else {
            fprintf(stderr, "Error: Unknown id in line %s\n", line);
        }
    }

    fclose(fp);
    if (line) {
        free(line);
    }

    if(idreceived[0] && idreceived[1] && idreceived[2] && idreceived[3] && idreceived[4]) {
        return 0;
    }
    fprintf(stderr, "Error parsing /framectrdata/ttnConnection.conf. Did not receive all ids! Exiting...\n");
    return 1;
}

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

//param datastr null terminated string
bool validateCommand(char* datastr) {
    regex_t regex;
    int reti;
    reti = regcomp(&regex, "^bluetooth: (on|off|clearregistry|register ([0-9a-fA-F][0-9a-fA-F]:){5}([0-9a-fA-F][0-9a-fA-F]))$", REG_EXTENDED);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        return false;
    }
    reti = regexec(&regex, datastr, 0, NULL, 0);
    if(!reti) {
        return true;
    } else if(reti == REG_NOMATCH) {
        return false;
    } else {
        char msgbuf[100];
        regerror(reti, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        return false;
    }
}

void rstrip(char* buff) {
    char* pos;
    if ((pos=strchr(buff, '\n')) != NULL) {
        *pos = '\0';
    }
}

void processReceivedData(char* data, int len) {
    char command[len + 200];
    size_t cx = 0;
    fprintf(stdout, "Got data bytes: ");
    //Prepare command with AES key
    FILE* keyfp = fopen("/framectrdata/cryptokey.txt", "r");
    char key[17];
    if(fgets(key, sizeof(key), keyfp) == NULL) {
        fprintf(stderr, "Error: Could not find /framectrdata/cryptokey.txt. Cannot decrypt incoming messages!");
        return;
    }
    fclose(keyfp);
    rstrip(key);
    cx += snprintf(command + cx, sizeof(command) - cx, "python3 encrypt_bluetoothcmd.py --decrypt --silent -k \"%s\" -t \"", key);
    for(int i=0; i<len; i++) {
        fprintf(stdout, "%02X", data[i]);
        cx += snprintf(command + cx, sizeof(command) - cx, "%X", data[i]);
    }
    fprintf(stdout, "\n");
    cx += snprintf(command + cx, sizeof(command) - cx, "\"");

    fprintf(stdout, "Running decypher command: %s\n", command);
    FILE* decryptp;
    char rawdata[255];
    decryptp = popen(command, "r");
    if(decryptp == NULL) {
        fprintf(stderr, "Error: Running decyper command failed. Exiting!\n");
        return;
    }
    if(fgets(rawdata, sizeof(rawdata)-1, decryptp) == NULL) {
        fprintf(stderr, "Error: Decrypt command did not return a valid result!\n");
        return;
    }
    fclose(decryptp);
    rstrip(rawdata);
    fprintf(stdout, "Got raw message: '%s'\n", rawdata);
    //Compare downframes
    long downframesTTN = strtol(rawdata, NULL, 10);
    u4_t downframesLocal = LMIC.seqnoDn - 1; //Because the current downframe should not yet be counted
    fprintf(stdout, "Downframes TTN: %ld, downframes local: %u\n", downframesTTN, LMIC.seqnoDn);
    if(downframesLocal > (u4_t) downframesTTN) {
        fprintf(stderr, "Warning: Mismatching downframes between TTN and local. Message will not be processed further!\n");
        return;
    }

    if(LMIC.seqnoDn < (u4_t) downframesTTN) {
        // Some downframe messages were not received -> synchronize
        LMIC.seqnoDn = (u4_t) downframesTTN;
        updateFramectrs();
    }

    char* datastr = strstr(rawdata, " ") + 1;
    fprintf(stdout, "Raw command: '%s'\n", datastr);

    //char datastr[len+1];
    //memcpy(datastr, data, len);
    //datastr[len] = '\0';
    if(validateCommand(datastr)) {
        FILE* updatefp = fopen("/framectrdata/update.command", "w");
        if(updatefp == NULL) {
            fprintf(stdout, "Error: Could not open /framectrdata/update.command for writing!\n");
        } else {
            fprintf(updatefp, "%s", datastr);
            fprintf(stdout, "Wrote to command file: '%s'\n", datastr);
        }
        fclose(updatefp);
    } else {
        fprintf(stdout, "Invalid command '%s' received!\n", datastr);
        fprintf(stdout, "Available options are:\n");
        fprintf(stdout, "\tbluetooth: on\n");
        fprintf(stdout, "\tbluetooth: off\n");
        fprintf(stdout, "\tbluetooth: register {MAC-Address}\n");
        fprintf(stdout, "\tbluetooth: clearregistry\n");
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

      FILE* fp;
      fp = fopen(measurementpath, "r");
      char buf[1024];
      long long time = -1;
      char* payloadstr;
      if(fp != NULL) {
          // Get output line by line
          while(fgets(buf, sizeof(buf), fp) != NULL) {
              char* pEnd;
              time = strtoll(buf, &pEnd, 10);
              payloadstr = strtok(pEnd, " \t\r\n\f\v");
          }
          fclose(fp);

          /* Write data to send */
          if(time > lasttime && strlen(payloadstr) >= 0) {
              senddatalen = strlen(payloadstr) / 2;
              for(int i=0; i<senddatalen; i++) {
                sscanf(&payloadstr[i * 2], "%x", &senddata[i]);
              }
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
            fprintf(stdout, "Got time: %lld payload: %s from file\n", time, payloadstr);

      } else {
          fprintf(stderr, "Could not open measurementfile at path: %s\n", measurementpath);
      }



    // Schedule a timed job to run at the given timestamp (absolute system time)
    os_setTimedCallback(j, os_getTime()+sec2osticks(sendinterval), do_send);

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

void fillOptions() {
    FILE* optionsfp = fopen(optionfilepath, "r");
    if(optionsfp != NULL) {
        //Read optionsfile completely
        fseek(optionsfp, 0, SEEK_END);
        size_t fsize = ftell(optionsfp);
        fseek(optionsfp, 0, SEEK_SET);

        char buf[fsize+1];
        fread(buf, fsize, 1, optionsfp);
        fclose(optionsfp);

        buf[fsize] = '\0';
        //Break down options
        char* token = strtok(buf, " \t\r\n\f\v");
        while(token != NULL) {
            //Parse valid options
            if(token == "-o") {
                token = strtok(NULL, " \t\r\n\f\v");
                if(token != NULL) {
                    if(strlen(token) > sizeof (measurementpath)) {
                        fprintf(stderr, "Error parsing %s: path to measurement file too long. "
                                       "Max %d characters are allowed!", optionfilepath,
                                        sizeof (measurementpath));
                        token = NULL;
                    } else {
                        strcpy(measurementpath, token);
                    }
                }
            } else if(token == "-t") {
                token = strtok(NULL, " \t\r\n\f\v");
                if(token != NULL) {
                    long tmp = strtol(token, NULL, 10);
                    if(tmp > 0) {
                        sendinterval = (u4_t) tmp;
                    } else {
                        fprintf(stderr, "Error parsing %s: -t option is no valid integer",
                                        optionfilepath);
                        token = NULL;
                    }
                }
            }
            //Get next option if any
            if(token != NULL) {
                token = strtok(NULL, " \t\r\n\f\v");
            }
        }
    }
}

void getDefaultMeasurementPath() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        fprintf(stdout, "Current working dir: %s\n", cwd);
    } else {
        fprintf(stdout, "Could not get cwd");
    }

    strcpy(measurementpath, cwd);
    strcpy(measurementpath, "measurement.txt");
}


int main() {

    getDefaultMeasurementPath();

    fillOptions();

    setvbuf(stdout, NULL, _IONBF, 0);

    if(readTTNConnectionDataFromFile() != 0) {
      return 1;
    }

    setup();

    //signal(SIGINT, initHandler);
    //howmanyprocess = popen("python3 write_people_to_file.py", "r");

    while (true) {
    loop();
    }
    return 0;
}

