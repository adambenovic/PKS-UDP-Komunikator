#include <stdlib.h>
#include <io.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>
#include <fcntl.h>

#pragma comment(lib, "ws2_32.lib")

#define TIMEOUT_SENDER 10
#define TIMEOUT_RECEIVER 20
#define MB 1048576
#define HEADER_SIZE 20
#define FILE_FLAG 1
#define MESSAGE_FLAG 0
#define END_FLAG 2
#define KEEP_ALIVE 3
#define MIN_SIZE 64
#define MAX_SIZE 1452

int timeout_sender = TIMEOUT_SENDER, timeout_receiver = TIMEOUT_RECEIVER;
int sendMore = 0;

void intToBytes(int value, char *bytes, int start) {
	for (int i = 0; i < 4; i++) {
		bytes[i + start] = value >> i * 8;
	}
}

int bytesToInt(char *bytes, int start) {
	int value = 0;
	for (int i = 3; i >= 0; i--) {
		value |= (unsigned char)bytes[i + start] << i * 8;
	}
	return value;
}

int recvfromTimeOutUDP(SOCKET socket, long sec, long usec) {
	struct timeval timeout;
	struct fd_set fds;	
	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	FD_ZERO(&fds);
	FD_SET(socket, &fds);

	return select(0, &fds, 0, 0, &timeout);
}

int makeChecksum(char *arr, int length) {
	int checksum = 0;

	for (int i = 0; i < length; i++)
		checksum += arr[i] ^ 1;

	return checksum;
}

int checkChecksum(int checksum, char *arr, int length) {
	for (int i = 0; i < length; i++)
		checksum -= arr[i] ^ 1;

	if (checksum == 0)
		return 1;

	return 0;
}

char **writeHeads(int type, char **fragments, int position, int fragmentLength) {
	int fragmentsSize = (MB / fragmentLength) + 1;

	char  **toBeSent = malloc(fragmentsSize * sizeof(char *));
	for (int i = 0; i < fragmentsSize; i++)
		toBeSent[i] = malloc(fragmentLength * sizeof(char));

	for (int i = 0; i < position; i++) {
		int checksum = makeChecksum(fragments[i], fragmentLength);
		intToBytes(i, toBeSent[i], 0);						//index
		intToBytes(checksum, toBeSent[i], 4);				//checksum
		intToBytes(position,toBeSent[i], 8);				//number of Fragments
		intToBytes(type, toBeSent[i], 12);					//data type
		intToBytes(fragmentLength, toBeSent[i], 16);		//data size
		for (int j = HEADER_SIZE; j < fragmentLength + HEADER_SIZE; j++)
			toBeSent[i][j] = fragments[i][j - HEADER_SIZE];
	}

	return toBeSent;
}

int fragmentMessage(char **toBeSent, char *message, int fragmentLength) {
	int readLen, position = 0, messLen = strlen(message);

	for (int i = 0; i < messLen; i += fragmentLength) {
		for (int j = 0; j < fragmentLength; j++)
			toBeSent[position][j] = message[i + j];
		position++;
	}

	return position;
}

int fragmentFile(char **toBeSent, char *fileName, int fragmentLength) {
	int f = open(fileName, O_RDONLY), readLen, position = 0;

	if (f < 0) {
		printf("File does not exist\n");
		return 0;
	}

	while ((readLen = read(f, toBeSent[position], fragmentLength)) > 0)
		position++;

	return position;
}

