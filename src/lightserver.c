/*
 * lightserver.c
 * CSC4200 — Program 3: Detect a Person
 *
 * Usage:
 *   lightserver -p <PORT> -s <LOG FILE LOCATION>
 *
 * Responsibilities:
 *   1. Parse arguments and validate the port.
 *   2. Listen on the specified UDP port (runs forever).
 *   3. Complete the three-way handshake with incoming clients.
 *   4. Receive blink parameters (duration + count) and echo them back.
 *   5. Wait for ":MotionDetected" from the client.
 *   6. Blink the LED the agreed number of times / duration.
 *   7. Receive FIN and log the completion message.
 *
 * GPIO wiring (LED):
 *   LED anode (+) → 330Ω resistor → GPIO 17 (Physical pin 11)
 *   LED cathode (−) → GND (Physical pin 9)
 *   WiringPi numbering: GPIO 17 = WiringPi pin 0
 *
 * Compile (on the Pi):
 *   gcc lightserver.c protocol.c -o lightserver -lwiringPi -lssl -lcrypto
 * Run:
 *   sudo ./lightserver -p 6543 -s server.log
 */

#include "../include/protocol.h"
#include <wiringPi.h>   /* GPIO — sudo apt install wiringpi */

/* ── LED GPIO pin (WiringPi numbering) ──
 * WiringPi pin 0  =  BCM GPIO 17  =  Physical pin 11.
 * Change this if you used a different pin. */
#define LED_PIN 0

/* MotionDetected sentinel string — must match lightclient.c exactly */
#define MOTION_STRING     ":MotionDetected"
#define MOTION_STRING_LEN 15

/* ─────────────────────────────────────────────────────────────────────────
 * setup_gpio()
 * Initialises WiringPi and configures the LED pin as output, starting LOW.
 * Must run as root (sudo) for GPIO access on most Pi OS versions.
 * ───────────────────────────────────────────────────────────────────────── */
