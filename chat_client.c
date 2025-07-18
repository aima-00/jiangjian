#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/ioctl.h>  // 用于 ioctl
#include <unistd.h>     // 用于 STDOUT_FILENO
#define BUFFER_SIZE 1024

int client_socket;
int is_running = 1;
void *receive_messages(void *arg) {
    // 定义接收缓冲区
    char buffer[BUFFER_SIZE];
    int read_size;

    // 循环接收服务器发送的消息
    // 只要 is_running 为真，并且 recv 成功读取到数据（read_size > 0），就继续循环
    while (is_running && (read_size = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[read_size] = '\0';

        // 获取终端宽度
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int terminal_width = w.ws_col;

        // 计算消息长度
        int msg_len = strlen(buffer);

        // 如果消息长度超过终端宽度，就不做对齐处理
        if (msg_len >= terminal_width) {
            printf("%s\n", buffer);
        } else {
            // 动态生成格式字符串，例如 "%*s\n"
            char format[20];
            snprintf(format, sizeof(format), "%%%ds\n", terminal_width);
            printf(format, buffer);
        }
    }

    // 判断退出循环的原因
    if (read_size == 0) {
        // read_size == 0 表示服务器已关闭连接
        printf("Server disconnected\n");
    } else if (read_size == -1) {
        // read_size == -1 表示接收数据时发生错误
        perror("Receive failed");
    }

    // 设置 is_running 为 0，通知主线程退出发送循环
    is_running = 0;

    // 线程正常退出
    pthread_exit(NULL);
}
   
int main(int argc, char *argv[]) {
    // 检查命令行参数是否正确：程序名、服务器IP、端口号
    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    // 获取服务器IP地址和端口号
    char *server_ip = argv[1];          // 第一个参数：服务器IP地址
    int port = atoi(argv[2]);           // 第二个参数：端口号（字符串转整数）

    // 定义服务器地址结构体
    struct sockaddr_in server_addr;
    pthread_t thread_id;                // 线程ID，用于创建接收消息的线程

    // 创建客户端TCP套接字
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址信息
    server_addr.sin_family = AF_INET;                   // 使用IPv4协议
    server_addr.sin_port = htons(port);                 // 设置端口号并转为网络字节序
    // 将IP地址从字符串转换为网络可用的二进制格式
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        exit(EXIT_FAILURE);
    }

    // 连接到服务器
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }

    // 打印连接成功信息
    printf("Connected to server %s:%d\n", server_ip, port);

    // 创建子线程用于接收服务器消息
    if (pthread_create(&thread_id, NULL, receive_messages, NULL) < 0) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }

    // 主线程用于发送用户输入的消息
    char buffer[BUFFER_SIZE];       // 发送缓冲区
    while (is_running) {
        // 从标准输入读取用户输入
        fgets(buffer, BUFFER_SIZE, stdin);
        // 去除字符串末尾换行符
        buffer[strcspn(buffer, "\n")] = '\0';

        // 如果用户输入 "/exit"，则退出程序
        if (strcmp(buffer, "/exit") == 0) {
            is_running = 0;         // 设置标志位，通知接收线程退出
            break;
        }

        // 向服务器发送消息
        if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }
    }

    // 关闭客户端套接字
    close(client_socket);

    // 程序正常退出
    return 0;
}