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
// 创建一个函数来处理客户端身份验证
// 该函数会提示用户输入用户名和密码，并进行简单的验证
// 返回值：1 表示验证成功，0 表示验证失败
int authenticate_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    char username[BUFFER_SIZE], password[BUFFER_SIZE];

    // 发送用户名提示
    send(client_socket, "Username: ", strlen("Username: "), 0);
    int read_size = recv(client_socket, username, BUFFER_SIZE, 0);
    if (read_size <= 0) return 0;
    

    // 发送密码提示
    send(client_socket, "Password: ", strlen("Password: "), 0);
    read_size = recv(client_socket, password, BUFFER_SIZE, 0);
    if (read_size <= 0) return 0;
    

    // 简单验证逻辑（应替换为数据库或哈希校验）
    if (strcmp(username, "admin") == 0 && strcmp(password, "secure123") == 0) {
        send(client_socket, "Login successful\n", strlen("Login successful\n"), 0);
        return 1;
    } else {
        send(client_socket, "Login failed\n", strlen("Login failed\n"), 0);
        return 0;
    }
}
void *handle_client(void *arg) {            // 创建一个线程处理客户端。后面可以增加参数用户名
    int client_socket = *(int *)arg;        // 客户端套接字 数组形式的
    char buffer[BUFFER_SIZE];               //客户端接收缓冲区
    int read_size;
    //
    //recv() 函数会从客户端接收数据，并将其存储到 buffer 中，最多接收 BUFFER_SIZE 字节的数据。
    //返回值是接受的字节数
    //
    // 先进行身份验证
    //如果身份验证失败，关闭套接字并退出线程
     if (!authenticate_client(client_socket)) {
        close(client_socket);
        pthread_exit(NULL);
    }
    while ((read_size = recv(client_socket, buffer, BUFFER_SIZE, 0))) {
        if (read_size <= 0) {       // 如果读取失败或客户端断开连接
            break;
        }

        buffer[read_size] = '\0';
       // 获取客户端地址信息
        struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char client_ip[INET_ADDRSTRLEN];        // 用于存储客户端IP地址字符串
    int client_port ;
    //打印接收到的消息,并显示套接字
    if (getpeername(client_socket, (struct sockaddr*)&client_addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        client_port = ntohs(client_addr.sin_port);      // 获取客户端端口号
        // 打印带端口的消息
        printf("Received from %s:%d: %s\n", client_ip, client_port, buffer);
    }

        // 广播消息给所有客户端
        pthread_mutex_lock(&mutex);//锁定互斥锁，确保线程安全操作客户端数组。
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != 0 && client_sockets[i] != client_socket) {
                char broadcast_msg[BUFFER_SIZE + 50];
                sprintf(broadcast_msg, "[%s:%d] %s", client_ip, client_port, buffer);
                send(client_sockets[i], broadcast_msg, strlen(broadcast_msg), 0);
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

    int port = atoi(argv[1]);       //程序会将 port 设置为 8080，然后服务器会在该端口监听客户端连接请求。
    //server_socket：服务器监听的主套接字。
    // client_socket：每次接受新客户端连接时返回的客户端套接字。
    // 用途：通过这些描述符可以对网络连接进行操作，如发送、接收、关闭等。
    int server_socket, client_socket;
    // server_addr：保存服务器自身的地址信息（IP + 端口）。
    // client_addr：保存连接到的客户端地址信息。
    struct sockaddr_in server_addr, client_addr;
    // socklen_t：用于存储地址结构的长度。
    socklen_t client_len = sizeof(client_addr);
    // 线程ID
    pthread_t thread_id;

    // 创建服务器套接字 程序通过系统调用请求操作系统创建的
    // 相当于文件描述符，通过它可以进一步对网络连接进行操作。
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    //一台机器可能有多个IP地址，127.0.0.1 本机回环地址。192.168.1.1 局域网IP
    //配置服务器地址是为了告诉操作系统服务器要监听本机哪个 IP 和端口上的连接请求，是服务器网络通信的必要准备步骤。
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // 绑定套接字
    //将服务器套接字与指定的地址和端口绑定。这样，通过套接字，操作系统就可以接收来自指定地址和端口的连接请求。
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // 让服务器处于监听状态，先listen，然后accept
    //listen() 函数使服务器套接字进入监听状态(无法主动发起连接，只能等待连接)，等待客户端连接请求。
    //第二个参数指定了服务器可以排队等待的最大连接数。
    //如果有多个客户端同时连接，超过这个数量的连接请求将被拒绝。
    if (listen(server_socket, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d. Waiting for connections...\n", port);

    while (1) {
        // 接受新连接
        //accept() 函数会阻塞，直到有客户端连接到服务器。
        //一旦有客户端连接，accept() 会返回一个新的套接字描述符 client_socket，用于与该客户端进行通信。
        //同时，client_addr 会被填充为连接的客户端的地址信息。
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