static bool setup_gpio(void)
{
    if (wiringPiSetup() == -1)
    {
        fprintf(stderr, "[server] wiringPiSetup() failed — run with sudo\n");
        return false;
    }
    pinMode(LED_PIN, OUTPUT);    /* configure pin as digital output */
    digitalWrite(LED_PIN, LOW);  /* ensure LED starts off */
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────
 * blink_led()
 * Blinks the LED `count` times, with each HIGH and LOW phase lasting
 * `duration_ms` milliseconds.
 *
 * Example: blink_led(500, 3) → LED on 500ms, off 500ms, repeated 3 times.
 * ───────────────────────────────────────────────────────────────────────── */
static void blink_led(uint16_t duration_ms, uint16_t count)
{
    printf("[server] Blinking LED %u times at %u ms per phase...\n",
           count, duration_ms);

    for (uint16_t i = 0; i < count; i++)
    {
        digitalWrite(LED_PIN, HIGH);  /* LED on  */
        delay(duration_ms);           /* WiringPi ms delay */
        digitalWrite(LED_PIN, LOW);   /* LED off */
        delay(duration_ms);
    }

    printf("[server] LED blink sequence complete.\n");
}

/* ─────────────────────────────────────────────────────────────────────────
 * onConnectionCallback()
 * Called by startListening() (in protocol.c) after the three-way handshake
 * completes.  This is where the application-level protocol runs:
 *
 *   Step 3 → receive blink params, echo them back
 *   Step 5 → wait for :MotionDetected
 *   Step 6 → blink LED
 *   Step 7 → receive FIN, log completion
 *
 * Parameters:
 *   worker_socket    — dedicated UDP socket for this client session
 *   serverConfig     — port and log-file path
 *   connectionData   — pointers to both addresses and both ISNs
 * ───────────────────────────────────────────────────────────────────────── */
void onConnectionCallback(int worker_socket, ServerConfig serverConfig,
                          ConnectionData connectionData)
{
    printf("[server] Handshake complete. Starting application protocol.\n");

    /* Convenience aliases */
    struct sockaddr_in *client_addr = connectionData.client_addr;
    socklen_t           client_len  = sizeof(struct sockaddr_in);
    uint32_t            server_isn  = *connectionData.server_isn;
    uint32_t            client_isn  = *connectionData.client_isn;

    /* Set receive timeout so we don't block forever waiting for the client */
    struct timeval tv = { TIMEOUT_SEC, TIMEOUT_USEC };
    if (setsockopt(worker_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        perror("[server] setsockopt SO_RCVTIMEO");

    /* After the handshake ACK the client's next seq is clientISN + 1 (the ACK)
     * then clientISN + 1 + 4 after the blink-param packet.  We track what
     * sequence number we expect from the client next. */
    uint32_t expected_client_seq = client_isn + 1;   /* starts after handshake ACK */

    /* We increment our own sequence number as we send data */
    uint32_t server_seq = server_isn + 1;             /* starts after our SYN|ACK */

    /* ─────────────────────────────────────────────────────────────────────
     * Step 3a: Receive blink parameters from the client
     *
     * The payload is two big-endian uint16_t values:
     *   bytes [0..1] → blink duration in ms
     *   bytes [2..3] → blink count
     * ───────────────────────────────────────────────────────────────────── */
    uint16_t blink_duration = 500;   /* sensible defaults if recv fails */
    uint16_t blink_count    = 5;

    {
        char    buf[HEADER_SIZE + MAX_PAYLOAD];
        ssize_t n = recvfrom(worker_socket, buf, sizeof(buf), 0,
                             (struct sockaddr *)client_addr, &client_len);
        if (n < 0)
        {
            perror("[server] recvfrom blink params");
            return;
        }

        Packet paramPkt = packet_deserialize(buf);
        log_packet(paramPkt, serverConfig.logfilePath, Receive);

        /* Parse the two uint16_t values from the payload */
        if (paramPkt.header.payloadLength >= 4 && paramPkt.payload)
        {
            uint16_t raw_dur, raw_cnt;
            memcpy(&raw_dur, paramPkt.payload,     sizeof(uint16_t));
            memcpy(&raw_cnt, paramPkt.payload + 2, sizeof(uint16_t));
            blink_duration = ntohs(raw_dur);
            blink_count    = ntohs(raw_cnt);
        }

        printf("[server] Received blink params: duration=%u ms, count=%u\n",
               blink_duration, blink_count);

        /* Advance the expected client sequence number */
        expected_client_seq = paramPkt.header.sequenceNumber +
                              paramPkt.header.payloadLength;

        free(paramPkt.payload);
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 3b: Echo blink parameters back as acknowledgement
     *
     * The server sends the same duration and count back inside its ACK
     * payload so the client can confirm the server understood correctly.
     * ───────────────────────────────────────────────────────────────────── */
    {
        uint16_t echo[2];
        echo[0] = htons(blink_duration);
        echo[1] = htons(blink_count);

        Packet echoPkt = make_packet();
        echoPkt.header.sequenceNumber       = server_seq;
        echoPkt.header.acknowledgmentNumber  = expected_client_seq;
        echoPkt.header.acknowledgmentValid   = 1;   /* ACK flag */
        echoPkt.header.payloadLength         = sizeof(echo);
        echoPkt.payload                      = (char *)echo;

        char *echoRaw = packet_serialize(echoPkt);

        if (sendto(worker_socket, echoRaw, HEADER_SIZE + sizeof(echo), 0,
                   (struct sockaddr *)client_addr, client_len) < 0)
        {
            perror("[server] sendto blink-params echo");
            free(echoRaw);
            return;
        }
        log_packet(echoPkt, serverConfig.logfilePath, Send);
        free(echoRaw);
        echoPkt.payload = NULL;

        /* Advance server sequence number by payload bytes sent */
        server_seq += (uint32_t)sizeof(echo);
        printf("[server] Blink params echoed to client. Waiting for motion...\n");
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 5: Wait for ":MotionDetected" from the client
     *
     * The client polls its PIR and sends this payload when motion is seen.
     * We block here (with timeout) until it arrives.
     * ───────────────────────────────────────────────────────────────────── */
    {
        char    buf[HEADER_SIZE + MAX_PAYLOAD];
        ssize_t n;
        Packet  motionPkt;
        bool    got_motion = false;
        uint32_t retries   = 0;

        do {
            n = recvfrom(worker_socket, buf, sizeof(buf), 0,
                         (struct sockaddr *)client_addr, &client_len);
            if (n < 0)
            {
                printf("[server] Timeout waiting for :MotionDetected (%u/%u)...\n",
                       retries + 1, (uint32_t)MAX_RETRIES);
                continue;
            }

            motionPkt = packet_deserialize(buf);
            log_packet(motionPkt, serverConfig.logfilePath, Receive);

            /* Check whether this is a FIN (client gave up) */
            if (motionPkt.header.noMoreData)
            {
                printf("[server] Received FIN before motion — client disconnected.\n");
                free(motionPkt.payload);
                return;
            }

            /* Validate payload matches ":MotionDetected" */
            if (motionPkt.header.payloadLength == MOTION_STRING_LEN &&
                motionPkt.payload &&
                memcmp(motionPkt.payload, MOTION_STRING, MOTION_STRING_LEN) == 0)
            {
                got_motion = true;
                expected_client_seq = motionPkt.header.sequenceNumber +
                                      motionPkt.header.payloadLength;
                free(motionPkt.payload);
                break;
            }

            printf("[server] Unexpected payload (len=%u). Waiting...\n",
                   motionPkt.header.payloadLength);
            free(motionPkt.payload);

        } while (++retries < MAX_RETRIES);

        if (!got_motion)
        {
            fprintf(stderr, "[server] Never received :MotionDetected. Giving up.\n");
            return;
        }
    }

    /* Log the motion event */
    {
        char *ts = time_stamp();
        printf("[server] [%s] :MotionDetected received!\n", ts);

        /* Append to server log */
        FILE *lf = fopen(serverConfig.logfilePath, "a");
        if (lf)
        {
            fprintf(lf, "[%s] :MotionDetected\n", ts);
            fflush(lf);
            fclose(lf);
        }
        free(ts);
    }

    /* Send ACK for the :MotionDetected packet */
    {
        Packet motionAck = make_packet();
        motionAck.header.sequenceNumber       = server_seq;
        motionAck.header.acknowledgmentNumber  = expected_client_seq;
        motionAck.header.acknowledgmentValid   = 1;   /* ACK flag */

        char *ackRaw = packet_serialize(motionAck);
        if (sendto(worker_socket, ackRaw, HEADER_SIZE, 0,
                   (struct sockaddr *)client_addr, client_len) < 0)
            perror("[server] sendto motionAck");
        else
            log_packet(motionAck, serverConfig.logfilePath, Send);
        free(ackRaw);
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 6: Blink the LED
     *
     * Uses the parameters the client sent in Step 2.
     * blink_led() is a blocking call — it returns when done.
     * ───────────────────────────────────────────────────────────────────── */
    blink_led(blink_duration, blink_count);

    /* ─────────────────────────────────────────────────────────────────────
     * Step 7: Receive FIN from the client
     *
     * The client sends FIN after receiving our blink ACK.
     * We log the completion message and return.
     * ───────────────────────────────────────────────────────────────────── */
    {
        char    buf[HEADER_SIZE];
        ssize_t n = recvfrom(worker_socket, buf, HEADER_SIZE, 0,
                             (struct sockaddr *)client_addr, &client_len);
        if (n >= 0)
        {
            Packet finPkt = packet_deserialize(buf);
            log_packet(finPkt, serverConfig.logfilePath, Receive);
            free(finPkt.payload);
        }

        /* Format the client's IP address for the log message */
        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr->sin_addr,
                      client_ip, sizeof(client_ip)) == NULL)
            strcpy(client_ip, "unknown");

        uint16_t client_port = ntohs(client_addr->sin_port);

        /* Completion log: required by spec */
        char *ts = time_stamp();
        printf("[server] [%s] :Interaction with %s:%u completed.\n",
               ts, client_ip, client_port);

        FILE *lf = fopen(serverConfig.logfilePath, "a");
        if (lf)
        {
            fprintf(lf, "[%s] :Interaction with %s:%u completed.\n",
                    ts, client_ip, client_port);
            fflush(lf);
            fclose(lf);
        }
        free(ts);
    }

    /* Make sure the LED is off when we leave */
    digitalWrite(LED_PIN, LOW);
    printf("[server] Session ended. Waiting for next client...\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main()
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    /* ── 1. Parse and validate arguments ── */
    ServerConfig cfg = parseServerArgs(argc, argv);

    if (cfg.port < 1 || cfg.port > 65535)
    {
        fprintf(stderr, "Usage: lightserver -p <PORT> -s <LOGFILE>\n"
                        "  PORT must be between 1 and 65535.\n");
        exit(EXIT_FAILURE);
    }
    if (!cfg.logfilePath)
    {
        fprintf(stderr, "[server] Log file path required (-s)\n");
        exit(EXIT_FAILURE);
    }

    /* ── 2. Initialise GPIO for the LED ── */
    if (!setup_gpio())
        exit(EXIT_FAILURE);

    /* ── 3. Create the main UDP socket ── */
    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Allow the OS to reuse the port immediately after the server stops */
    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* ── 4. Enter the infinite accept loop ──
     * startListening() (protocol.c) never returns.
     * For each incoming SYN it fork()s a child that:
     *   - completes the handshake
     *   - calls onConnectionCallback() above
     * The parent loops back immediately to wait for the next client. */
    struct sockaddr_in server_addr;
    if (!startListening(server_socket, cfg, &server_addr, onConnectionCallback))
    {
        fprintf(stderr, "[server] startListening() failed.\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    /* unreachable — startListening() runs forever */
    close(server_socket);
    return EXIT_SUCCESS;
}