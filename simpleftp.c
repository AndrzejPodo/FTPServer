#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>

#define PORT 6063
#define MAX_REQ_LEN 1024
#define MAX_FILENAME_LEN 512
#define MAX_CHUNK_SIZE 512
#define MAX_CMD_LEN 32

const char *OK = "OK\n";                   // server response when everything is OK
const char *GET = "GET";                   // command for requesting for a file
const char *BAD_REQUEST = "BAD_REQUEST\n"; // in case command not supported or bad filename
const char *BAD_FILENAME = "BAD_FILENAME\n";

int chunkWrite(int, char *, int); // writes data from socket chunk by chunk to deal with possible socket buffer limits
int chunkRead(int, char *, int);  // reads data from socket chunk by chunk to deal with possible socket buffer limits
void cutRequest(char *, char *, char *);
int sendFile(int, FILE*);

void *clientHandler(void *);
int mainSocket, clientCmdSocket, clientFtSocket;

void sig_handler(int signo)
{
  if (signo == SIGINT){
    close(mainSocket);
    exit(0);
  }
}

int main()
{   
    signal(SIGINT, sig_handler);
    struct sockaddr_in serverAddr, clientAddrCmd, clientAddrFT;
    char clientAddresStr[100];

    printf("SERVER: creating main socket\n");
    if ((mainSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("SERVER: cannot create main socket");
        return -1;
    }

    bzero((char *)&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    printf("SERVER: binding main socket\n");
    if (bind(mainSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("SERVER: cannot bind");
        return -1;
    }

    printf("SERVER: starting listen\n");
    if (listen(mainSocket, 5) < 0)
    {
        perror("SERVER: error while listening");
        return -1;
    }

    //main loop where we handle connections
    int clientAddrLen;
    while (1)
    {
        printf("SERVER: waiting for client CMD connection\n");
        clientAddrLen = sizeof(clientAddrCmd);
        if ((clientCmdSocket = accept(mainSocket, (struct sockaddr *)&clientAddrCmd, &clientAddrLen)) < 0)
        {
            perror("SERVER: accept error");
            close(mainSocket);
            return -1;
        }

        printf("SERVER: waiting for client File Transfer connection\n");
        clientAddrLen = sizeof(clientAddrFT);
        if ((clientFtSocket = accept(mainSocket, (struct sockaddr *)&clientAddrFT, &clientAddrLen)) < 0)
        {
            perror("SERVER: accept error");
            close(mainSocket);
            return -1;
        }

        if(clientAddrCmd.sin_addr.s_addr != clientAddrFT.sin_addr.s_addr){
            printf("SERVER: connection from different host.");
            close(clientCmdSocket);
            close(clientFtSocket);
            close(mainSocket);
        }
        inet_ntop(AF_INET, &(clientAddrCmd.sin_addr), clientAddresStr, 100);
        printf("SERVER: clinet with ip %s accepted with sock %d\n", clientAddresStr, clientCmdSocket);

        //starting client thread
        pthread_t clientHandlerThread;
        pthread_create(&clientHandlerThread, NULL, clientHandler, NULL);
        pthread_join(clientHandlerThread, NULL);

        close(clientFtSocket);
        close(clientCmdSocket);
    }

    return 0;
}

void *clientHandler(void * any)
{
    // int clinetSd = *(int *)clientSock;
    int* res = malloc(sizeof(int));
    FILE *fp;
    char request[MAX_REQ_LEN];
    char command[MAX_CMD_LEN];
    char filename[MAX_FILENAME_LEN];
    char response[MAX_CMD_LEN];


    if (recv(clientCmdSocket, request, MAX_REQ_LEN, 0) < 0)
    {
        perror("CLIENT HANDLER: cannot receive request");
        *res = -1; return (void*) res;
    }

    printf("CLIENT HANDLER: %s\n", request);

    cutRequest(request, command, filename);

    printf("CLIENT HANDLER - CMD:%s\n", command);
    if(strcmp(GET, command) == 0){    
        printf("CLIENT HANDLER - FILENAME:%s\n", filename);
        if((fp = fopen(filename, "rb")) == NULL){
            strcpy(response, BAD_FILENAME);
            chunkWrite(clientCmdSocket, response, sizeof(response));
            close(clientCmdSocket);
            *res = -1; return (void*) res;
        }

        strcpy(response, OK);
        if (chunkWrite(clientCmdSocket, response, sizeof(response)) < 0){
            *res = -1; return (void*) res;
        }

        if(sendFile(clientFtSocket, fp) < 0){
            *res = -1; return (void*) res;
        }
    }

    return 0;
}

void cutRequest(char *request, char *command, char *filename)
{
    char *token;
    token = strtok(request, " \n\r");
    strcpy(command, token);
    token = strtok(NULL, " \n\r");
    if (token != NULL)
    {
        strcpy(filename, token);
    }
    else
    {
        filename[0] = 0;
    }
}

int chunkRead(int socketId, char *buffer, int size)
{
    int bytesToRead = size;
    int bytesRead = 0;
    while (bytesToRead > 0)
    {
        bytesRead = read(socketId, buffer, bytesToRead);
        if (bytesRead < 0)
        {
            perror("Read from socket failed:");
            return -1; //this indicates an error
        }
        if (bytesRead == 0)
        {
            break;
        }
        buffer += bytesRead;
        bytesToRead -= bytesRead;
    }
    return (size - bytesToRead);
}

int sendFile(int socketId, FILE* fp){
    char buffer[MAX_CHUNK_SIZE];
    int bytesRead;

    while((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0){
        if(chunkWrite(socketId, buffer, bytesRead) < 0){
            return -1;   
        }
    }
    return 0;
}

int chunkWrite(int socketId, char *data, int size)
{
    int bytesToWrite = size;
    int bytesWritten = 0;
    while (bytesToWrite > 0)
    {
        bytesWritten = write(socketId, data, bytesToWrite);
        if (bytesWritten < 0)
        {
            return -1;
        }
        if (bytesWritten == 0)
        {
            break;
        }
        bytesToWrite -= bytesWritten;
        data += bytesWritten;
    }
    return (size - bytesToWrite);
}
