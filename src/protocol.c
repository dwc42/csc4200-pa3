/*
 * protocol.c
 * CSC4200 — Program 3: Detect a Person
 *
 * Shared utility functions used by both lightclient and lightserver:
 *   - Packet construction, serialization, and deserialization
 *   - Logging helpers
 *   - Argument parsing
 *   - Three-way handshake (client side and server side)
 *   - Timestamp formatting
 *
 * All multi-byte integers are transmitted in network byte order
 * (big-endian) using htonl() / ntohl().
 */

#include "../include/protocol.h"

/* ─────────────────────────────────────────────
 * Packet helpers
 * ───────────────────────────────────────────── */

/*
 * make_packet()
 * Returns a zero-initialised Packet.  Always use this instead of a
 * raw struct literal so every field starts at a known value.
 */
Packet make_packet(void)
{
    Packet packet;
    packet.header.sequenceNumber      = 0;
    packet.header.acknowledgmentNumber = 0;
    packet.header.unused              = 0;
    packet.header.acknowledgmentValid = 0;
    packet.header.synchronizeSequence = 0;
    packet.header.noMoreData          = 0;
    packet.header.payloadLength       = 0;
    packet.payload                    = NULL;
    return packet;
}

/*
 * packet_serialize()
 * Converts a Packet struct into a flat byte buffer ready to hand to
 * sendto().  The caller is responsible for free()-ing the returned buffer.
 *
 * Wire layout (each field written big-endian):
 *   [0..3]   Sequence Number
 *   [4..7]   Acknowledgment Number
 *   [8..11]  Flags word  (bits: ...A S F)
 *   [12..15] Payload Length          ← Program 2 kept this; Program 3 still
 *                                       uses it so deserialization works.
 *   [16..]   Payload bytes
 */
char *packet_serialize(Packet packet)
{
    /* Total buffer = fixed header + payload + null terminator for safety */
    uint64_t totalSize = HEADER_SIZE + packet.header.payloadLength + 1;
    char *buf = malloc(totalSize);
    if (!buf) return NULL;

    uint8_t  i = 0;          /* index into 4-byte slots */
    uint32_t net;            /* temporary network-order integer */

    /* Slot 0: Sequence Number */
    net = htonl(packet.header.sequenceNumber);
    memcpy(buf + sizeof(uint32_t) * i++, &net, sizeof(uint32_t));

    /* Slot 1: Acknowledgment Number */
    net = htonl(packet.header.acknowledgmentNumber);
    memcpy(buf + sizeof(uint32_t) * i++, &net, sizeof(uint32_t));

    /* Slot 2: Flags word — pack ACK(bit2) | SYN(bit1) | FIN(bit0) */
    net = htonl(
        (packet.header.unused              << 3) |
        (packet.header.acknowledgmentValid << 2) |
        (packet.header.synchronizeSequence << 1) |
         packet.header.noMoreData
    );
    memcpy(buf + sizeof(uint32_t) * i++, &net, sizeof(uint32_t));

    /* Slot 3: Payload Length */
    net = htonl(packet.header.payloadLength);
    memcpy(buf + sizeof(uint32_t) * i++, &net, sizeof(uint32_t));

    /* Payload bytes (may be zero length for handshake/control packets) */
    if (packet.header.payloadLength > 0 && packet.payload != NULL)
        memcpy(buf + HEADER_SIZE, packet.payload, packet.header.payloadLength);

    buf[HEADER_SIZE + packet.header.payloadLength] = '\0'; /* safety sentinel */
    return buf;
}

/*
 * packet_deserialize()
 * Reconstructs a Packet struct from a raw byte buffer received via recvfrom().
 * Dynamically allocates packet.payload — caller must free(packet.payload).
 */
