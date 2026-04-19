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
	uint32_t expected_seq = *connectionData.client_isn + 1;
	uint32_t retries = 0;
	socklen_t client_addr_len = sizeof(struct sockaddr_in);
	char fileName[255] = {'\0'};
	bool wroteAnyData = false;
	bool timedOut = false;
	while (1)
	{
		retries = 0;
		bool shouldBreak = false;
		do
		{
			char filePacketBufferRaw[MAX_PAYLOAD + HEADER_SIZE];
			if (recvfrom(server_socket, filePacketBufferRaw, MAX_PAYLOAD + HEADER_SIZE, 0, (struct sockaddr *)connectionData.client_addr, &client_addr_len) < 0)
			{
				perror("recv packet failed, retransmit?");
				continue;
			}
			Packet filePacket = packet_deserialize(filePacketBufferRaw);
			if (filePacket.header.noMoreData)
			{
				char clientAddressString[INET_ADDRSTRLEN];
				if (inet_ntop(AF_INET, &connectionData.client_addr->sin_addr, clientAddressString, sizeof(clientAddressString)) == NULL)
				{
					perror("inet_ntop failed");
					strcpy(clientAddressString, "unknown");
				}
				printf("Interaction with %s completed.\n", clientAddressString);
				hash_file(fileName);
				Packet finishedACKPacket = make_packet();
				finishedACKPacket.header.acknowledgmentValid = 1;
				finishedACKPacket.header.noMoreData = 1;
				finishedACKPacket.header.sequenceNumber = *connectionData.server_isn;
				finishedACKPacket.header.acknowledgmentNumber = filePacket.header.sequenceNumber + 1;

				char *finishedACKPacketRaw = packet_serialize(finishedACKPacket);
				sendto(server_socket, finishedACKPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)connectionData.client_addr, client_addr_len);
				log_packet(finishedACKPacket, serverConfig.logfilePath, Send);
				free(finishedACKPacketRaw);
				free(filePacket.payload);
				shouldBreak = true;
				break;
			}
			log_packet(filePacket, serverConfig.logfilePath, Receive);
			Packet acknowledgementPacket = make_packet();
			bool retransmit = false;
			if (expected_seq != filePacket.header.sequenceNumber)
			{
				printf("expected_seq != filePacket.header.sequenceNumber\n");
				acknowledgementPacket.header.acknowledgmentNumber = expected_seq;
				retransmit = true;
				free(filePacket.payload);
			}
			else
			{
				const char *fileflag = wroteAnyData ? "ab" : "wb";
				acknowledgementPacket.header.acknowledgmentNumber = expected_seq + filePacket.header.payloadLength;
				expected_seq += filePacket.header.payloadLength;
				const char *fileNameTag = "FILENAME:";
				size_t fileNameTagLength = strlen(fileNameTag);
				size_t payloadLength = filePacket.header.payloadLength;
				if (payloadLength < fileNameTagLength + 1)
				{
					free(filePacket.payload);
					printf("payload too short for FILENAME header\n");
					return;
				}
				if (memcmp(filePacket.payload, fileNameTag, fileNameTagLength) != 0)
				{
					free(filePacket.payload);
					printf("failed to find FILENAME:\n");
					return;
				}
				char *fileNameStart = filePacket.payload + fileNameTagLength;
				size_t remainderSize = payloadLength - fileNameTagLength;
				char *fileNameEnd = memchr(fileNameStart, '\0', remainderSize);
				if (fileNameEnd == NULL)
				{
					free(filePacket.payload);
					printf("missing filename terminator\n");
					return;
				}
				size_t fileNameLength = (size_t)(fileNameEnd - fileNameStart);
				if (fileNameLength >= sizeof(fileName))
					fileNameLength = sizeof(fileName) - 1;
				memcpy(fileName, fileNameStart, fileNameLength);
				fileName[fileNameLength] = '\0';
				if (!wroteAnyData)
					printf("FILENAME: %s\n", fileName);

				char *correctPayload = fileNameEnd + 1;
				FILE *filePtr = fopen(fileName, fileflag);
				if (filePtr == NULL)
				{
					free(filePacket.payload);
					printf("file open failed\n");
					return;
				}
				size_t correctPayloadLength = payloadLength - (size_t)(correctPayload - filePacket.payload);
				if (fwrite(correctPayload, 1, correctPayloadLength, filePtr) != correctPayloadLength)
				{
					fclose(filePtr);
					free(filePacket.payload);
					printf("file write failed\n");
					return;
				}
				fflush(filePtr);
				fclose(filePtr);
				wroteAnyData = true;
				free(filePacket.payload);
			}
			acknowledgementPacket.header.sequenceNumber = *connectionData.server_isn;
			acknowledgementPacket.header.acknowledgmentValid = 1;
			char *acknowledgementPacketRaw = packet_serialize(acknowledgementPacket);
			if (sendto(server_socket, acknowledgementPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)connectionData.client_addr, client_addr_len) < 0)
			{
				free(acknowledgementPacketRaw);
				printf("acknowledgementPacketRaw sendto failed\n");
				continue;
			}
			free(acknowledgementPacketRaw);
			if (retransmit)
				continue;
			break;
		} while (++retries < MAX_RETRIES);
		if (retries >= MAX_RETRIES)
		{
			timedOut = true;
			break;
		}
		if (shouldBreak)
			break;
	}
	if (timedOut)
	{
		printf("failed to receive file: Timed Out\n");
		return;
	}

	// char finishedPacketRaw[HEADER_SIZE];
	// retries = 0;
	// do
	// {
	// 	if (recvfrom(server_socket, finishedPacketRaw, HEADER_SIZE, 0, (struct sockaddr *)connectionData.client_addr, &client_addr_len) < 0)
	// 	{
	// 		// perror("recv packet failed, retransmit?");
	// 		continue;
	// 	}
	// 	finishedPacket = packet_deserialize(finishedPacketRaw);
	// 	if (!finishedPacket.header.noMoreData)
	// 	{
	// 		free(finishedPacket.payload);
	// 		printf("finishedPacket.header.noMoreData not 1\n");
	// 		continue;
	// 	}
	// 	free(finishedPacket.payload);

	// } while (++retries < MAX_RETRIES);
	// if (retries >= MAX_RETRIES)
	// {
	// 	printf("failed to receive finished packet\n");
	// 	return;
	// }
}

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