#include "../include/protocol.h"

void onConnectionCallback(int server_socket, ServerConfig serverConfig, ConnectionData connectionData)
{
	printf("Handshake complete.\n");


	if (serverConfig.drop)
	{
		char bufferDrop[255];
		sprintf(bufferDrop, "sudo iptables -A INPUT -p udp --dport %d -j DROP", serverConfig.port);
		system(bufferDrop);
	}

	struct timeval timeout = {TIMEOUT_SEC, TIMEOUT_USEC};

	if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
	{
		perror("setsockopt failed");
		return;
	}

	uint32_t retries = 0;
	bool timedOut = false;

    
	socklen_t client_addr_len = sizeof(struct sockaddr_in);
	uint32_t expected_seq = connectionData.client_isn + 1;

    uint16_t blink_duration = 0;
    uint16_t blink_count = 0;

    char buffer[MAX_PAYLOAD + HEADER_SIZE];

    Packet pkt;
    retries = 0;


    // Receives packet
    do {
        if (recvfrom(server_socket, buffer, sizeof(buffer), 0,
            (struct sockaddr *)connectionData.client_addr, &addr_len) < 0)
        {
            perror("recv blink params failed");
            continue;
        }

        pkt = packet_deserialize(buffer);
        log_packet(pkt, serverConfig.logfilePath, Receive);

        if (!pkt.header.ack) {
            printf("Expected ACK packet with blink params\n");
            free(pkt.payload);
            continue;
        }

        break;

    } while (++retries < MAX_RETRIES);

    if (retries >= MAX_RETRIES) {
        printf("Timed out receiving blink params\n");
        return;
    }


    // Extract blink params
    memcpy(&blink_duration, pkt.payload, sizeof(uint16_t));
    memcpy(&blink_count, pkt.payload + sizeof(uint16_t), sizeof(uint16_t));

    blink_duration = ntohs(blink_duration);
    blink_count = ntohs(blink_count);

    expected_seq += 4;

    free(pkt.payload);


    // Send ACK + ECHO
    Packet ack_pkt = make_packet();
    ack_pkt.header.sequenceNumber = connectionData.server_isn;
    ack_pkt.header.acknowledgmentNumber = expected_seq;
    ack_pkt.header.ack = 1;


    // Reads blink duration and count
    uint16_t payload[2];
    payload[0] = htons(blink_duration);
    payload[1] = htons(blink_count);

    char *raw = packet_serialize_with_payload(ack_pkt, payload, 4);

    sendto(server_socket, raw, HEADER_SIZE + 4, 0, 
        (struct sockaddr *)connectionData.client_addr, addr_len);

    log_packet(ack_pkt, serverConfig.logfilePath, Send);
    free(raw);

    printf("Blink params: duration=%d ms, count=%d\n", blink_duration, blink_count);


    
    // Wait for motion
    while (1) {

        retries = 0;
        bool received = false;

        Packet motion_pkt;

        // Receive motion packet
        do {
            if (recvfrom(server_socket, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)connectionData.client_addr, &addr_len) < 0)
            {
                perror("recv failed");
                continue;
            }
            
            
            motion_pkt = packet_deserialize(buffer);
            log_packet(motion_pkt, serverConfig.logfilePath, Receive);

            received = true;
            break;
            
        } while (++retries < MAX_RETRIES);


        if (!received) {
            printf("Timeout waiting for motion/FIN\n");
            break;
        }


        // FIN
        if (motion_pkt.header.fin)
        {
            printf("Interaction completed.\n");
            free(motion_pkt.payload);
            break;
        }

        // Motion detected
        if (motion_pkt.payload &&
            strcmp((char *)motion_pkt.payload, ":MotionDetected") == 0)
        {
            printf("Motion detected! Blinking LED...\n");

            // Function for printing LED
            print_led(blink_count, blink_duration);
        }

        free(motion_pkt.payload);
    }

}


void print_led(uint16_t blink_count, uint16_t blink_duration) {
    //
}


//** POTENTIAL CODE FOR HAVING MULTIPLE CLIENTS *//