int sendFile(SOCKET sendingSocket, int fragmentLength, SOCKADDR_IN receiverAddress, char *fileName) {
	int sendLength = fragmentLength + HEADER_SIZE, fragmentsSize = (MB / fragmentLength) + 1, fileNameFragmentsSize;
	char *buf = malloc(sendLength * sizeof(char));

	char  **fragments = malloc(fragmentsSize * sizeof(char *));
	for (int i = 0; i < fragmentsSize; i++)
		fragments[i] = malloc(fragmentLength * sizeof(char));

	char  **fragmentsName = malloc(fragmentsSize * sizeof(char *));
	for (int i = 0; i < fragmentsSize; i++)
		fragmentsName[i] = malloc(fragmentLength * sizeof(char));

	if (!(fragmentsSize = fragmentFile(&*fragments, fileName, fragmentLength)))
		return 0;

	char **toBeSent = writeHeads(FILE_FLAG, fragments, fragmentsSize, fragmentLength);

	if(!(fileNameFragmentsSize = fragmentMessage(fragmentsName, fileName, fragmentLength)))
		printf("filename fragment error\n");

	char **fileNameSend = writeHeads(FILE_FLAG, fragmentsName, fileNameFragmentsSize, fragmentLength);
		
	for (int i = 0; i < fileNameFragmentsSize; i++)
		if (!sendto(sendingSocket, fileNameSend[i], fragmentLength + HEADER_SIZE, 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)))
			printf("filename sending error %s\n",fileNameSend+i);
		else
			printf("filename fragment no.%d sent\n", i);

	for (int i = 0; i < fragmentsSize; i++)
		if (!sendto(sendingSocket, toBeSent[i], fragmentLength + HEADER_SIZE, 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)))
			printf("sending error %d\n", i);
		else {
			printf("fragment no.%d sent\n", i);
			Sleep(5);
		}

	return 1;
}

int sendMessage(SOCKET sendingSocket, SOCKADDR_IN receiverAddress, int fragmentLength, char *messageToSend) {
	int sendLength = fragmentLength + HEADER_SIZE, fragmentsSize = (MB / fragmentLength) + 1;
	char *buf = malloc(sendLength * sizeof(char));

	char  **fragments = malloc(fragmentsSize * sizeof(char *));
	for (int i = 0; i < fragmentsSize; i++)
		fragments[i] = malloc(fragmentLength * sizeof(char));

	fragmentsSize = fragmentMessage(fragments, messageToSend, fragmentLength);

	char **toBeSent = writeHeads(MESSAGE_FLAG, fragments, fragmentsSize, fragmentLength);

	for (int i = 0; i < fragmentsSize; i++)
		if (!sendto(sendingSocket, toBeSent[i], fragmentLength + HEADER_SIZE, 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)))
			printf("sending error %d\n", i);
		else
			printf("sent! %d\n", i);

	return 1;
}

int sendKeepAlive(SOCKET sendingSocket, SOCKADDR_IN receiverAddress) {
	char *toBeSent = malloc(HEADER_SIZE * sizeof(char));
	intToBytes(KEEP_ALIVE, toBeSent, 12);					//data type
	if (sendto(sendingSocket, toBeSent, HEADER_SIZE, 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)))
		return 1;
	return 0;
}

ThreadFunction(LPVOID param) {
	while (1) {
		if (getchar() == 'c') {
			sendMore = 1;
			_endthread();
		}
	}
}