Packet packet_deserialize(char *buf)
{
    Packet   packet = make_packet();
    uint8_t  i = 0;
    uint32_t net;

    /* Slot 0: Sequence Number */
    memcpy(&net, buf + sizeof(uint32_t) * i++, sizeof(uint32_t));
    packet.header.sequenceNumber = ntohl(net);

    /* Slot 1: Acknowledgment Number */
    memcpy(&net, buf + sizeof(uint32_t) * i++, sizeof(uint32_t));
    packet.header.acknowledgmentNumber = ntohl(net);

    /* Slot 2: Flags word — unpack individual flag bits */
    memcpy(&net, buf + sizeof(uint32_t) * i++, sizeof(uint32_t));
    net = ntohl(net);
    packet.header.unused              = net >> 3;
    packet.header.acknowledgmentValid = (net >> 2) & 0x1u;
    packet.header.synchronizeSequence = (net >> 1) & 0x1u;
    packet.header.noMoreData          =  net        & 0x1u;

    /* Slot 3: Payload Length */
    memcpy(&net, buf + sizeof(uint32_t) * i++, sizeof(uint32_t));
    packet.header.payloadLength = ntohl(net);

    /* Payload — allocate and copy; add null terminator for string safety */
    packet.payload = malloc(packet.header.payloadLength + 1);
    if (packet.payload)
    {
        memcpy(packet.payload, buf + HEADER_SIZE, packet.header.payloadLength);
        packet.payload[packet.header.payloadLength] = '\0';
    }
    return packet;
}

/* Debug helper — prints every field of a packet to stdout */
void printPacket(Packet packet)
{
    printf("Packet {\n");
    printf("  seq=%u  ack=%u  flags=[%s%s%s]  len=%u\n",
        packet.header.sequenceNumber,
        packet.header.acknowledgmentNumber,
        packet.header.synchronizeSequence ? "SYN " : "",
        packet.header.acknowledgmentValid  ? "ACK " : "",
        packet.header.noMoreData           ? "FIN"  : "",
        packet.header.payloadLength);
    if (packet.header.payloadLength > 0 && packet.payload)
        printf("  payload=\"%s\"\n", packet.payload);
    printf("}\n");
}

/* ─────────────────────────────────────────────
 * Logging
 * ───────────────────────────────────────────── */

/*
 * log_packet()
 * Appends one line to the log file in the required format:
 *
 *   [TIMESTAMP] SEND|RECV SEQ=<n> ACK=<n> [SYN] [ACK] [FIN] [LEN=<n>]
 *
 * fflush() is called after every write so nothing is lost on a crash.
 */
void log_packet(Packet packet, char *filePath, PacketType packetType)
{
    if (!filePath) return;

    FILE *f = fopen(filePath, "a");
    if (!f) return;

    char *ts = time_stamp();
    const char *typeStr = (packetType == Send) ? "SEND" : "RECV";

    /* Build flag string — only include flags that are actually set */
    char flags[32] = "";
    if (packet.header.synchronizeSequence) strcat(flags, "SYN ");
    if (packet.header.acknowledgmentValid)  strcat(flags, "ACK ");
    if (packet.header.noMoreData)           strcat(flags, "FIN ");

    if (packet.header.payloadLength > 0)
        fprintf(f, "[%s] %s SEQ=%u ACK=%u %sLEN=%u\n",
            ts, typeStr,
            packet.header.sequenceNumber,
            packet.header.acknowledgmentNumber,
            flags,
            packet.header.payloadLength);
    else
        fprintf(f, "[%s] %s SEQ=%u ACK=%u %s\n",
            ts, typeStr,
            packet.header.sequenceNumber,
            packet.header.acknowledgmentNumber,
            flags);

    free(ts);
    fflush(f);
    fclose(f);
}

/*
 * time_stamp()
 * Returns a heap-allocated string "YYYY-MM-DD-HH-MM-SS".
 * Caller must free() the result.
 */
char *time_stamp(void)
{
    time_t     now = time(NULL);
    struct tm *t   = localtime(&now);
    char      *buf = malloc(20);
    if (buf)
        strftime(buf, 20, "%Y-%m-%d-%H-%M-%S", t);
    return buf;
}

/* ─────────────────────────────────────────────
 * Argument parsing
 * ───────────────────────────────────────────── */

/*
 * parseClientArgs()
 * Parses: lightclient -s <SERVER-IP> -p <PORT> -l <LOGFILE>
 * Sets fields to NULL / 0 when not provided — callers validate.
 */
ClientConfig parseClientArgs(int argc, char *argv[])
{
    ClientConfig cfg = { NULL, 0, NULL, NULL };
    for (int i = 1; i < argc; i++)
    {
        if      (strcmp(argv[i], "-s") == 0 && i + 1 < argc) cfg.serverIp   = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) cfg.port       = (uint16_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) cfg.logfilePath = argv[++i];
        /* -f is unused in Program 3 but kept for protocol.h compatibility */
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) cfg.filePath   = argv[++i];
    }
    return cfg;
}

/*
 * parseServerArgs()
 * Parses: lightserver -p <PORT> -s <LOGFILE>
 * Note: the assignment uses -s for the log file on the server side.
 */