/*
    typedef enum {
    STATE_HANDSHAKE,
    STATE_WAIT_PARAMS,
    STATE_WAIT_MOTION,
    STATE_DONE
} ClientStateEnum;

typedef struct {
    struct sockaddr_in addr;

    uint32_t client_isn;
    uint32_t server_isn;
    uint32_t expected_seq;

    uint16_t blink_duration;
    uint16_t blink_count;

    ClientStateEnum state;

    time_t last_active;   // for timeout handling
    int active;
} ClientState;

#define MAX_CLIENTS 50
ClientState clients[MAX_CLIENTS];

int find_client(struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active &&
            clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_port == addr->sin_port) {
            return i;
        }
    }
    return -1;
}

int create_client(struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].addr = *addr;
            clients[i].active = 1;
            clients[i].state = STATE_HANDSHAKE;
            clients[i].last_active = time(NULL);
            return i;
        }
    }
    return -1;
}

while (1)
{
    char buffer[MAX_PAYLOAD + HEADER_SIZE];
    struct sockaddr_in sender;
    socklen_t addr_len = sizeof(sender);

    int n = recvfrom(server_socket, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)&sender, &addr_len);

    if (n < 0) continue;

    Packet pkt = packet_deserialize(buffer);

    int idx = find_client(&sender);
    if (idx == -1) {
        idx = create_client(&sender);
        if (idx == -1) continue; // no space
    }

    ClientState *c = &clients[idx];
    c->last_active = time(NULL);

    log_packet(pkt, logfile, Receive);

    // ---------------- STATE MACHINE ---------------- 

    switch (c->state) {
        case STATE_HANDSHAKE:

        if (pkt.header.syn && !pkt.header.ack)
        {
            c->client_isn = pkt.header.sequenceNumber;
            c->server_isn = rand();

            Packet syn_ack = make_packet();
            syn_ack.header.syn = 1;
            syn_ack.header.ack = 1;
            syn_ack.header.sequenceNumber = c->server_isn;
            syn_ack.header.acknowledgmentNumber = c->client_isn + 1;

            send_packet(server_socket, &sender, syn_ack);
            log_packet(syn_ack, logfile, Send);
        }
        else if (pkt.header.ack)
        {
            c->expected_seq = pkt.header.sequenceNumber;
            c->state = STATE_WAIT_PARAMS;
        }

        break;

    case STATE_WAIT_PARAMS:

        if (pkt.header.ack && pkt.payload)
        {
            memcpy(&c->blink_duration, pkt.payload, 2);
            memcpy(&c->blink_count, pkt.payload + 2, 2);

            c->blink_duration = ntohs(c->blink_duration);
            c->blink_count = ntohs(c->blink_count);

            c->expected_seq += 4;

            //* Send ACK + echo 
            Packet ack = make_packet();
            ack.header.ack = 1;
            ack.header.sequenceNumber = c->server_isn;
            ack.header.acknowledgmentNumber = c->expected_seq;

            uint16_t payload[2] = {
                htons(c->blink_duration),
                htons(c->blink_count)
            };

            send_packet_with_payload(server_socket, &sender, ack, payload, 4);
            log_packet(ack, logfile, Send);

            c->state = STATE_WAIT_MOTION;
        }

        break;

    case STATE_WAIT_MOTION:

        if (pkt.header.fin)
        {
            printf("Client finished.\n");
            c->state = STATE_DONE;
            break;
        }

        if (pkt.payload &&
            strcmp((char *)pkt.payload, ":MotionDetected") == 0)
        {
            printf("Motion detected from client!\n");

            for (int i = 0; i < c->blink_count; i++)
            {
                digitalWrite(LED_PIN, HIGH);
                delay(c->blink_duration);
                digitalWrite(LED_PIN, LOW);
                delay(c->blink_duration);
            }
        }

        break;

    case STATE_DONE:
        c->active = 0; // free slot
        break;

    for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].active &&
        time(NULL) - clients[i].last_active > 10) {
        printf("Client timed out\n");
        clients[i].active = 0;
    }
}
*/

/**
 * 1. Create a UDP socket, bind to port.
2. Wait for SYN (blocking `recvfrom` — no timeout yet).
3. Validate `FLAG_SYN` is set. Ignore other packets.
4. Generate your own random ISN.
5. Send SYN|ACK (seq = server_isn, ack = client_isn + 1).
6. Set `SO_RCVTIMEO`. Wait for ACK.
7. If timeout, retransmit SYN|ACK.
8. On valid ACK: print "Handshake complete", log the event.
 */
int main(int argc, char *argv[])
{

	ServerConfig serverConfig = parseServerArgs(argc, argv);
	if (serverConfig.logfilePath == NULL)
	{
		printf("serverConfig.logfilePath == NULL");
		exit(EXIT_FAILURE);
	}
	// else if (serverConfig.port < 1024)
	// {
	// 	printf("serverConfig.port < 1024");
	// 	exit(EXIT_FAILURE);
	// }

	int server_socket;
	struct sockaddr_in server_addr;
	server_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (!startListening(server_socket, serverConfig, &server_addr, onConnectionCallback))
	{
		printf("failed to startListening\n");
		exit(EXIT_FAILURE);
	};
}