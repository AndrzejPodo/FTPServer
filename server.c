#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <dirent.h>

#define MAX_CLIENTS 10
#define SERVER_PORT_MIN 8080
#define DATA_PORT_MIN 10000

void *ClientHandler(void *socket_desc);
char *getFilenameFromRequest(char *request);
bool sendFileOverSocket(int socket_desc, char *file_name);
int sendResponse(int sockfd, const char *response);

uint16_t DATA_PORT = DATA_PORT_MIN;
uint16_t SERVER_PORT = SERVER_PORT_MIN;

typedef struct
{
	int id;
	char name[30];
	int socket;		 // do tego socketa piszesz responsy do zapytan
	int data_socket; // do tego socketa piszesz dane, np. wynik LS
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

	do
	{
		server.sin_port = htons(SERVER_PORT);
		SERVER_PORT += 1;
	} while (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0);

	listen(socket_desc, 3);

	int clientCounter = 0;
	printf("Waiting for client\n");
	while ((socket_client = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c)) > 0)
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

void *ClientHandler(void *cli)
{
	struct sockaddr_in data_addr, data_client_addr;
	int data_socket, data_socket_cli;
	int c = sizeof(struct sockaddr_in);

	Client client = *(Client *)cli;
	//TODO: zastanowic sie nad rozmiarami buforow
	char client_request[BUFSIZ], client_response[BUFSIZ];

	//po otwarciu połączenia wysyłamy do klienta powitanie
	sendResponse(client.socket, "220 (Welcome to STM32F4 FTP Server!) \r\n");

	char *request_token;
	//w petli odbieramy requesty od klienta
	while (1)
	{
		int receivedBytes = recv(client.socket, client_request, BUFSIZ, 0);
		if (receivedBytes >= 0 && receivedBytes < BUFSIZ - 1)
		{
			client_request[receivedBytes] = 0;
		}
		// analizujemy request token po tokenie
		// odcinamy pierwszą część (RETR, PASV, USER itp.)
		request_token = strtok(client_request, " \r\n");
		printf("%s\n", client_request);
		// obsluga komendy USER
		if (strcmp(request_token, "USER") == 0)
		{
			request_token = strtok(NULL, " \r\n");
			if (request_token != NULL)
			{
				printf("%s\n", request_token);
				strcpy(client.name, request_token);
				sprintf(client_response, "230 (Hi %s!) \r\n", request_token);
				sendResponse(client.socket, client_response);
			}
		}
		// obsluga komendy PASV
		if (strcmp(request_token, "PASV") == 0)
		{
			//trzeba otworzyc socket na porcie randomowym od 10k do 11k
			data_socket = socket(AF_INET, SOCK_STREAM, 0);
			data_addr.sin_family = AF_INET;
			inet_pton(AF_INET, "127.0.0.1", &(data_addr.sin_addr));
			//bierzemy DATA_PORT a potem inkrementujemy o jeden
			do
			{
				data_addr.sin_port = htons(DATA_PORT);
				DATA_PORT += 1;
			} while (bind(data_socket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0);

			//wyciagamy z data_addr poszczegolne bajty
			uint32_t ip_addr = ntohl(data_addr.sin_addr.s_addr);
			uint16_t port = ntohs(data_addr.sin_port);
			uint8_t b1 = (ip_addr >> 24), b2 = (ip_addr >> 16) % 256, b3 = (ip_addr >> 8) % 256, b4 = ip_addr % 256, b5 = port >> 8, b6 = port % 256;
			sprintf(client_response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n", b1, b2, b3, b4, b5, b6);
			sendResponse(client.socket, client_response);

			//czekamy na połaczenie na portcie danych
			listen(data_socket, 3);
			printf("Waiting for data connection \n");
			client.data_socket = accept(data_socket, (struct sockaddr *)&data_client_addr, (socklen_t *)&c);
			printf("Data connection established\n");
		}

		// obsluga komendy QUIT
		if (strcmp(request_token, "QUIT") == 0)
		{
			sprintf(client_response, "221 (Goodbye %s!) \r\n", client.name);
			sendResponse(client.socket, client_response);
			close(client.socket);

			int *res = malloc(1 * sizeof(int));
			*res = 0;
			return (void *)res;
		}

		//obsluga komendy LIST
		if (strcmp(request_token, "LIST") == 0)
		{
			if (client.data_socket > 0)
			{
				sendResponse(client.socket, "150 Here comes the directory listing. \r\n");
				request_token = strtok(NULL, " \r\n");
				if (request_token == NULL)
				{
					char cwd[256];
					getcwd(cwd, sizeof(cwd));
					request_token = &cwd[0];
				}
				DIR *dir = opendir(request_token);
				char path[256];
				struct stat buf;
				struct tm *t;
				char timebuff[80];
				time_t time;
				if (dir != NULL)
				{
					struct dirent *file;
					while ((file = readdir(dir)))
					{
						if ((strcmp(file->d_name, ".") != 0) && (strcmp(file->d_name, "..") != 0))
						{
							strcpy(path, request_token);
							strcat(path, "/");
							strcat(path, file->d_name);
							if (stat(path, &buf) != -1)
							{
								time = buf.st_mtime;
								t = localtime(&time);
								strftime(timebuff, 80, "%b %d %H:%M", t);
								sprintf(client_response, "%c%s %5d %4d %4d %8d %s %s \r\n",
										file->d_type == DT_DIR ? 'd' : '-',
										S_ISDIR(buf.st_mode) ? "rwxr-xr-x" : "rw-r--r--",
										buf.st_nlink,
										buf.st_uid,
										buf.st_gid,
										buf.st_size,
										timebuff,
										file->d_name);
								sendResponse(client.data_socket, client_response);
							}
						}
					}
					closedir(dir);
					sendResponse(client.socket, "226 Listing completed. \r\n");
					client.data_socket = -1;
					close(client.data_socket);
					printf("Listing completed.\n");
				}
				else
				{
					sendResponse(client.socket, "550 Failed to open directory. \r\n");
				}
			}
			else
			{
				sendResponse(client.socket, "425 Use PORT or PASV first. \r\n");
			}
		}
	}
}
int sendResponse(int sockfd, const char *response)
{
	return send(sockfd, response, strlen(response), 0);
}