void senderPart() {
	WSADATA wsaData;
	SOCKET sendingSocket;
	SOCKADDR_IN receiverAddress;
	int port, fragmentLength, sendType;
	char *messageToSend, destinationIP[16], fileName[50];

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("WSAStartup failed with error %ld\n", WSAGetLastError());		
		WSACleanup();
		getchar();

		return -1;
	}

	sendingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sendingSocket == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		WSACleanup();
		getchar();

		return -1;
	}

	printf("Sending socket is open.\n");
	/*
	printf("Zadajte IP prijimatela:\n");
	scanf("%s", destinationIP);
	printf("ip: %s\n", destinationIP);

	printf("Zadajte port na prijimanie odpovedi:\n");
	scanf("%d", &port);
	printf("port: %d\n", port);
	*/
	receiverAddress.sin_family = AF_INET;
	receiverAddress.sin_port = htons(5150);
	receiverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

	int timeout = TIMEOUT_SENDER;
	while (1) {		
		/*printf("Zadajte velkost fragmentu(64-1452):\n");
		scanf("%d", &fragmentLength);
		printf("fragment: %d\n", fragmentLength);		
		*/

		fragmentLength = 1452;

		printf("Chcete posielat spravu(1) alebo subor(2):\n");
		scanf("%d", &sendType);
		printf("sendType: %d\n", sendType);
		
		if (sendType != 1) {
			/*printf("Zadajte cestu k suboru:\n");
			scanf("%s", fileName);
			printf("filename: %s\n", fileName);*/
			strcpy(fileName, "mb.txt");

			if (sendFile(sendingSocket, fragmentLength, receiverAddress, fileName))
				printf("Sending OK!\n");
			else
				printf("Sending is not OK!\n");
		}
		else {
			char *buf = calloc(MAX_SIZE, sizeof(char));
			messageToSend = calloc(MB, sizeof(char));
			printf("Zadajte spravu:\n");
			getchar();
			fgets(buf, MAX_SIZE, stdin);
			strcat(messageToSend, buf);
				
			printf("messageToSend: \"%s\"\n", messageToSend);

			if (sendMessage(sendingSocket, receiverAddress, fragmentLength, messageToSend))
				printf("Message sent!\n");
		}
		
		printf("Ak este chcete posielat, stlacte 'c'\n");		
		HANDLE handle = (HANDLE)_beginthread(ThreadFunction, 0, 0); // create thread for getchar() 
		while (1) {
			while (timeout_sender >= 0) {
				if (sendMore) {
					break;
				}
				printf("Waiting for %ds more\n", timeout_sender);
				timeout_sender--;
				Sleep(1000);
			}
			
			if (sendMore) {
				timeout_sender = TIMEOUT_SENDER;
				sendMore = 0;
				break;
			}

			printf("sending keep alive\n");
			if (sendKeepAlive(sendingSocket, receiverAddress, fragmentLength, messageToSend))
				printf("KeepAlive sent.\n");
			timeout_sender = TIMEOUT_SENDER;
			sendMore = 0;
		}		
	}
	
	printf("Finish. Closing the sending socket...\n");

	if (closesocket(sendingSocket) != 0)
		printf("closesocket() failed - error code: %ld\n", WSAGetLastError());
	else
		printf("closesocket() is OK\n");

	if (WSACleanup() != 0)
		printf("WSACleanup() failed - error code: %ld\n", WSAGetLastError());
	else
		printf("WSACleanup() is OK\n");

	getchar();
	getchar();
	getchar();
	getchar();
	getchar();
}


