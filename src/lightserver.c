#include "../include/protocol.h"

#define LED_PIN 17

#define LED_PARAM_PAYLOAD_LENGTH 4
#define LED_PARAM_LENGTH 2

int blinkLED(uint16_t blinkDur, uint16_t blinkCount);
void onConnectionCallback(int worker_socket, ServerConfig serverConfig, ConnectionData connectionData)
{
    printf("[server] Session started. Waiting for packets...\n");

    uint16_t blinkDur = 0, blinkCount = 0;
    bool paramsSet = false;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    char buffer[MAX_PAYLOAD + HEADER_SIZE];

    // server_isn and client_isn from handshake
    uint32_t server_seq = *connectionData.server_isn + 1;
    uint32_t expected_client_seq = *connectionData.client_isn + 1;

    while (1)
    {
        int64_t rec = recvfrom(worker_socket, buffer, sizeof(buffer), 0,
                               (struct sockaddr *)connectionData.client_addr, &addr_len);
        if (rec < 0)
        {
            printf("rec bytes < 0, retransmit?\n");
            continue;
        }

        Packet pkt = packet_deserialize(buffer, rec);
        log_packet(pkt, serverConfig.logfilePath, Receive);

        uint16_t payloadLength = rec - HEADER_SIZE;
        // If fin packet
        if (pkt.header.noMoreData)
        {
            Packet finAck = make_packet();
            finAck.header.sequenceNumber = server_seq;
            finAck.header.acknowledgmentNumber = pkt.header.sequenceNumber + 1;
            finAck.header.acknowledgmentValid = 1;
            finAck.header.noMoreData = 1;

            char *raw = packet_serialize(finAck, 0);
            sendto(worker_socket, raw, HEADER_SIZE, 0, (struct sockaddr *)connectionData.client_addr, addr_len);
            log_packet(finAck, serverConfig.logfilePath, Send);

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &connectionData.client_addr->sin_addr, ip, sizeof(ip));
            printf("[server] Interaction with %s completed.\n", ip);

            free(raw);
            free(pkt.payload);
            break; // Break loop in onConnection
        }

        // If led packet (Length 4)
        else if (payloadLength == 4)
        {
            uint16_t netDur, netCount;
            memcpy(&netDur, pkt.payload, LED_PARAM_LENGTH);
            memcpy(&netCount, pkt.payload + LED_PARAM_LENGTH, LED_PARAM_LENGTH);

            blinkDur = ntohs(netDur);
            blinkCount = ntohs(netCount);
            paramsSet = true;

            printf("[server] Params updated: %dms, %d blinks\n", blinkDur, blinkCount);

            // Send ack (echo payload back)
            Packet ack = make_packet();
            ack.header.sequenceNumber = server_seq;
            ack.header.acknowledgmentNumber = pkt.header.sequenceNumber + sizeof(uint16_t) * 2;
            ack.header.acknowledgmentValid = 1;
            ack.payload = pkt.payload;
            int64_t payloadLength = LED_PARAM_PAYLOAD_LENGTH;
            char *raw = packet_serialize(ack, payloadLength);
            sendto(worker_socket, raw, HEADER_SIZE + payloadLength, 0, (struct sockaddr *)connectionData.client_addr, addr_len);
            log_packet(ack, serverConfig.logfilePath, Send);

            server_seq += 4;
            free(raw);
        }

        // If motion packet
        else if (pkt.payload && strcmp(pkt.payload, ":MotionDetected") == 0)
        {
            printf("[server] Motion! Blinking %d times at %dms\n", blinkCount, blinkDur);

            // Blink the LED
            blinkLED(blinkDur, blinkCount);
            // Send ACK (echo payload)
            Packet ack = make_packet();
            ack.header.sequenceNumber = server_seq;
            ack.header.acknowledgmentNumber = pkt.header.sequenceNumber + payloadLength;
            ack.header.acknowledgmentValid = 1;
            ack.payload = pkt.payload;

            char *raw = packet_serialize(ack, payloadLength);
            sendto(worker_socket, raw, HEADER_SIZE + payloadLength, 0,
                   (struct sockaddr *)connectionData.client_addr, addr_len);
            log_packet(ack, serverConfig.logfilePath, Send);

            server_seq += payloadLength;
            free(raw);
        }
        else
        {
            printf("packet sorting failed\n");
        }

        free(pkt.payload);
    }
    close(worker_socket);
}

int main(int argc, char *argv[])
{
    ServerConfig cfg = parseServerArgs(argc, argv);
    if (cfg.port == 0 || !cfg.logfilePath)
    {
        fprintf(stderr, "Usage: lightserver -p <PORT> -l <LOG>\n");
        exit(EXIT_FAILURE);
    }
    printf("%s\n", cfg.logfilePath);
    if (!setupGPIO())
    {
        printf("failed to setup GPIO\n");
        exit(EXIT_FAILURE);
    }

    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;
    // startListening() (handles main accept loop and forking in protocol.c)
    if (!startListening(server_socket, cfg, &addr, onConnectionCallback))
    {
        printf("startListening failed\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int blinkLED(uint16_t blinkDur, uint16_t blinkCount)
{
    int pid = fork();
    if (!pid)
    {
        Led *led = led_create(LED_PIN);
        if (!led)
            return 1;
        if (led_blink(led, (uint16_t)blinkCount, (uint16_t)blinkDur, (uint16_t)blinkDur, 0) < 0)
        {
            led_destroy(led);
            return 1;
        }
        while (led_busy(led))
            usleep(10000);

        led_destroy(led);
        exit(0);
    }
    return 0;
}