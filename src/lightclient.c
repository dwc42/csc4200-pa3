/*
 * lightclient.c
 * CSC4200 — Program 3: Detect a Person
 *
 * Usage:
 *   lightclient -s <SERVER-IP> -p <PORT> -l <LOGFILE>
 *
 * Responsibilities:
 *   1. Parse arguments and validate them.
 *   2. Perform the three-way handshake with lightserver.
 *   3. Send blink parameters (duration + count) to the server.
 *   4. Wait for the server's echo-ACK confirming the parameters.
 *   5. Wait for the PIR sensor to detect motion.
 *   6. Send ":MotionDetected" to the server.
 *   7. Send FIN to end the interaction.
 *
 * GPIO wiring (HC-SR501 PIR):
 *   PIR VCC → Pi Pin 2  (5V)
 *   PIR GND → Pi Pin 6  (GND)
 *   PIR OUT → GPIO 4, Pi Pin 7  (WiringPi pin 7)
 *
 * Compile (on the Pi):
 *   gcc lightclient.c protocol.c -o lightclient -lwiringPi -lssl -lcrypto
 * Run:
 *   sudo ./lightclient -s 192.168.x.x -p 6543 -l client.log
 */

#include "../include/protocol.h"
#include <wiringPi.h>   /* GPIO access — install with: sudo apt install wiringpi */

/* ── GPIO pin for the PIR sensor output (WiringPi numbering) ──
 * WiringPi pin 7  =  BCM GPIO 4  =  Physical pin 7 on the 40-pin header.
 * Change this if you wired your PIR to a different pin. */
#define PIR_PIN 7

/* ── Blink parameters sent to the server ──
 * Adjust these values before the demo if needed. */
#define BLINK_DURATION_MS  500   /* how long each LED on/off phase lasts (ms) */
#define BLINK_COUNT         5    /* how many times the LED should blink */

/* ── PIR warm-up and polling ── */
#define PIR_WARMUP_SEC     30    /* seconds to wait for the PIR to stabilise */
#define PIR_POLL_MS       100    /* polling interval in milliseconds */
#define PIR_DEBOUNCE_CNT    3    /* consecutive HIGH reads before we trust it */

/* MotionDetected payload string — MUST match exactly what the server expects */
#define MOTION_STRING      ":MotionDetected"
#define MOTION_STRING_LEN  15   /* strlen(":MotionDetected") */

/* ─────────────────────────────────────────────────────────────────────────
 * setup_gpio()
 * Initialises WiringPi and configures the PIR pin as an input.
 * Returns true on success, false if wiringPiSetup() fails.
 * ───────────────────────────────────────────────────────────────────────── */
