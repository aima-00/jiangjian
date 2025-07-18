#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

int client_socket;
int is_running = 1;

void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];
    int read_size;

    while (is_running && (read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[read_size] = '\0';
        printf("%s\n", buffer);
    }

    if (read_size == 0) {
        printf("Server disconnected\n");
    } else if (read_size == -1) {
        perror("Receive failed");
    }

    is_running = 0;
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    struct sockaddr_in server_addr;
    pthread_t thread_id;

    // 创建客户端套接字
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        exit(EXIT_FAILURE);
    }

    // 连接到服务器
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n", server_ip, port);

    // 创建线程接收消息
    if (pthread_create(&thread_id, NULL, receive_messages, NULL) < 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    // 主线程发送消息
    char buffer[BUFFER_SIZE];
    while (is_running) {
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; // 移除换行符

        if (strcmp(buffer, "/exit") == 0) {
            is_running = 0;
            break;
        }

        if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }
    }

    close(client_socket);
    return 0;
}