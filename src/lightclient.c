#include "../include/protocol.h"
int client(int argc, char *argv[]);
/**
 * 1. Create a UDP socket with `socket(AF_INET, SOCK_DGRAM, 0)`.
2. Set `SO_RCVTIMEO` to `{TIMEOUT_SEC, TIMEOUT_USEC}`.
3. Generate a random ISN: seed with `srand((unsigned)time(NULL) ^ getpid())`, then call `rand()`.
4. Send a SYN packet (flags = `FLAG_SYN`, seq = ISN, ack = 0).
5. Wait for SYN|ACK. Validate: `FLAG_SYN | FLAG_ACK` both set, `ack_num == client_isn + 1`.
6. If timeout or invalid, retransmit SYN (up to `MAX_RETRIES`).
7. Send ACK (flags = `FLAG_ACK`, seq = client_isn + 1, ack = server_isn + 1).
8. Print "Handshake complete."
 */
int main(int argc, char *argv[])
{
	int pid = fork();
	if (!pid)
	{
		printf("child: %d\n", pid);
		client(argc, argv);
	}
	else
	{
		printf("parent: %d\n", pid);
		client(argc, argv);
	}
}
int client(int argc, char *argv[])
{
	ClientConfig clientConfig = parseClientArgs(argc, argv);
	if (clientConfig.logfilePath == NULL)
	{
		printf("clientConfig.logfilePath == NULL");
		exit(EXIT_FAILURE);
	}
	// else if (clientConfig.port < 1024)
	// {
	// 	printf("clientConfig.port < 1024");
	// 	exit(EXIT_FAILURE);
	// }
	else if (clientConfig.serverIp == NULL)
	{
		printf("clientConfig.serverIp == NULL");
		exit(EXIT_FAILURE);
	}
	else if (clientConfig.filePath == NULL)
	{
		printf("clientConfig.filePath == NULL");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server_addr;
	uint32_t client_ISN;
	// struct sockaddr_in local_addr;

	int socket_client = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_client < 0)
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	if (!createConnection(socket_client, clientConfig, &server_addr, &client_ISN))
	{
		// connection is already closed by this point;
		printf("Handshake failed.\n");
		exit(EXIT_FAILURE);
	};
	printf("Handshake complete.\n");
	// printf("test\n");
	struct timeval timeout = {TIMEOUT_SEC, TIMEOUT_USEC};
	if (setsockopt(socket_client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
	{
		perror("setsockopt failed");
		return false;
	}

	socklen_t server_addr_len = sizeof(struct sockaddr_in);
	FILE *filePtr = fopen(clientConfig.filePath, "rb");

	if (filePtr == NULL)
	{
		printf("file not found\n");
		close(socket_client);
		exit(EXIT_FAILURE);
	}
	printf("file found at: %s\n", clientConfig.filePath);
	hash_file(clientConfig.filePath);
	char *filePathToSend;
	const char *fileNameTag = "FILENAME:";
	const uint32_t fileNameTagLength = strlen(fileNameTag);
	const uint32_t filePathToSendLength = strlen(clientConfig.filePath) + fileNameTagLength + 1;

	filePathToSend = malloc(filePathToSendLength);
	memcpy(filePathToSend, fileNameTag, fileNameTagLength);
	memcpy(filePathToSend + fileNameTagLength, clientConfig.filePath, strlen(clientConfig.filePath));
	filePathToSend[filePathToSendLength - 1] = '\0';
	const uint32_t newMaxPayloadSize = MAX_PAYLOAD - filePathToSendLength;
	char payloadBuffer[MAX_PAYLOAD];
	memcpy(payloadBuffer, filePathToSend, filePathToSendLength);
	int ch;
	uint32_t retries = 0;
	uint32_t totalFileByteCount = 0;
	uint32_t currentPayloadChunkSize = filePathToSendLength;
	uint32_t currentSequenceNumber = client_ISN + 1;
	while ((ch = fgetc(filePtr)) != EOF)
	{
		payloadBuffer[currentPayloadChunkSize] = (char)ch;
		currentPayloadChunkSize++;
		totalFileByteCount++;
		if (currentPayloadChunkSize >= newMaxPayloadSize)
		{
			Packet packet = make_packet();
			packet.header.sequenceNumber = currentSequenceNumber;
			packet.header.acknowledgmentNumber = client_ISN + currentPayloadChunkSize;
			packet.header.payloadLength = currentPayloadChunkSize;
			packet.payload = malloc(currentPayloadChunkSize + 1);
			packet.payload[currentPayloadChunkSize] = '\0';
			memcpy(packet.payload, payloadBuffer, currentPayloadChunkSize);
			char *serializedPacket = packet_serialize(packet);
			do
			{
				if (sendto(socket_client, serializedPacket, HEADER_SIZE + currentPayloadChunkSize, 0, (struct sockaddr *)&server_addr, server_addr_len) < 0)
				{
					close(socket_client);
					perror("send packet failed");
					exit(EXIT_FAILURE);
				}
				log_packet(packet, clientConfig.logfilePath, Send);
				char acknowledgementPacketRaw[HEADER_SIZE];
				if (recvfrom(socket_client, acknowledgementPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, &server_addr_len) < 0)
				{
					perror("recvfrom packet failed or timed out");
					continue;
				}
				Packet acknowledgementPacket = packet_deserialize(acknowledgementPacketRaw);
				log_packet(acknowledgementPacket, clientConfig.logfilePath, Receive);
				if (acknowledgementPacket.header.acknowledgmentNumber != currentSequenceNumber + currentPayloadChunkSize)
				{
					free(acknowledgementPacket.payload);
					printf("acknowledgementPacket.header.acknowledgmentNumber != currentSequenceNumber + currentPayloadChunkSize\n");
					continue;
				}
				free(acknowledgementPacket.payload);
				break;

			} while (++retries < MAX_RETRIES);
			free(serializedPacket);
			free(packet.payload);
			if (retries >= MAX_RETRIES)
			{
				printf("failed to send file");
				break;
			}

			currentSequenceNumber += currentPayloadChunkSize;
			memcpy(payloadBuffer, filePathToSend, filePathToSendLength);
			currentPayloadChunkSize = filePathToSendLength;
			retries = 0;
		}
	}
	if (currentPayloadChunkSize > filePathToSendLength)
	{
		retries = 0;
		Packet packet = make_packet();
		packet.header.sequenceNumber = currentSequenceNumber;
		packet.header.acknowledgmentNumber = client_ISN + currentPayloadChunkSize;
		packet.header.payloadLength = currentPayloadChunkSize;
		packet.payload = malloc(currentPayloadChunkSize + 1);
		packet.payload[currentPayloadChunkSize] = '\0';
		memcpy(packet.payload, payloadBuffer, currentPayloadChunkSize);
		char *serializedPacket = packet_serialize(packet);
		do
		{
			if (sendto(socket_client, serializedPacket, HEADER_SIZE + currentPayloadChunkSize, 0, (struct sockaddr *)&server_addr, server_addr_len) < 0)
			{
				close(socket_client);
				perror("send packet failed");
				exit(EXIT_FAILURE);
			}
			log_packet(packet, clientConfig.logfilePath, Send);
			char acknowledgementPacketRaw[HEADER_SIZE];
			if (recvfrom(socket_client, acknowledgementPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, &server_addr_len) < 0)
			{
				perror("recvfrom packet failed or timed out");
				continue;
			}
			Packet acknowledgementPacket = packet_deserialize(acknowledgementPacketRaw);
			log_packet(acknowledgementPacket, clientConfig.logfilePath, Receive);
			if (acknowledgementPacket.header.acknowledgmentNumber != currentSequenceNumber + currentPayloadChunkSize)
			{
				free(acknowledgementPacket.payload);
				printf("acknowledgementPacket.header.acknowledgmentNumber != currentSequenceNumber + currentPayloadChunkSize\n");
				continue;
			}
			free(acknowledgementPacket.payload);
			break;

		} while (++retries < MAX_RETRIES);
		free(serializedPacket);
		free(packet.payload);
		if (retries >= MAX_RETRIES)
		{
			free(filePathToSend);
			close(socket_client);
			printf("failed to send file\n");
			exit(EXIT_FAILURE);
		}
		else
		{

			currentSequenceNumber += currentPayloadChunkSize;
			currentPayloadChunkSize = filePathToSendLength;
		}
	}
	Packet finishedPacket = make_packet();
	finishedPacket.header.sequenceNumber = currentSequenceNumber;
	finishedPacket.header.noMoreData = 1;
	char *finishedPacketRaw = packet_serialize(finishedPacket);
	Packet finishedACKPacket;
	retries = 0;
	do
	{
		if (sendto(socket_client, finishedPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, server_addr_len) < 0)
		{
			printf("sendto finishedPacketRaw failed\n");
			continue;
		}
		log_packet(finishedPacket, clientConfig.logfilePath, Send);
		char finishedACKPacketRaw[HEADER_SIZE];
		if (recvfrom(socket_client, finishedACKPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)&server_addr, &server_addr_len) < 0)
		{
			printf("recvfrom finishedACKPacketRaw failed\n");
			continue;
		}

		finishedACKPacket = packet_deserialize(finishedACKPacketRaw);
		log_packet(finishedACKPacket, clientConfig.logfilePath, Receive);
		if (!finishedACKPacket.header.acknowledgmentValid || !finishedACKPacket.header.noMoreData)
		{
			free(finishedACKPacket.payload);
			printf("finishedACKPacket: acknowledgmentValid and noMoreData flags both not 1\n");
			continue;
		}
		free(finishedACKPacket.payload);
		break;
	} while (++retries < MAX_RETRIES);
	if (retries >= MAX_RETRIES)
	{
		free(finishedPacketRaw);
		free(filePathToSend);
		close(socket_client);
		printf("failed to send file\n");
		exit(EXIT_FAILURE);
	}
	printf("File Sent\nConnection closed cleanly.\n");
	free(finishedPacketRaw);
	free(filePathToSend);
	close(socket_client);
}