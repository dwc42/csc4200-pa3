#include "../include/protocol.h"

// GPIO Configuration
#define PIR_PIN 27
#define BLINK_DURATION_MS 500
#define BLINK_COUNT 5
#define MAX_MOTION_COUNT 10
#define MAX_MOTION_COUNT 10
// Global state for the motion callback
static int client_socket;
static struct sockaddr_in server_addr;
static ClientConfig cfg;
static uint32_t client_seq;
static uint32_t server_ack_val; // The next seq we expect from server
static int motion_detected_count = 0;
static bool is_sending = false; // Lock variable
static bool breakKeepAliveLoop = false;
// motionDetectionCallback
int createClient(uint16_t blink_duration, uint16_t blink_count);
bool coinFLip()
{
    srand(time(NULL));

    // rand() % 2 returns 0 or 1
    return rand() % 2 == 1;
}
void motionDetectionCallback(void *aux)
{

    if (!coinFLip())
        return;
    // Check lock var if currently sending
    printf("motion\n");
    if (is_sending)
        return;

    // If not set lock var
    is_sending = true;

    uint32_t retries = 0;
    bool acknowledged = false;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    printf("[client] Motion detected! (%d/%u)\n", motion_detected_count + 1, MAX_MOTION_COUNT);

    // do { motionDetected Packet; Receive ack } while (< max retries)
    do
    {
        Packet pkt = make_packet();
        pkt.header.sequenceNumber = client_seq;
        pkt.header.acknowledgmentNumber = server_ack_val;
        pkt.header.acknowledgmentValid = 1;
        uint16_t payloadLength = 15; // strlen(":MotionDetected")
        pkt.payload = ":MotionDetected";

        char *raw = packet_serialize(pkt, payloadLength);
        sendto(client_socket, raw, HEADER_SIZE + payloadLength, 0,
               (struct sockaddr *)&server_addr, addr_len);
        log_packet(pkt, cfg.logfilePath, Send);
        free(raw);

        char recvBuf[HEADER_SIZE + MAX_PAYLOAD];
        int64_t bytesRecvAck = recvfrom(client_socket, recvBuf, sizeof(recvBuf), 0, NULL, NULL);
        if (bytesRecvAck > 0)
        {
            Packet ackPkt = packet_deserialize(recvBuf, bytesRecvAck);
            log_packet(ackPkt, cfg.logfilePath, Receive);

            if (ackPkt.header.acknowledgmentValid &&
                ackPkt.header.acknowledgmentNumber == (client_seq + payloadLength))
            {

                client_seq += payloadLength;
                server_ack_val = ackPkt.header.sequenceNumber;
                acknowledged = true;
            }
            free(ackPkt.payload);
        }
    } while (!acknowledged && ++retries < MAX_RETRIES);

    // If i++ >= 10 send fin packet
    motion_detected_count++;
    if (motion_detected_count >= MAX_MOTION_COUNT)
    {
        printf("[client] %u detections reached. Sending FIN...\n", MAX_MOTION_COUNT);

        Packet finPkt = make_packet();
        finPkt.header.sequenceNumber = client_seq;
        finPkt.header.noMoreData = 1;

        char *finRaw = packet_serialize(finPkt, 0);
        sendto(client_socket, finRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, addr_len);
        log_packet(finPkt, cfg.logfilePath, Send);
        free(finRaw);
        breakKeepAliveLoop = true;
        printf("[client] Interaction complete. Exiting.\n");
        int16_t eventId = aux == NULL ? -1 : *((int16_t *)aux);
        if (eventId >= 0)
            unsubscribeMotionDetectEvent(eventId);
        close(client_socket);
        exit(EXIT_SUCCESS);
    }

    is_sending = false; // Release lock
}
int main(int argc, char *argv[])
{

    cfg = parseClientArgs(argc, argv);
    if (!cfg.serverIp || cfg.port == 0)
    {
        fprintf(stderr, "Usage: lightclient -s <IP> -p <PORT> -l <LOG>\n");
        exit(EXIT_FAILURE);
    }
    setupGPIO();
    setPin(PIR_PIN);
    int pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (!pid)
    {
        createClient(500, 5);
    }
    else
    {
        createClient(200, 7);
    }
}

int createClient(uint16_t blink_duration, uint16_t blink_count)
{

    breakKeepAliveLoop = false;
    is_sending = false;

    // if (wiringPiSetup() == -1)
    //     exit(EXIT_FAILURE);
    // pinMode(PIR_PIN, INPUT);

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);

    // createConnection() (handles handshake in protocol.c)
    uint32_t isn;
    if (!createConnection(client_socket, cfg, &server_addr, &isn))
    {
        printf("failed to create connection\n");
        exit(EXIT_FAILURE);
    }
    printf("connected to server\n");
    client_seq = isn + 1; // Start seq after handshake

    // send initial blink params
    uint16_t params[2] = {htons(blink_duration), htons(blink_count)};
    uint32_t retries = 0;
    bool param_ack = false;

    do
    {
        Packet p = make_packet();
        p.header.sequenceNumber = client_seq;
        p.payload = (char *)params;

        char *raw = packet_serialize(p, 4);
        sendto(client_socket, raw, HEADER_SIZE + 4, 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        log_packet(p, cfg.logfilePath, Send);
        free(raw);

        char buf[256];
        int64_t bytesBlinkACK = recvfrom(client_socket, buf, sizeof(buf), 0, NULL, NULL);
        if (bytesBlinkACK <= 0)
        {
            printf("bytesBlinkACK < 0, retransmit?\n");
            continue;
        }

        Packet ack = packet_deserialize(buf, bytesBlinkACK);
        log_packet(ack, cfg.logfilePath, Receive);

        uint16_t netDur, netCount;
        memcpy(&netDur, ack.payload, 2);
        memcpy(&netCount, ack.payload + 2, 2);

        uint16_t blinkDurACK = ntohs(netDur);
        uint16_t blinkCountACK = ntohs(netCount);
        if (blinkDurACK != BLINK_DURATION_MS || blinkCountACK != BLINK_COUNT)
        {
            printf("blinkDurACK != sentDur || blinkCountACK != sentCount, retransmit?\n");
            continue;
        }
        param_ack = true;
        client_seq += 4;
        server_ack_val = ack.header.sequenceNumber;
        free(ack.payload);

    } while (!param_ack && ++retries < MAX_RETRIES);
    if (retries >= MAX_RETRIES)
    {
        printf("failed to send LED params\n");
        return -1;
    }
    printf("[client] Parameters set. Monitoring PIR sensor...\n");

    // subscribeMotionDetectEvent() using WiringPi Interrupt
    // (PIR_PIN, INT_EDGE_RISING, callback)

    int16_t eventId;
    eventId = subscribeMotionDetectEvent(motionDetectionCallback, &eventId);

    while (!breakKeepAliveLoop)
    {
        delay(1000);
    } // Keep main thread alive
    return 0;
}