static bool setup_gpio(void)
{
    if (wiringPiSetup() == -1)
    {
        fprintf(stderr, "[client] wiringPiSetup() failed — are you running as root?\n");
        return false;
    }
    pinMode(PIR_PIN, INPUT);   /* PIR OUT drives this pin; we only read it */
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────
 * wait_for_motion()
 * Polls the PIR sensor until motion is confirmed.
 * Uses a simple software debounce: the pin must read HIGH for
 * PIR_DEBOUNCE_CNT consecutive polls before we return true.
 *
 * The PIR warm-up sleep (PIR_WARMUP_SEC) is done once here so the
 * rest of main() stays clean.
 * ───────────────────────────────────────────────────────────────────────── */
static void wait_for_motion(void)
{
    printf("[client] Waiting %d seconds for PIR to warm up...\n", PIR_WARMUP_SEC);
    sleep(PIR_WARMUP_SEC);   /* HC-SR501 stabilisation period */

    printf("[client] PIR ready. Polling for motion every %d ms...\n", PIR_POLL_MS);

    int consecutive = 0;   /* debounce counter */
    while (1)
    {
        int val = digitalRead(PIR_PIN);   /* HIGH (1) = motion detected */

        if (val == HIGH)
        {
            consecutive++;
            if (consecutive >= PIR_DEBOUNCE_CNT)
            {
                printf("[client] Motion confirmed after %d consecutive HIGH reads.\n",
                       consecutive);
                return;   /* confirmed motion — exit polling loop */
            }
        }
        else
        {
            /* Pin went LOW before we hit the threshold — reset debounce */
            consecutive = 0;
        }

        delay(PIR_POLL_MS);   /* WiringPi millisecond delay */
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * send_with_retry()
 * Sends a pre-serialised packet and waits for an ACK.
 * Retransmits up to MAX_RETRIES times on timeout/loss.
 *
 * Parameters:
 *   sock          — the UDP socket
 *   raw           — serialised packet buffer
 *   raw_len       — number of bytes to send
 *   server_addr   — destination address
 *   addr_len      — size of server_addr
 *   sent_pkt      — the Packet struct (for logging)
 *   logpath       — log file path
 *   expected_ack  — the acknowledgmentNumber we expect in the reply
 *   out_server_isn — if non-NULL, filled with the server's sequence number
 *                    from the reply (used to track server's seq after blink ACK)
 *
 * Returns true on success, false if we run out of retries.
 * ───────────────────────────────────────────────────────────────────────── */
static bool send_with_retry(int sock, const char *raw, size_t raw_len,
                             struct sockaddr_in *server_addr, socklen_t addr_len,
                             Packet sent_pkt, char *logpath,
                             uint32_t expected_ack, uint32_t *out_server_isn)
{
    char     recvBuf[HEADER_SIZE + MAX_PAYLOAD];
    uint32_t retries = 0;

    do {
        /* Send the packet */
        if (sendto(sock, raw, raw_len, 0,
                   (struct sockaddr *)server_addr, addr_len) < 0)
        {
            perror("[client] sendto");
            return false;
        }
        log_packet(sent_pkt, logpath, Send);

        /* Wait for the ACK */
        ssize_t n = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                             (struct sockaddr *)server_addr, &addr_len);
        if (n < 0)
        {
            printf("[client] Timeout / recv failed — retransmitting...\n");
            continue;
        }

        Packet reply = packet_deserialize(recvBuf);
        log_packet(reply, logpath, Receive);

        /* Validate the ACK number */
        if (!reply.header.acknowledgmentValid ||
            reply.header.acknowledgmentNumber != expected_ack)
        {
            printf("[client] ACK mismatch (got %u, expected %u) — retrying...\n",
                   reply.header.acknowledgmentNumber, expected_ack);
            free(reply.payload);
            continue;
        }

        /* Optionally record the server's current sequence number */
        if (out_server_isn)
            *out_server_isn = reply.header.sequenceNumber;

        free(reply.payload);
        return true;

    } while (++retries < MAX_RETRIES);

    fprintf(stderr, "[client] send_with_retry: gave up after %u retries\n", MAX_RETRIES);
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main()
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    /* ── 1. Parse and validate arguments ── */
    ClientConfig cfg = parseClientArgs(argc, argv);

    if (!cfg.serverIp)
    {
        fprintf(stderr, "Usage: lightclient -s <SERVER-IP> -p <PORT> -l <LOGFILE>\n");
        exit(EXIT_FAILURE);
    }
    if (cfg.port < 1 || cfg.port > 65535)
    {
        fprintf(stderr, "[client] Invalid port number: %d\n", cfg.port);
        exit(EXIT_FAILURE);
    }
    if (!cfg.logfilePath)
    {
        fprintf(stderr, "[client] Log file path required (-l)\n");
        exit(EXIT_FAILURE);
    }

    /* ── 2. Initialise GPIO for PIR sensor ── */
    if (!setup_gpio())
        exit(EXIT_FAILURE);

    /* ── 3. Create UDP socket ── */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(EXIT_FAILURE); }

    /* Set receive timeout for the handshake and all subsequent receives */
    struct timeval tv = { TIMEOUT_SEC, TIMEOUT_USEC };
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
        close(sock);
        exit(EXIT_FAILURE);
    }

    /* ── 4. Three-way handshake ── */
    struct sockaddr_in server_addr;
    uint32_t           client_ISN;

    if (!createConnection(sock, cfg, &server_addr, &client_ISN))
    {
        fprintf(stderr, "[client] Handshake failed. Exiting.\n");
        close(sock);
        exit(EXIT_FAILURE);
    }
    printf("[client] Handshake complete.\n");

    socklen_t addr_len     = sizeof(server_addr);
    /* Sequence number starts at clientISN + 1 after the handshake ACK */
    uint32_t  client_seq   = client_ISN + 1;
    uint32_t  server_seq   = 0;   /* we'll learn this from the server's replies */

    /* ─────────────────────────────────────────────────────────────────────
     * Step 2: Send Blink Parameters
     *
     * Payload: two uint16_t values in network byte order
     *   [0..1]  blink duration in milliseconds
     *   [2..3]  blink count
     *
     * We also set the ACK flag and include correct seq/ack numbers.
     * ───────────────────────────────────────────────────────────────────── */
    uint16_t blink_params[2];
    blink_params[0] = htons((uint16_t)BLINK_DURATION_MS);
    blink_params[1] = htons((uint16_t)BLINK_COUNT);
    uint32_t params_payload_len = sizeof(blink_params);   /* 4 bytes */

    Packet blinkPkt = make_packet();
    blinkPkt.header.sequenceNumber       = client_seq;
    blinkPkt.header.acknowledgmentNumber  = 0;            /* no ack to report yet */
    blinkPkt.header.acknowledgmentValid   = 1;            /* ACK flag set */
    blinkPkt.header.payloadLength         = params_payload_len;
    blinkPkt.payload = (char *)blink_params;              /* borrow the stack buffer */

    char *blinkRaw = packet_serialize(blinkPkt);

    printf("[client] Sending blink parameters: duration=%d ms, count=%d\n",
           BLINK_DURATION_MS, BLINK_COUNT);

    /* expected_ack = our seq + bytes we're sending */
    uint32_t expected_ack_after_blink = client_seq + params_payload_len;

    if (!send_with_retry(sock, blinkRaw, HEADER_SIZE + params_payload_len,
                         &server_addr, addr_len,
                         blinkPkt, cfg.logfilePath,
                         expected_ack_after_blink, &server_seq))
    {
        fprintf(stderr, "[client] Failed to send blink parameters.\n");
        free(blinkRaw);
        close(sock);
        exit(EXIT_FAILURE);
    }
    free(blinkRaw);
    blinkPkt.payload = NULL;   /* we borrowed the stack buffer; don't free it */

    /* Advance our sequence number by the payload bytes we just sent */
    client_seq += params_payload_len;
    printf("[client] Blink parameters acknowledged by server.\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Steps 4 & 5: Wait for PIR motion, then send ":MotionDetected"
     * ───────────────────────────────────────────────────────────────────── */

    /* Block here until the PIR fires (with debounce) */
    wait_for_motion();

    /* Log the detection event with a timestamp */
    char *ts = time_stamp();
    printf("[client] [%s] Motion detected! Sending notification to server...\n", ts);
    free(ts);

    /* Build the MotionDetected packet */
    Packet motionPkt = make_packet();
    motionPkt.header.sequenceNumber       = client_seq;
    motionPkt.header.acknowledgmentNumber  = 0;
    motionPkt.header.acknowledgmentValid   = 1;          /* ACK flag set */
    motionPkt.header.payloadLength         = MOTION_STRING_LEN;
    motionPkt.payload                      = MOTION_STRING;  /* string literal */

    char *motionRaw = packet_serialize(motionPkt);

    uint32_t expected_ack_after_motion = client_seq + MOTION_STRING_LEN;

    if (!send_with_retry(sock, motionRaw, HEADER_SIZE + MOTION_STRING_LEN,
                         &server_addr, addr_len,
                         motionPkt, cfg.logfilePath,
                         expected_ack_after_motion, &server_seq))
    {
        fprintf(stderr, "[client] Failed to send :MotionDetected.\n");
        free(motionRaw);
        close(sock);
        exit(EXIT_FAILURE);
    }
    free(motionRaw);
    motionPkt.payload = NULL;   /* borrowed string literal — do not free */

    client_seq += MOTION_STRING_LEN;
    printf("[client] :MotionDetected acknowledged by server. LED should be blinking.\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Step 7: Send FIN to signal end of interaction
     *
     * We send FIN with no payload.  The server logs the completion message
     * and either exits (single-client) or loops back (multi-client bonus).
     * ───────────────────────────────────────────────────────────────────── */
    Packet finPkt = make_packet();
    finPkt.header.sequenceNumber  = client_seq;
    finPkt.header.noMoreData      = 1;   /* FIN flag */

    char *finRaw = packet_serialize(finPkt);

    /* For FIN we don't strictly require an ACK from the server per the spec,
     * but we do a single send and log it. */
    if (sendto(sock, finRaw, HEADER_SIZE, 0,
               (struct sockaddr *)&server_addr, addr_len) < 0)
    {
        perror("[client] sendto FIN");
    }
    else
    {
        log_packet(finPkt, cfg.logfilePath, Send);
        printf("[client] FIN sent. Interaction complete.\n");
    }
    free(finRaw);

    /* ── Clean up ── */
    close(sock);
    return EXIT_SUCCESS;
}