void receiverPart() {
	WSADATA wsaData;
	SOCKET receivingSocket;
	SOCKADDR_IN receiverAddress, senderAddr;
	int port = 5150, fragmentLength = 1024, senderAddrSize = sizeof(senderAddr),
		byteReceived = 5, SelectTiming, returnCode;

	if ((returnCode = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
		printf("WSAStartup failed with error %d\n", returnCode);
		return 1;
	}

	receivingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (receivingSocket == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	receiverAddress.sin_family = AF_INET;
	receiverAddress.sin_port = htons(port);
	receiverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(receivingSocket, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)) == SOCKET_ERROR) {
		printf("Server: bind() failed! Error: %ld.\n", WSAGetLastError());
		closesocket(receivingSocket);
		WSACleanup();
		return -1;
	}

	printf("Ready to receive.\n");

	getsockname(receivingSocket, (SOCKADDR *)&receiverAddress, (int *)sizeof(receiverAddress));

	int timeout = recvfromTimeOutUDP(receivingSocket, TIMEOUT_RECEIVER, 0);
	while (timeout > 0) {
		int max_rcv = HEADER_SIZE + MAX_SIZE;
		char *buf = malloc(max_rcv * sizeof(char));

		recvfrom(receivingSocket, buf, max_rcv, 0, (SOCKADDR *)&senderAddr, &senderAddrSize);
		int dataType = bytesToInt(buf, 12);		
		printf("dataType is: %d\n", dataType);
		
		if (dataType == KEEP_ALIVE) {
			printf("keep alive received \n");
			timeout = recvfromTimeOutUDP(receivingSocket, TIMEOUT_RECEIVER, 0);
			continue;
		}
		else if (dataType == FILE_FLAG) {
			int fragmentSize = bytesToInt(buf, 16);
			printf("fileName = %s\n", buf+20);
			int fd = open(buf + HEADER_SIZE, O_RDWR | O_CREAT, 0666), n;
			free(buf);
			buf = malloc(max_rcv * sizeof(char));

			while ((n = recvfrom(receivingSocket, buf, fragmentSize + HEADER_SIZE, 0, (SOCKADDR *)&senderAddr, &senderAddrSize)) &&
				  (bytesToInt(buf, 12) == FILE_FLAG && (bytesToInt(buf, 0) < bytesToInt(buf, 8)))) {
					printf("index = %d\n", bytesToInt(buf, 0));
					printf("total = %d\n", bytesToInt(buf, 8));
					printf("checksum = %d\n", bytesToInt(buf, 4));
					printf("dataType = %d\n", bytesToInt(buf, 12));
					printf("dataSize = %d\n", bytesToInt(buf, 16));
					printf("fd = %d\n", fd);
					printf("n = %d\n", n);

					printf("checked checksum: %d\n", checkChecksum(bytesToInt(buf, 4), buf + HEADER_SIZE, n - HEADER_SIZE));

					write(fd, buf + HEADER_SIZE, n - HEADER_SIZE);

					if (bytesToInt(buf, 0) == bytesToInt(buf, 8) - 1)
						break;
					
					free(buf);
					buf = malloc(max_rcv * sizeof(char));
				}
			close(fd);
			printf("jumped out\n");
		}
		else if (dataType == MESSAGE_FLAG) {
			char *receivedMessage = malloc(MB * sizeof(char));
			strcpy(receivedMessage, buf + HEADER_SIZE);
			int total = bytesToInt(buf, 8) - 1;

			printf("index = %d\n", bytesToInt(buf, 0));
			printf("total = %d\n", bytesToInt(buf, 8));
			printf("checksum = %d\n", bytesToInt(buf, 4));
			printf("dataType = %d\n", bytesToInt(buf, 12));
			printf("dataSize = %d\n", bytesToInt(buf, 16));
			
			free(buf);
			buf = malloc(max_rcv * sizeof(char));

			for (int i = 0; i < total; i++) {
				if (recvfrom(receivingSocket, buf, fragmentLength, 0, (SOCKADDR *)&senderAddr, &senderAddrSize) &&
				   (bytesToInt(buf, 12) == MESSAGE_FLAG && (bytesToInt(buf, 0) < bytesToInt(buf, 8)))) {
					strcat(receivedMessage, buf + HEADER_SIZE);

					printf("in while now\n");
					printf("index = %d\n", bytesToInt(buf, 0));
					printf("total = %d\n", bytesToInt(buf, 8));
					printf("checksum = %d\n", bytesToInt(buf, 4));
					printf("dataType = %d\n", bytesToInt(buf, 12));
					printf("dataSize = %d\n", bytesToInt(buf, 16));

					free(buf);
					buf = malloc(max_rcv * sizeof(char));
				}
			}
			printf("Message is: \"%s\"\n", receivedMessage);
		}
		else
			printf("I do not recognize this type of data.\n");
	
		printf("receive success\n");
		timeout = recvfromTimeOutUDP(receivingSocket, TIMEOUT_RECEIVER, 0);
	}

	printf("timed out\n");

	getpeername(receivingSocket, (SOCKADDR *)&senderAddr, &senderAddrSize);

	if (closesocket(receivingSocket) != 0)
		printf("Server: closesocket() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Server: closesocket() is OK\n");

	WSACleanup();

	getchar();
	getchar();
	getchar();
	getchar();
}

int main() {
	printf("Stlacte 1 ak chcete vysielat, stlacte 2 ak chcete prijimat:\n");

	if (getchar() == '1')
		senderPart();
	else
		receiverPart();
	

	return 0;
}