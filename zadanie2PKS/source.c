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


char END_FLAG[4] = "END", FILE_FLAG[5] = "FILE", MESSAGE_FLAG[8] = "MESSAGE", KEEP_ALIVE[11] = "KEEP ALIVE";
int timeout_sender = TIMEOUT_SENDER, timeout_receiver = TIMEOUT_RECEIVER;
int sendMore = 0;

int recvfromTimeOutUDP(SOCKET socket, long sec, long usec) {
	struct timeval timeout;
	struct fd_set fds;	
	timeout.tv_sec = sec;
	timeout.tv_usec = usec;
	FD_ZERO(&fds);
	FD_SET(socket, &fds);

	return select(0, &fds, 0, 0, &timeout);
}

char **fragmentFile() {

}

int sendFile(SOCKET sendingSocket, int fragmentLength, SOCKADDR_IN receiverAddress, char *fileName) {
	int f = open(fileName, O_RDONLY), readLen, bytesSent;
	char *buf = malloc(fragmentLength * sizeof(char));
	//char  **toBeSent = malloc()

	if (f == NULL) {
		printf("File does not exist\n");
		return 1;
	}

	sendto(sendingSocket, FILE_FLAG, strlen(FILE_FLAG), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	sendto(sendingSocket, fileName, strlen(fileName), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	while ((readLen = read(f, buf, fragmentLength)) > 0) {
		sendto(sendingSocket, buf, readLen, 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));
		free(buf);
		buf = malloc(fragmentLength * sizeof(char));
		//printf("readlen %d\n", readLen);
	}

	sendto(sendingSocket, END_FLAG, strlen(END_FLAG), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	return 1;
}

int sendMessage(SOCKET sendingSocket, SOCKADDR_IN receiverAddress, int fragmentLength, char *messageToSend) {

	sendto(sendingSocket, MESSAGE_FLAG, strlen(MESSAGE_FLAG), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	sendto(sendingSocket, messageToSend, strlen(messageToSend), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	sendto(sendingSocket, END_FLAG, strlen(END_FLAG), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));

	return 1;
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
	SOCKADDR_IN receiverAddress, SrcInfo;
	int Port, fragmentLength, len, TotalByteSent, sendType;
	char messageToSend[1452], destinationIP[16], fileName[50];

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("Client: WSAStartup failed with error %ld\n", WSAGetLastError());
		WSACleanup();

		return -1;
	}

	sendingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sendingSocket == INVALID_SOCKET) {
		printf("Client: Error at socket(): %ld\n", WSAGetLastError());
		WSACleanup();

		return -1;
	}
	
	int timeout = 10;
	while (1) {
		
		printf("Zadajte velkost fragmentu(64-1452):\n");
		scanf("%d", &fragmentLength);
		printf("fragment: %d\n", fragmentLength);

		printf("Zadajte IP prijimatela:\n");
		scanf("%s", destinationIP);
		printf("ip: %s\n", destinationIP);

		printf("Zadajte Port na prijimanie odpovedi:\n");
		scanf("%d", &Port);
		printf("port: %d\n", Port);
		

		receiverAddress.sin_family = AF_INET;
		receiverAddress.sin_port = htons(5150);
		receiverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

		
		printf("Chcete posielat spravu(1) alebo subor(2):\n");
		scanf("%d", &sendType);
		printf("sendType: %d\n", sendType);
		

		sendType = 1;
		fragmentLength = 1024;
		if (sendType != 1) {
			/*printf("Zadajte cestu k suboru:\n");
			getchar();
			scanf("%[^\n]\n", fileName);
			printf("filename: %s\n", fileName);*/

			strcpy(fileName, "1mb.txt");

			if (sendFile(sendingSocket, fragmentLength, receiverAddress, fileName))
				printf("Sending OK!\n");
			else
				printf("Sending is not OK!\n");
		}
		else {

			printf("Zadajte spravu:\n");
			getchar();
			fgets(messageToSend, fragmentLength, stdin);
			messageToSend[strlen(messageToSend)] = 0;
			printf("buf: %s\n", messageToSend);

			if (sendMessage(sendingSocket, receiverAddress, fragmentLength, messageToSend))
				printf("Message sent!\n");
		}
		
		printf("Ak este chcete posielat, stlacte 'c'\n");		
		HANDLE handle = (HANDLE)_beginthread(ThreadFunction, 0, 0); // create thread
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
			sendto(sendingSocket, KEEP_ALIVE, strlen(KEEP_ALIVE), 0, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress));
			timeout_sender = TIMEOUT_SENDER;
			sendMore = 0;
		}
		
	}
	
	// When your application is finished receiving datagrams close the socket.

	printf("Client: Finished sending. Closing the sending socket...\n");

	if (closesocket(sendingSocket) != 0)
		printf("Client: closesocket() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Server: closesocket() is OK\n");

	// When your application is finished call WSACleanup.

	printf("Client: Cleaning up...\n");
	if (WSACleanup() != 0)
		printf("Client: WSACleanup() failed! Error code: %ld\n", WSAGetLastError());

	else
		printf("Client: WSACleanup() is OK\n");

	while (1) {};
}


