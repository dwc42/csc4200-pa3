#include "../include/protocol.h"
#include <wiringPi.h>

// GPIO Configuration
#define PIR_PIN 7
#define BLINK_DURATION_MS 500
#define BLINK_COUNT 5

// Global state for the motion callback
static int client_socket;
static struct sockaddr_in server_addr;
static ClientConfig cfg;
static uint32_t client_seq;
static uint32_t server_ack_val; // The next seq we expect from server
static int motion_detected_count = 0;
static bool is_sending = false; // Lock variable


// motionDetectionCallback
void motionDetectionCallback(void) {
    // Check lock var if currently sending
    if (is_sending) return;
    
    // If not set lock var
    is_sending = true;

    uint32_t retries = 0;
    bool acknowledged = false;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    printf("[client] Motion detected! (%d/10)\n", motion_detected_count + 1);

    // do { motionDetected Packet; Receive ack } while (< max retries)
    do {
        Packet pkt = make_packet();
        pkt.header.sequenceNumber = client_seq;
        pkt.header.acknowledgmentNumber = server_ack_val;
        pkt.header.acknowledgmentValid = 1;
        pkt.header.payloadLength = 15; // strlen(":MotionDetected")
        pkt.payload = ":MotionDetected";

        char *raw = packet_serialize(pkt);
        sendto(client_socket, raw, HEADER_SIZE + pkt.header.payloadLength, 0,
               (struct sockaddr *)&server_addr, addr_len);
        log_packet(pkt, cfg.logfilePath, Send);
        free(raw);

        char recvBuf[HEADER_SIZE + MAX_PAYLOAD];
        if (recvfrom(client_socket, recvBuf, sizeof(recvBuf), 0, NULL, NULL) > 0) {
            Packet ackPkt = packet_deserialize(recvBuf);
            log_packet(ackPkt, cfg.logfilePath, Receive);
            
            if (ackPkt.header.acknowledgmentValid && 
                ackPkt.header.acknowledgmentNumber == (client_seq + pkt.header.payloadLength)) {
                
                client_seq += pkt.header.payloadLength;
                server_ack_val = ackPkt.header.sequenceNumber;
                acknowledged = true;
            }
            free(ackPkt.payload);
        }
    } while (!acknowledged && ++retries < MAX_RETRIES);

    // If i++ >= 10 send fin packet
    motion_detected_count++;
    if (motion_detected_count >= 10) {
        printf("[client] 10 detections reached. Sending FIN...\n");
        
        Packet finPkt = make_packet();
        finPkt.header.sequenceNumber = client_seq;
        finPkt.header.noMoreData = 1;
        
        char *finRaw = packet_serialize(finPkt);
        sendto(client_socket, finRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, addr_len);
        log_packet(finPkt, cfg.logfilePath, Send);
        free(finRaw);

        printf("[client] Interaction complete. Exiting.\n");
        close(client_socket);
        exit(EXIT_SUCCESS);
    }

    is_sending = false; // Release lock
}



int main(int argc, char *argv[]) {
    cfg = parseClientArgs(argc, argv);
    if (!cfg.serverIp || cfg.port == 0) {
        fprintf(stderr, "Usage: lightclient -s <IP> -p <PORT> -l <LOG>\n");
        exit(EXIT_FAILURE);
    }

    if (wiringPiSetup() == -1) exit(EXIT_FAILURE);
    pinMode(PIR_PIN, INPUT);

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    // createConnection() (handles handshake in protocol.c)
    uint32_t isn;
    if (!createConnection(client_socket, cfg, &server_addr, &isn)) {
        exit(EXIT_FAILURE);
    }
    client_seq = isn + 1; // Start seq after handshake

    // send initial blink params
    uint16_t params[2] = { htons(BLINK_DURATION_MS), htons(BLINK_COUNT) };
    uint32_t retries = 0;
    bool param_ack = false;
    
    do {
        Packet p = make_packet();
        p.header.sequenceNumber = client_seq;
        p.header.acknowledgmentValid = 1;
        p.header.payloadLength = 4;
        p.payload = (char*)params;

        char *raw = packet_serialize(p);
        sendto(client_socket, raw, HEADER_SIZE + 4, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        log_packet(p, cfg.logfilePath, Send);
        free(raw);

        char buf[256];
        if (recvfrom(client_socket, buf, sizeof(buf), 0, NULL, NULL) > 0) {
            Packet ack = packet_deserialize(buf);
            log_packet(ack, cfg.logfilePath, Receive);
            param_ack = true;
            client_seq += 4;
            server_ack_val = ack.header.sequenceNumber;
            free(ack.payload);
        }
    } while (!param_ack && ++retries < MAX_RETRIES);

    printf("[client] Parameters set. Monitoring PIR sensor...\n");

    // subscribeMotionDetectEvent() using WiringPi Interrupt
    // (PIR_PIN, INT_EDGE_RISING, callback)
    if (wiringPiISR(PIR_PIN, INT_EDGE_RISING, &motionDetectionCallback) < 0) {
        perror("ISR setup failed");
        exit(EXIT_FAILURE);
    }

    while(1) { delay(1000); } // Keep main thread alive
    return 0;
}