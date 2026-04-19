/*
 * protocol.h
 * CSC4200 — Program 2: TCP-Like Reliable Protocol over UDP
 *
 * This header defines the packet structure and constants for the
 * custom reliability protocol you will implement.
 *
 * DO NOT change field names, sizes, or the HEADER_SIZE constant.
 * Your serialization and deserialization must match this layout exactly.
 *
 * Packet Wire Format (all fields big-endian / network byte order):
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Sequence Number  (32 bits)                |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Acknowledgment Number (32 bits)              |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   Not Used (29 bits)                    |A|S|F|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Payload Length (32 bits)                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Payload (variable)                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Flag bits (low 3 bits of the flags field):
 *   Bit 0 (F) — FIN  : No more data from sender; initiate teardown
 *   Bit 1 (S) — SYN  : Synchronize sequence numbers (handshake)
 *   Bit 2 (A) — ACK  : Acknowledgment Number field is valid
 */
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
#endif
typedef enum PacketType
{
	Send = 0,
	Receive = 1,
} PacketType;
typedef struct PacketHeader
{
	uint32_t sequenceNumber : 32;
	uint32_t acknowledgmentNumber : 32;
	uint32_t unused : 29;
	uint8_t acknowledgmentValid : 1;
	uint8_t synchronizeSequence : 1;
	uint8_t noMoreData : 1;
	uint32_t payloadLength : 32;
} PacketHeader;
#define HEADER_SIZE 4 * sizeof(uint32_t)
#define TIMEOUT_SEC 10L
#define TIMEOUT_USEC 0L

#define REMOTE_SERVER_IP "10.128.0.3"
#define REMOTE_SERVER_PORT 5000
#define BACKLOG_SIZE 5
#define MAX_RETRIES 5
#define MAX_PAYLOAD 1024 - HEADER_SIZE
typedef struct Packet
{
	PacketHeader header;
	char *payload;
} Packet;

Packet make_packet();
char *packet_serialize(Packet packet);
Packet packet_deserialize(char *serializedPacket);

void printPacket(Packet packet);
void log_packet(Packet packet, char *filePath, PacketType packetType);
char *time_stamp();
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