void receiverPart() {
	WSADATA wsaData;
	SOCKET ReceivingSocket;
	SOCKADDR_IN receiverAddress, SenderAddr;
	int Port = 5150, fragmentLength = 1024, SenderAddrSize = sizeof(SenderAddr),
		ByteReceived = 5, SelectTiming, ErrorCode, returnCode;

	// Initialize Winsock version 2.2
	if ((returnCode = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
		printf("WSAStartup failed with error %d\n", returnCode);
		return 1;
	}

	// Create a new socket to receive datagrams on.
	ReceivingSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (ReceivingSocket == INVALID_SOCKET) {
		printf("Server: Error at socket(): %ld\n", WSAGetLastError());
		WSACleanup();
		return -1;
	}

	receiverAddress.sin_family = AF_INET;
	receiverAddress.sin_port = htons(Port);
	receiverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(ReceivingSocket, (SOCKADDR *)&receiverAddress, sizeof(receiverAddress)) == SOCKET_ERROR) {
		printf("Server: bind() failed! Error: %ld.\n", WSAGetLastError());
		closesocket(ReceivingSocket);
		WSACleanup();
		return -1;
	}

	getsockname(ReceivingSocket, (SOCKADDR *)&receiverAddress, (int *)sizeof(receiverAddress));

	int timeout = recvfromTimeOutUDP(ReceivingSocket, TIMEOUT_RECEIVER, 0);
	while (timeout > 0) {
		char *buf = calloc(65535, sizeof(char));

		//FILE or MESSAGE or KeepAlive
		recvfrom(ReceivingSocket, buf, 1452, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
		printf("%s\n", buf);

		if (!strcmp(buf, KEEP_ALIVE)) {
			printf("keep alive received %s\n", buf);
			timeout = recvfromTimeOutUDP(ReceivingSocket, TIMEOUT_RECEIVER, 0);
			continue;
		}
		else if (!strcmp(buf, FILE_FLAG)) {
			free(buf);
			buf = calloc(65535, sizeof(char));
			recvfrom(ReceivingSocket, buf, 65535, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
			int fd = open(buf, O_RDWR | O_CREAT, 0666), n;
			free(buf);
			buf = calloc(65535, sizeof(char));

			while ((n = recvfrom(ReceivingSocket, buf, 1024, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize))) {
				buf[n] = 0;
				if (!(strcmp(buf, END_FLAG))) {
					break;
				}

				write(fd, buf, n);

				free(buf);
				buf = calloc(65535, sizeof(char));
			}
			close(fd);
		}
		else {			
			free(buf);
			buf = calloc(65535, sizeof(char));
			ByteReceived = recvfrom(ReceivingSocket, buf, fragmentLength, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
			printf("Server: The data is \"%s\"\n", buf);
			recvfrom(ReceivingSocket, buf, fragmentLength, 0, (SOCKADDR *)&SenderAddr, &SenderAddrSize);
		}

		printf("receive success\n");
		timeout = recvfromTimeOutUDP(ReceivingSocket, TIMEOUT_RECEIVER, 0);
	}

	printf("timed out\n");

	getpeername(ReceivingSocket, (SOCKADDR *)&SenderAddr, &SenderAddrSize);

	if (closesocket(ReceivingSocket) != 0)
		printf("Server: closesocket() failed! Error code: %ld\n", WSAGetLastError());
	else
		printf("Server: closesocket() is OK\n");

	WSACleanup();

	while (1) {};
}

int main() {
	printf("Stlacte 1 ak chcete vysielat, stlacte 2 ak chcete prijimat:\n");

	if (getchar() == '1')
		senderPart();
	else
		receiverPart();
	

	return 0;
}