#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <openssl/sha.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

/* * The PacketHeader must contain all fields used by the logic in protocol.c.
 * Note: These are internal struct fields; the serialization logic in 
 * protocol.c handles the bit-packing for the wire format.
 */
typedef struct PacketHeader
{
    uint32_t sequenceNumber;
    uint32_t acknowledgmentNumber;
    uint32_t unused;              /* 29 bits in wire format */
    uint8_t  acknowledgmentValid; /* Bit 2: ACK */
    uint8_t  synchronizeSequence; /* Bit 1: SYN */
    uint8_t  noMoreData;          /* Bit 0: FIN */
    uint32_t payloadLength;       /* Serialized as the 4th 32-bit word */
} PacketHeader;

/* * The Packet struct contains the header and the actual data buffer.
 */
typedef struct Packet
{
    PacketHeader header;
    char *payload;
} Packet;

/* * Constants 
 * HEADER_SIZE is 16 bytes (4 slots * 4 bytes): Seq, Ack, Flags, Len 
 */
#define HEADER_SIZE (4 * sizeof(uint32_t))
#define MAX_PAYLOAD 1024
#define TIMEOUT_SEC 10L
#define TIMEOUT_USEC 0L

#define REMOTE_SERVER_IP "10.128.0.3"
#define REMOTE_SERVER_PORT 5000
#define BACKLOG_SIZE 5
#define MAX_RETRIES 5

typedef enum PacketType
{
    Send = 0,
    Receive = 1,
} PacketType;

/* Function Prototypes */
Packet make_packet(void);
char *packet_serialize(Packet packet);
Packet packet_deserialize(char *buf);

void printPacket(Packet packet);
void log_packet(Packet packet, char *filePath, PacketType packetType);
char *time_stamp(void);

typedef struct ClientConfig
{
    char *serverIp;
    uint16_t port;
    char *logfilePath;
    char *filePath;
} ClientConfig;

ClientConfig parseClientArgs(int argc, char *argv[]);
bool createConnection(int socket_client, ClientConfig clientConfig, struct sockaddr_in *server_addr, uint32_t *client_ISN);

typedef struct ServerConfig
{
    uint16_t port;
    char *logfilePath;
    bool drop;
} ServerConfig;

ServerConfig parseServerArgs(int argc, char *argv[]);

typedef struct ConnectionData
{
    struct sockaddr_in *server_addr;
    struct sockaddr_in *client_addr;
    uint32_t *client_isn;
    uint32_t *server_isn;
} ConnectionData;

typedef void OnConnectionCallback(int server_socket, ServerConfig serverConfig, ConnectionData connectionData);
bool startListening(int server_socket, ServerConfig serverConfig, struct sockaddr_in *server_addr, OnConnectionCallback callback);
void hash_file(const char *path);

#endif // PROTOCOL_H_