#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>

#define MAX_CLIENTS 10      //
#define BUFFER_SIZE 1024    //缓冲区大小

int client_sockets[MAX_CLIENTS] = {0};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;        // 客户端套接字 数组形式的
    char buffer[BUFFER_SIZE];               //客户端接收缓冲区
    int read_size;
    //
    //recv() 函数会从客户端接收数据，并将其存储到 buffer 中，最多接收 BUFFER_SIZE 字节的数据。
    //返回值是接受的字节数
    //
    while ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0))) {
        if (read_size <= 0) {       // 如果读取失败或客户端断开连接
            break;
        }

        buffer[read_size] = '\0';
        printf("Received %d: %s\n",client_socket, buffer);

        // 广播消息给所有客户端
        pthread_mutex_lock(&mutex);//锁定互斥锁，确保线程安全操作客户端数组。
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != 0 && client_sockets[i] != client_socket) {
                send(client_sockets[i], buffer, strlen(buffer), 0);
            }
        }
        pthread_mutex_unlock(&mutex);
    }

    // 客户端断开连接
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == client_socket) {
            client_sockets[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);

    close(client_socket);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;

    // 创建服务器套接字
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // 绑定套接字
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 监听连接
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d. Waiting for connections...\n", port);

    while (1) {
        // 接受新连接
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        printf("New connection from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // 将新客户端添加到数组
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] == 0) {
                client_sockets[i] = client_socket;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        // 创建线程处理客户端
        if (pthread_create(&thread_id, NULL, handle_client, (void *)&client_socket) < 0) {
            perror("Thread creation failed");
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_socket);
    return 0;
}