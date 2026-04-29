#include "../include/protocol.h"
#include <fcntl.h>

// GPIO Configuration
#define PIR_PIN 27
#define PIR_PIN2 22
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
static int token_read_fd = -1;
static int token_write_fd = -1;
// motionDetectionCallback
typedef struct MotionEventAux
{
    int16_t eventId;
    int8_t pin;
} MotionEventAux;

int createClient(uint16_t blink_duration, uint16_t blink_count, uint8_t pin);
void motionDetectionCallback(void *aux)
{
    char token;
    if (read(token_read_fd, &token, 1) <= 0)
        return;
    // Check lock var if currently sending
    // printf("motion\n");
    if (is_sending)
    {
        write(token_write_fd, "T", 1);
        return;
    }
    uint8_t pin = aux == NULL ? -1 : ((MotionEventAux *)aux)->pin;
    // If not set lock var
    is_sending = true;

    uint32_t retries = 0;
    bool acknowledged = false;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    printf("[%u] [client] Motion detected! (%d/%u)\n", pin, motion_detected_count + 1, MAX_MOTION_COUNT);

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
        printf("[%u] [client] %u detections reached. Sending FIN...\n", pin, MAX_MOTION_COUNT);

        Packet finPkt = make_packet();
        finPkt.header.sequenceNumber = client_seq;
        finPkt.header.noMoreData = 1;

        char *finRaw = packet_serialize(finPkt, 0);
        sendto(client_socket, finRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, addr_len);
        log_packet(finPkt, cfg.logfilePath, Send);
        free(finRaw);
        breakKeepAliveLoop = true;
        printf("[%u] [client] Interaction complete. Exiting.\n", pin);
        int16_t eventId = aux == NULL ? -1 : ((MotionEventAux *)aux)->eventId;
        if (eventId >= 0)
            unsubscribeMotionDetectEvent(eventId);
        close(client_socket);
        exit(EXIT_SUCCESS);
    }

    is_sending = false; // Release lock
    write(token_write_fd, "T", 1);
}
int main(int argc, char *argv[])
{

    cfg = parseClientArgs(argc, argv);
    if (!cfg.serverIp || cfg.port == 0)
    {
        fprintf(stderr, "Usage: lightclient -s <IP> -p <PORT> -l <LOG>\n");
        exit(EXIT_FAILURE);
    }

    int parent_to_child[2];
    int child_to_parent[2];
    if (pipe(parent_to_child) < 0 || pipe(child_to_parent) < 0)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (!pid)
    {
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        token_read_fd = parent_to_child[0];
        token_write_fd = child_to_parent[1];
        fcntl(token_read_fd, F_SETFL, O_NONBLOCK);
        createClient(500, 5, PIR_PIN);
    }
    else
    {
        close(parent_to_child[0]);
        close(child_to_parent[1]);
        token_read_fd = child_to_parent[0];
        token_write_fd = parent_to_child[1];
        fcntl(token_read_fd, F_SETFL, O_NONBLOCK);
        write(token_write_fd, "T", 1);
        createClient(200, 7, PIR_PIN2);
    }
}

int createClient(uint16_t blink_duration, uint16_t blink_count, uint8_t pin)
{
    setupGPIO();
    setPin(pin);
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
        printf("[%u] failed to create connection\n", pin);
        exit(EXIT_FAILURE);
    }
    printf("[%u] connected to server\n", pin);
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
            printf("[%u] bytesBlinkACK < 0, retransmit?\n", pin);
            continue;
        }

        Packet ack = packet_deserialize(buf, bytesBlinkACK);
        log_packet(ack, cfg.logfilePath, Receive);

        uint16_t netDur, netCount;
        memcpy(&netDur, ack.payload, 2);
        memcpy(&netCount, ack.payload + 2, 2);

        uint16_t blinkDurACK = ntohs(netDur);
        uint16_t blinkCountACK = ntohs(netCount);
        if (blinkDurACK != blink_duration || blinkCountACK != blink_count)
        {
            printf("[%u] blinkDurACK != sentDur || blinkCountACK != sentCount, retransmit?\n", pin);
            continue;
        }
        param_ack = true;
        client_seq += 4;
        server_ack_val = ack.header.sequenceNumber;
        free(ack.payload);

    } while (!param_ack && ++retries < MAX_RETRIES);
    if (retries >= MAX_RETRIES)
    {
        printf("[%u] failed to send LED params\n", pin);
        return -1;
    }
    printf("[%u] [client] Parameters set. Monitoring PIR sensor...\n", pin);

    // subscribeMotionDetectEvent() using WiringPi Interrupt
    // (PIR_PIN, INT_EDGE_RISING, callback)

    MotionEventAux motionEventAux;
    motionEventAux.pin = pin;
    motionEventAux.eventId = subscribeMotionDetectEvent(motionDetectionCallback, &motionEventAux);

    while (!breakKeepAliveLoop)
    {
        delay(1000);
    } // Keep main thread alive
    return 0;
}