ServerConfig parseServerArgs(int argc, char *argv[])
{
    ServerConfig cfg = { 0, NULL, false };
    for (int i = 1; i < argc; i++)
    {
        if      (strcmp(argv[i], "-p") == 0 && i + 1 < argc) cfg.port       = (uint16_t)atoi(argv[++i]);
        /* Assignment spec uses -s for log file on server */
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) cfg.logfilePath = argv[++i];
        /* -l accepted as alias so both spellings work */
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) cfg.logfilePath = argv[++i];
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) cfg.drop       = (atoi(argv[++i]) == 1);
    }
    return cfg;
}

/* ─────────────────────────────────────────────
 * Client-side three-way handshake
 * ───────────────────────────────────────────── */

/*
 * createConnection()
 * Performs the TCP-style three-way handshake over UDP:
 *
 *   Client → Server : SYN  (seq=clientISN, ack=0)
 *   Server → Client : SYN|ACK (seq=serverISN, ack=clientISN+1)
 *   Client → Server : ACK  (seq=clientISN+1, ack=serverISN+1)
 *
 * On success:  *client_ISN is set to the client's initial sequence number,
 *              server_addr is filled with the server's address, returns true.
 * On failure:  closes socket and returns false.
 */
bool createConnection(int socket_client, ClientConfig clientConfig,
                      struct sockaddr_in *server_addr, uint32_t *client_ISN)
{
    /* ── Configure destination address ── */
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port   = htons(clientConfig.port);
    socklen_t addr_len      = sizeof(struct sockaddr_in);

    if (inet_pton(AF_INET, clientConfig.serverIp, &server_addr->sin_addr) <= 0)
    {
        perror("inet_pton: invalid server IP");
        return false;
    }

    /* ── Set receive timeout so we can retransmit on loss ── */
    struct timeval tv = { TIMEOUT_SEC, TIMEOUT_USEC };
    if (setsockopt(socket_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
        return false;
    }

    /* ── Step 1: Send SYN ── */
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    uint32_t clientISN = (uint32_t)rand();
    *client_ISN = clientISN;

    Packet synPkt = make_packet();
    synPkt.header.sequenceNumber      = clientISN;
    synPkt.header.acknowledgmentNumber = 0;
    synPkt.header.synchronizeSequence  = 1;   /* SYN flag */

    char *synRaw = packet_serialize(synPkt);

    /* ── Step 2: Wait for SYN|ACK, retransmit SYN up to MAX_RETRIES ── */
    char        recvBuf[HEADER_SIZE];
    Packet      synAckPkt;
    uint32_t    retries = 0;
    bool        gotSynAck = false;

    do {
        /* Send (or re-send) the SYN */
        if (sendto(socket_client, synRaw, HEADER_SIZE, 0,
                   (struct sockaddr *)server_addr, addr_len) < 0)
        {
            perror("sendto SYN");
            free(synRaw);
            return false;
        }
        log_packet(synPkt, clientConfig.logfilePath, Send);
        printf("[client] Sent SYN (seq=%u)\n", clientISN);

        /* Wait for SYN|ACK */
        if (recvfrom(socket_client, recvBuf, HEADER_SIZE, 0,
                     (struct sockaddr *)server_addr, &addr_len) < 0)
        {
            printf("[client] Timeout waiting for SYN|ACK, retrying...\n");
            continue;
        }

        synAckPkt = packet_deserialize(recvBuf);
        log_packet(synAckPkt, clientConfig.logfilePath, Receive);

        /* Validate: must have SYN and ACK flags, and ack must equal clientISN+1 */
        if (!synAckPkt.header.synchronizeSequence || !synAckPkt.header.acknowledgmentValid)
        {
            printf("[client] Expected SYN|ACK flags, got something else. Retrying...\n");
            free(synAckPkt.payload);
            continue;
        }
        if (synAckPkt.header.acknowledgmentNumber != clientISN + 1)
        {
            printf("[client] SYN|ACK ack mismatch. Retrying...\n");
            free(synAckPkt.payload);
            continue;
        }

        gotSynAck = true;
        break;
    } while (++retries < MAX_RETRIES);

    free(synRaw);

    if (!gotSynAck)
    {
        fprintf(stderr, "[client] Handshake failed: no valid SYN|ACK after %u retries\n", MAX_RETRIES);
        close(socket_client);
        return false;
    }

    uint32_t serverISN = synAckPkt.header.sequenceNumber;
    free(synAckPkt.payload);

    /* ── Step 3: Send ACK to complete handshake ── */
    Packet ackPkt = make_packet();
    ackPkt.header.sequenceNumber       = clientISN + 1;
    ackPkt.header.acknowledgmentNumber  = serverISN + 1;
    ackPkt.header.acknowledgmentValid   = 1;   /* ACK flag */

    char *ackRaw = packet_serialize(ackPkt);
    if (sendto(socket_client, ackRaw, HEADER_SIZE, 0,
               (struct sockaddr *)server_addr, addr_len) < 0)
    {
        perror("sendto ACK");
        free(ackRaw);
        return false;
    }
    log_packet(ackPkt, clientConfig.logfilePath, Send);
    free(ackRaw);

    printf("[client] Handshake complete. clientISN=%u serverISN=%u\n",
           clientISN, serverISN);
    return true;
}

/* ─────────────────────────────────────────────
 * Server-side listening + handshake
 * ───────────────────────────────────────────── */

/*
 * createServerWorkerSocket()
 * Creates a second UDP socket bound to an OS-assigned ephemeral port.
 * The server's main socket stays free to accept the next SYN while
 * this worker socket handles the ongoing session with one client.
 * (Used for multi-client bonus support.)
 */
bool createServerWorkerSocket(int *worker_socket,
                               struct sockaddr_in *worker_addr,
                               uint16_t *worker_port)
{
    *worker_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*worker_socket < 0) { perror("worker socket"); return false; }

