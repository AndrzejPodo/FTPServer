#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#define MAX_CLIENTS 10
#define SERVER_PORT 8086

void *ClientHandler(void *socket_desc);
char *getFilenameFromRequest(char *request);
bool sendFileOverSocket(int socket_desc, char *file_name);
int sendResponse(int sockfd, const char* response);

typedef struct
{
	int id;
	char name[30];
	int socket;
	pthread_t thread;
} Client;

Client clients[MAX_CLIENTS];

int main(int argc, char **argv)
{
	int socket_desc,
		socket_client,
		*new_sock,
		c = sizeof(struct sockaddr_in);

	struct sockaddr_in server, client;

	// Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		perror("Could not create socket");
		return 1;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SERVER_PORT);

	if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
	{
		perror("Bind failed");
		return 1;
	}

	listen(socket_desc, 3);

	int clientCounter = 0;
	printf("Waiting for client\n");
	while (socket_client = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c))
	{
		//uruchamiamy nowy wątek który będzie obsługiwał klienta
		pthread_t client_thread;
		Client client;
		client.id = clientCounter;
		client.socket = socket_client;
		pthread_create(&client_thread, NULL, ClientHandler, (void *)&client);
		//zapamiętujemy watek w tablicy klientów żeby móc je zjojnować czy zatrzymaniu serwera

		client.thread = client_thread;
		clients[clientCounter] = client;
		clientCounter += 1;

		printf("Client connected\n");
		printf("Waiting for client\n");
	}

	if (socket_client < 0)
	{
		perror("Accept failed");
		return 1;
	}

	return 0;
}

void* ClientHandler(void *cli)
{

	Client client = *(Client *)cli;
	//TODO: zastanowic sie nad rozmiarami buforow
	char client_request[BUFSIZ], client_response[BUFSIZ];
	
	//po otwarciu połączenia wysyłamy do klienta powitanie
	sendResponse(client.socket, "220 (Welcome to STM32F4 FTP Server!) \r\n");

	char *request_token;
	//w petli odbieramy requesty od klienta
	while (1)
	{
		recv(client.socket, client_request, BUFSIZ, 0);
		// analizujemy request token po tokenie
		// odcinamy pierwszą część (RETR, PASV, USER itp.)
		request_token = strtok(client_request, " \r\n");
		printf("%s\n", client_request);
		// obsluga komendy USER
		if (strcmp(request_token, "USER") == 0) {
			request_token = strtok(NULL, " \r\n");
			if(request_token != NULL) {
				printf("%s\n", request_token);
				strcpy(client.name, request_token);
				sprintf(client_response, "230 (Hi %s!) \r\n", request_token);
				sendResponse(client.socket, client_response);				
			}
		}

		// obsluga komendy QUIT
		if (strcmp(request_token, "QUIT") == 0){
			sprintf(client_response, "221 (Goodbye %s!) \r\n", client.name);
			sendResponse(client.socket, client_response);
			close(client.socket);

			int* res = malloc(1*sizeof(int));
			*res = 0;
			return (void*) res;
		}
	}
}

int sendResponse(int sockfd, const char* response){
	return send(sockfd, response, strlen(response), 0);
}


// te funkcja jest stara i moze nie miec sensu
char *GetFilenameFromRequest(char *request)
{
	printf("\n");
	char *file_name = strchr(request, ' ');
	printf("%s", file_name);
	return file_name + 1;
}

// te funkcja jest stara i moze nie miec sensu
bool SendFileOverSocket(int socket_desc, char *file_name)
{

	struct stat obj;
	int file_desc,
		file_size;

	stat(file_name, &obj);
	file_desc = open(file_name, O_RDONLY);
	file_size = obj.st_size;
	send(socket_desc, &file_size, sizeof(int), 0);
	sendfile(socket_desc, file_desc, NULL, file_size);

	return true;
}
