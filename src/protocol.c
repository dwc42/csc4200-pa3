#include "../include/protocol.h"
/*
 * protocol.c
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
Packet make_packet()
{
	Packet packet;
	packet.header.acknowledgmentNumber = 0;
	packet.header.acknowledgmentValid = 0;
	packet.header.noMoreData = 0;
	packet.header.payloadLength = 0;
	packet.header.sequenceNumber = 0;
	packet.header.synchronizeSequence = 0;
	packet.header.unused = 0;
	return packet;
}
char *packet_serialize(Packet packet)
{
	char *serializedPacket;
	//+1 for null termination
	uint64_t serializedPacketSize = HEADER_SIZE + packet.header.payloadLength + 1;
	serializedPacket = malloc(serializedPacketSize);

	uint8_t i = 0;
	uint32_t bufferNetworkInt; // store int to htonl for the serializedPacket
	bufferNetworkInt = htonl(packet.header.sequenceNumber);
	// i++ to count at runtime;
	memcpy(serializedPacket + sizeof(uint32_t) * i++, &bufferNetworkInt, sizeof(uint32_t));
	bufferNetworkInt = htonl(packet.header.acknowledgmentNumber);
	memcpy(serializedPacket + sizeof(uint32_t) * i++, &bufferNetworkInt, sizeof(uint32_t));
	bufferNetworkInt = htonl((packet.header.unused << 3) | (packet.header.acknowledgmentValid << 2) | (packet.header.synchronizeSequence << 1) | packet.header.noMoreData);
	memcpy(serializedPacket + sizeof(uint32_t) * i++, &bufferNetworkInt, sizeof(uint32_t));
	bufferNetworkInt = htonl(packet.header.payloadLength);
	memcpy(serializedPacket + sizeof(uint32_t) * i++, &bufferNetworkInt, sizeof(uint32_t));
	// printf("finished packet_serialize of header\n");
	memcpy(serializedPacket + HEADER_SIZE, packet.payload, packet.header.payloadLength);
	serializedPacket[HEADER_SIZE + packet.header.payloadLength] = '\0';
	// printf("finished packet_serialize");
	return serializedPacket;
}
Packet packet_deserialize(char *serializedPacket)
{
	// printf("run");
	Packet packet = make_packet();
	uint8_t i = 0;
	uint32_t bufferInt;
	memcpy(&bufferInt, serializedPacket + sizeof(uint32_t) * i++, sizeof(uint32_t));
	packet.header.sequenceNumber = ntohl(bufferInt);
	memcpy(&bufferInt, serializedPacket + sizeof(uint32_t) * i++, sizeof(uint32_t));
	packet.header.acknowledgmentNumber = ntohl(bufferInt);
	memcpy(&bufferInt, serializedPacket + sizeof(uint32_t) * i++, sizeof(uint32_t));
	bufferInt = ntohl(bufferInt);
	packet.header.unused = bufferInt >> 3;
	packet.header.acknowledgmentValid = bufferInt >> 2 & 0x1u;
	packet.header.synchronizeSequence = bufferInt >> 1 & 0x1u;
	packet.header.noMoreData = bufferInt & 0x1u;
	memcpy(&bufferInt, serializedPacket + sizeof(uint32_t) * i++, sizeof(uint32_t));
	packet.header.payloadLength = ntohl(bufferInt);

	// printf("payloadLength: %d,\n", packet.header.payloadLength);
	packet.payload = malloc(packet.header.payloadLength + 1);
	memcpy(packet.payload, serializedPacket + HEADER_SIZE, packet.header.payloadLength);
	packet.payload[packet.header.payloadLength] = '\0';
	return packet;
}

void printPacket(Packet packet)
{
	printf("Packet: (\n");
	printf("  acknowledgmentNumber: %d,\n", packet.header.acknowledgmentNumber);
	printf("  synchronizeSequence: %d,\n", packet.header.sequenceNumber);
	printf("  unused: %d,\n", packet.header.unused);
	printf("  acknowledgmentValid: %d,\n", packet.header.acknowledgmentValid);
	printf("  synchronizeSequence: %d,\n", packet.header.synchronizeSequence);
	printf("  noMoreData: %d,\n", packet.header.noMoreData);
	printf("  payloadLength: %d,\n", packet.header.payloadLength);
	printf("  payload: %s,\n", packet.payload);
	printf(")\n");
}

void log_packet(Packet packet, char *filePath, PacketType packetType)
{
	FILE *fptr = fopen(filePath, "a");
	if (fptr == NULL)
		return;
	char *dateString = time_stamp();
	const char *packetTypeString = packetType == Send ? "SEND" : "RECV";
	char flagsBuffer[32];
	flagsBuffer[0] = '\0';
	if (packet.header.synchronizeSequence)
		strcat(flagsBuffer, "SYN ");
	if (packet.header.acknowledgmentValid)
		strcat(flagsBuffer, "ACK ");
	if (packet.header.noMoreData)
		strcat(flagsBuffer, "FIN ");
	if (packet.header.payloadLength)
		fprintf(fptr, "[%s] %s SEQ=%u ACK=%u %s LEN=%u\n", dateString, packetTypeString, packet.header.sequenceNumber, packet.header.acknowledgmentNumber, flagsBuffer, packet.header.payloadLength);
	else
		fprintf(fptr, "[%s] %s SEQ=%u ACK=%u %s\n", dateString, packetTypeString, packet.header.sequenceNumber, packet.header.acknowledgmentNumber, flagsBuffer);
	free(dateString);
	fflush(fptr);
	fclose(fptr);
}
char *time_stamp()
{
	time_t nowEpochTime = time(NULL);
	struct tm *t = localtime(&nowEpochTime);
	char *dateString;
	dateString = malloc(20);
	// Format: YYYY-MM-DD-HH-MM-SS
	strftime(dateString, 20, "%Y-%m-%d-%H-%M-%S", t);
	return dateString;
}

ClientConfig parseClientArgs(int argc, char *argv[])
{
	ClientConfig clientConfig;
	clientConfig.logfilePath = NULL;
	clientConfig.filePath = NULL;
	clientConfig.serverIp = NULL;
	clientConfig.port = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
		{
			clientConfig.port = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
		{
			clientConfig.logfilePath = argv[++i];
		}
		else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
		{
			clientConfig.serverIp = argv[++i];
		}
		else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
		{
			clientConfig.filePath = argv[++i];
		}
	}
	return clientConfig;
}

bool createConnection(int socket_client, ClientConfig clientConfig, struct sockaddr_in *server_addr, uint32_t *client_ISN)
{

	memset(server_addr, 0, sizeof(struct sockaddr_in));
	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(clientConfig.port);
	socklen_t server_addr_len = sizeof(struct sockaddr_in);
	if (inet_pton(AF_INET, clientConfig.serverIp, &server_addr->sin_addr) <= 0)
	{
		perror("server dest ip set failed");
		return false;
	}
	struct timeval timeout = {TIMEOUT_SEC, TIMEOUT_USEC};
	if (setsockopt(socket_client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
	{
		perror("setsockopt failed");
		return false;
	}

	if (connect(socket_client, (struct sockaddr *)server_addr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("connection failed");
		return false;
	}
	printf("Success: Connected to server at %s:%d\n", clientConfig.serverIp, clientConfig.port);

	Packet packetSYN = make_packet();
	// ISN
	srand((unsigned)time(NULL) ^ getpid());
	uint32_t initialSequenceNumber = rand();
	printf("ISN: %d\n", initialSequenceNumber);
	*client_ISN = initialSequenceNumber;
	packetSYN.header.sequenceNumber = initialSequenceNumber;
	packetSYN.header.acknowledgmentNumber = 0;
	packetSYN.header.synchronizeSequence = 1;
	packetSYN.header.payloadLength = 0;
	char *serializedPacketSYN = packet_serialize(packetSYN);

	char bufferRawServerPacketSYN[HEADER_SIZE];

	uint32_t retries = 0;
	Packet serverPacketSYN;
	do
	{
		if (sendto(socket_client, serializedPacketSYN, HEADER_SIZE, 0, (struct sockaddr *)server_addr, server_addr_len) < 0)
		{
			free(serializedPacketSYN);
			close(socket_client);
			perror("SYN failed");
			return false;
		}
		log_packet(packetSYN, clientConfig.logfilePath, Send);
		printf("sent cient SYN\n");
		if (recvfrom(socket_client, bufferRawServerPacketSYN, HEADER_SIZE, 0, (struct sockaddr *)server_addr, &server_addr_len) < 0)
		{
			printf("timeout or recv failed, retransmit?\n");
			continue;
		}
		serverPacketSYN = packet_deserialize(bufferRawServerPacketSYN);
		log_packet(serverPacketSYN, clientConfig.logfilePath, Receive);
		if (!serverPacketSYN.header.synchronizeSequence || !serverPacketSYN.header.acknowledgmentValid)
		{
			printf("synchronizeSequence and acknowledgmentValid flags both not 1, retransmit?\n");
			continue;
		}
		if (serverPacketSYN.header.acknowledgmentNumber != (initialSequenceNumber + 1))
		{
			printf("acknowledgmentNumber != initialSequenceNumber + 1, retransmit?\n");
			continue;
		}
		printf("recv Server SYN\n");
		break;
	} while (++retries < MAX_RETRIES);
	free(serializedPacketSYN);
	if (retries >= MAX_RETRIES)
	{
		perror("MAX_RETRIES, closed connection");
		close(socket_client);
		return false;
	}
	Packet packetACK = make_packet();
	packetACK.header.sequenceNumber = initialSequenceNumber + 1;
	packetACK.header.acknowledgmentNumber = serverPacketSYN.header.sequenceNumber + 1;
	packetACK.header.acknowledgmentValid = 1;
	packetACK.header.payloadLength = 0;
	char *serializedPacketACK = packet_serialize(packetACK);
	if (sendto(socket_client, serializedPacketACK, HEADER_SIZE, 0, (struct sockaddr *)server_addr, server_addr_len) < 0)
	{
		free(serverPacketSYN.payload);
		free(serializedPacketACK);
		close(socket_client);
		perror("ACK failed");
		return false;
	}
	free(serializedPacketACK);
	log_packet(packetACK, clientConfig.logfilePath, Send);
	return true;
}
ServerConfig parseServerArgs(int argc, char *argv[])
{
	ServerConfig serverConfig;
	serverConfig.logfilePath = NULL;
	serverConfig.port = 0;
	serverConfig.drop = false;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
		{
			serverConfig.port = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
		{
			serverConfig.logfilePath = argv[++i];
		}
		else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
		{
			serverConfig.drop = (atoi(argv[++i]) == 1) ? true : false;
		}
	}
	return serverConfig;
}
bool startListening(int server_socket, ServerConfig serverConfig, struct sockaddr_in *server_addr, OnConnectionCallback callback)
{
	if (server_socket < 0)
	{
		perror("socket creation failed");
		return false;
	}
	// configure server address
	memset(server_addr, 0, sizeof(struct sockaddr_in));
	server_addr->sin_family = AF_INET;
	server_addr->sin_addr.s_addr = INADDR_ANY;
	server_addr->sin_port = htons(REMOTE_SERVER_PORT);
	if (bind(server_socket, (struct sockaddr *)server_addr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind failed");
		return false;
	};
	printf("Server listening on port %d...\n", serverConfig.port);
	while (1)
	{
		struct sockaddr_in client_addr;
		uint32_t clientISN;
		// resets timeout to 0
		struct timeval blocking_timeout = {0, 0};
		if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &blocking_timeout, sizeof(blocking_timeout)) < 0)
		{
			perror("setsockopt failed");
			continue;
		}
		socklen_t client_addr_len = sizeof(struct sockaddr_in);
		char bufferClientRawPacketSYN[HEADER_SIZE];
		if (recvfrom(server_socket, bufferClientRawPacketSYN, HEADER_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len) < 0)
		{
			perror("receive syc failed");
			continue;
		}

		Packet clientPacketSYN = packet_deserialize(bufferClientRawPacketSYN);
		log_packet(clientPacketSYN, serverConfig.logfilePath, Receive);

		srand((unsigned)time(NULL) ^ getpid());
		uint32_t initialSequenceNumber = rand();
		clientISN = clientPacketSYN.header.sequenceNumber;
		printf("Client ISN: %u, Server ISN: %u\n", clientISN, initialSequenceNumber);
		if (!clientPacketSYN.header.synchronizeSequence)
		{
			perror("packet.header.synchronizeSequence not 1");
			free(clientPacketSYN.payload);
			continue;
		}
		free(clientPacketSYN.payload);
		Packet packetSYN = make_packet();
		packetSYN.header.sequenceNumber = initialSequenceNumber;
		packetSYN.header.acknowledgmentNumber = clientISN + 1;
		packetSYN.header.synchronizeSequence = 1;
		packetSYN.header.acknowledgmentValid = 1;
		packetSYN.header.payloadLength = 0;
		char *serializedPacketSYN = packet_serialize(packetSYN);

		uint32_t retries = 0;
		Packet clientPacketACK;
		char bufferClientRawPacketACK[HEADER_SIZE];
		do
		{
			if (sendto(server_socket, serializedPacketSYN, HEADER_SIZE, 0, (struct sockaddr *)&client_addr, client_addr_len) < 0)
			{
				perror("send SYN failed");
				continue;
			};
			log_packet(packetSYN, serverConfig.logfilePath, Send);
			struct timeval timeout = {TIMEOUT_SEC, TIMEOUT_USEC};
			if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
			{
				perror("setsockopt failed");
				continue;
			}
			if (recvfrom(server_socket, bufferClientRawPacketACK, HEADER_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len) < 0)
			{
				perror("timeout or recv failed, retransmit?");
				continue;
			}
			clientPacketACK = packet_deserialize(bufferClientRawPacketACK);
			log_packet(clientPacketACK, serverConfig.logfilePath, Receive);
			if (!clientPacketACK.header.acknowledgmentValid || clientPacketACK.header.synchronizeSequence)
			{
				printf("synchronizeSequence not 1 or acknowledgmentValid not 0 flags, retransmit?\n");
				continue;
			}
			if (clientPacketACK.header.acknowledgmentNumber != (initialSequenceNumber + 1))
			{
				printf("acknowledgmentNumber != initialSequenceNumber+1, retransmit?\n");
				continue;
			}
			printf("recv Client ACK\n");
			break;
		} while (++retries < MAX_RETRIES);
		free(clientPacketACK.payload);
		free(serializedPacketSYN);
		if (retries >= MAX_RETRIES)
		{
			perror("MAX_RETRIES, closed connection");
			// exit(EXIT_FAILURE);
			continue;
		}
		ConnectionData connectionData = {server_addr, &client_addr, &clientISN, &initialSequenceNumber};
		callback(server_socket, serverConfig, connectionData);
		printf("Waiting for next client...\n");
	}
	return true; // should never happen
}

// googles ai wrote this
void hash_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file)
		return;

	SHA256_CTX sha256;
	SHA256_Init(&sha256);
	unsigned char buffer[4096];
	int bytesRead = 0;

	while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) != 0)
	{
		SHA256_Update(&sha256, buffer, bytesRead);
	}

	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_Final(hash, &sha256);

	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
		printf("%02x", hash[i]);
	printf("\n");

	fclose(file);
}
// int main()
// {

// 	Packet packet = make_packet();
// 	packet.payload = "Test Message";
// 	packet.header.acknowledgmentNumber = 389983;
// 	packet.header.sequenceNumber = 389983;
// 	packet.header.acknowledgmentValid = 1;
// 	packet.header.synchronizeSequence = 1;
// 	packet.header.noMoreData = 1;
// 	packet.header.payloadLength = strlen(packet.payload);
// 	printf("\n\nPacket Start: \n");
// 	printPacket(packet);
// 	char *serializedPacket = packet_serialize(packet);
// 	printf("%s\n", serializedPacket);
// 	Packet packet2 = packet_deserialize(serializedPacket);
// 	printf("\n\nPacket Back: \n");
// 	printPacket(packet2);

// 	free(serializedPacket);
// 	free(packet.payload);
// 	free(packet2.payload);
// }