    memset(worker_addr, 0, sizeof(struct sockaddr_in));
    worker_addr->sin_family      = AF_INET;
    worker_addr->sin_addr.s_addr = INADDR_ANY;
    worker_addr->sin_port        = htons(0); /* let OS pick a free port */

    if (bind(*worker_socket, (struct sockaddr *)worker_addr,
             sizeof(struct sockaddr_in)) < 0)
    {
        perror("worker bind");
        return false;
    }

    socklen_t len = sizeof(*worker_addr);
    if (getsockname(*worker_socket, (struct sockaddr *)worker_addr, &len) < 0)
    {
        perror("getsockname");
        return false;
    }
    *worker_port = ntohs(worker_addr->sin_port);
    return true;
}

/*
 * startListening()
 * Infinite loop: blocks on the main socket waiting for a SYN from any client.
 * When one arrives, fork()s a child process to complete the handshake and
 * run the session (via the callback), then the parent loops back immediately
 * to accept the next client — this is how multi-client bonus is achieved.
 *
 * The child:
 *   1. Creates a worker socket on an ephemeral port.
 *   2. Sends SYN|ACK to the client.
 *   3. Waits for the client's ACK.
 *   4. Calls the application callback (onConnectionCallback in lightserver.c).
 */
bool startListening(int server_socket, ServerConfig serverConfig,
                    struct sockaddr_in *server_addr,
                    OnConnectionCallback callback)
{
    if (server_socket < 0) { perror("invalid socket"); return false; }

    /* Bind the main socket to the configured port on all interfaces */
    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family      = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port        = htons(serverConfig.port);

    if (bind(server_socket, (struct sockaddr *)server_addr,
             sizeof(struct sockaddr_in)) < 0)
    {
        perror("bind");
        return false;
    }
    printf("[server] Listening on port %d...\n", serverConfig.port);

    while (1) /* run forever — accept new clients in a loop */
    {
        /* ── Wait (blocking) for a SYN from any client ── */
        struct sockaddr_in client_addr;
        socklen_t          client_addr_len = sizeof(client_addr);
        char               synBuf[HEADER_SIZE];

        /* Blocking receive: no timeout until we have a client */
        struct timeval blocking = {0, 0};
        setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO,
                   &blocking, sizeof(blocking));

        if (recvfrom(server_socket, synBuf, HEADER_SIZE, 0,
                     (struct sockaddr *)&client_addr, &client_addr_len) < 0)
        {
            perror("recvfrom SYN");
            continue;
        }

        /* fork() so the parent can immediately loop back and accept the next client */
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); continue; }

        if (pid == 0)
        {
            /* ═══════════════════════ CHILD PROCESS ═══════════════════════
             * Handle this client's entire session. */

            /* Create a dedicated worker socket for this client */
            int                worker_socket;
            struct sockaddr_in worker_addr;
            uint16_t           worker_port;
            if (!createServerWorkerSocket(&worker_socket, &worker_addr, &worker_port))
            {
                fprintf(stderr, "[server-child] Failed to create worker socket\n");
                exit(EXIT_FAILURE);
            }

            /* Deserialize and validate the SYN packet */
            Packet clientSyn = packet_deserialize(synBuf);
            log_packet(clientSyn, serverConfig.logfilePath, Receive);

            if (!clientSyn.header.synchronizeSequence)
            {
                fprintf(stderr, "[server-child] First packet missing SYN flag, ignoring\n");
                free(clientSyn.payload);
                exit(EXIT_FAILURE);
            }

            uint32_t clientISN = clientSyn.header.sequenceNumber;
            free(clientSyn.payload);

            /* Choose our own random initial sequence number */
            srand((unsigned)time(NULL) ^ (unsigned)getpid());
            uint32_t serverISN = (uint32_t)rand();
            printf("[server-child] clientISN=%u  serverISN=%u\n", clientISN, serverISN);

            /* Build SYN|ACK response */
            Packet synAck = make_packet();
            synAck.header.sequenceNumber       = serverISN;
            synAck.header.acknowledgmentNumber  = clientISN + 1;
            synAck.header.synchronizeSequence   = 1;  /* SYN flag */
            synAck.header.acknowledgmentValid   = 1;  /* ACK flag */

            char *synAckRaw = packet_serialize(synAck);

            /* ── Wait for client's ACK, retransmit SYN|ACK on timeout ── */
            struct timeval tv = { TIMEOUT_SEC, TIMEOUT_USEC };
            setsockopt(worker_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            char     ackBuf[HEADER_SIZE];
            Packet   clientAck;
            uint32_t retries  = 0;
            bool     gotAck   = false;

            do {
                if (sendto(worker_socket, synAckRaw, HEADER_SIZE, 0,
                           (struct sockaddr *)&client_addr, client_addr_len) < 0)
                {
                    perror("sendto SYN|ACK");
                    continue;
                }
                log_packet(synAck, serverConfig.logfilePath, Send);

                if (recvfrom(worker_socket, ackBuf, HEADER_SIZE, 0,
                             (struct sockaddr *)&client_addr, &client_addr_len) < 0)
                {
                    printf("[server-child] Timeout waiting for ACK, retrying SYN|ACK...\n");
                    continue;
                }

                clientAck = packet_deserialize(ackBuf);
                log_packet(clientAck, serverConfig.logfilePath, Receive);

                /* Validate: ACK flag set, no SYN, ack == serverISN+1 */
                if (!clientAck.header.acknowledgmentValid ||
                     clientAck.header.synchronizeSequence)
                {
                    printf("[server-child] Expected ACK-only, got something else. Retrying...\n");
                    free(clientAck.payload);
                    continue;
                }
                if (clientAck.header.acknowledgmentNumber != serverISN + 1)
                {
                    printf("[server-child] ACK number mismatch. Retrying...\n");
                    free(clientAck.payload);
                    continue;
                }

                gotAck = true;
                free(clientAck.payload);
                break;
            } while (++retries < MAX_RETRIES);

            free(synAckRaw);

            if (!gotAck)
            {
                fprintf(stderr, "[server-child] Handshake failed after %u retries\n", MAX_RETRIES);
                close(worker_socket);
                exit(EXIT_FAILURE);
            }

            printf("[server-child] Handshake complete with client.\n");

            /* Hand off to the application callback (defined in lightserver.c) */
            ConnectionData cd = { &worker_addr, &client_addr, &clientISN, &serverISN };
            callback(worker_socket, serverConfig, cd);

            close(worker_socket);
            exit(EXIT_SUCCESS);
            /* ═══════════════════════ END CHILD ═══════════════════════════ */
        }

        /* Parent: immediately loop to wait for the next SYN */
        printf("[server] Spawned child PID %d for new client. Waiting for next...\n", pid);
    }

    return true; /* unreachable */
}

/* ─────────────────────────────────────────────
 * Miscellaneous
 * ───────────────────────────────────────────── */

/*
 * hash_file() — SHA-256 integrity check printed to stdout.
 * Kept from Program 2 for compatibility; not required by Program 3.
 */
void hash_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    unsigned char buf[4096];
    int n;
    while ((n = (int)fread(buf, 1, sizeof(buf), f)) > 0)
        SHA256_Update(&ctx, buf, (size_t)n);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        printf("%02x", digest[i]);
    printf("\n");

    fclose(f);
}