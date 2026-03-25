#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <openssl/evp.h>    //用于实现哈希函数的库
#define MAX_CLIENTS 10      //允许的最大客户端数量
#define BUFFER_SIZE 1024    //缓冲区大小
//实现用配置文件存储用户信息
#define MAX_USERNAME_LEN 32     //用户名最大长度
#define MAX_PASSWORD_LEN 128    //密码最大长度
#define USER_DB_FILE "users.conf"   // 用户信息存储文件名
typedef struct {
    char username[MAX_USERNAME_LEN];
    char password_hash[MAX_PASSWORD_LEN]; // 存储哈希处理后的密码
} User;
User users[MAX_CLIENTS];                // 存储用户信息的数组缓冲区
int client_sockets[MAX_CLIENTS] = {0};  // 用于存储连接的客户端套接字
//互斥锁，确保对 client_sockets 数组的访问是线程安全的
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


//
// 使用 OpenSSL 库进行 SHA-256 哈希处理
//
void hash_password(const char *input, char *output) {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_sha256();
    unsigned char md_value[32];
    unsigned int md_len;

    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, input, strlen(input));
    EVP_DigestFinal_ex(mdctx, md_value, &md_len);
    EVP_MD_CTX_free(mdctx);

    // 转换为十六进制字符串
    for (unsigned int i = 0; i < md_len; i++) {
        sprintf(output + (i * 2), "%02x", md_value[i]);
    }
    output[64] = '\0'; // SHA-256 生成 64 位十六进制字符
}


//实现加载、保存和管理用户信息的函数：

int user_count = 0;                       // 当前用户数量，全局变量
pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;   // 互斥锁，确保对用户信息的访问是线程安全的
//
//加载用户信息函数：从配置文件中读取用户信息，并存储到全局变量 users 中。
//如果文件不存在，则不执行任何操作。
//该函数会在服务器启动时调用，以确保用户信息已加载到内存中。
//注意：该函数假设用户信息文件的格式为 "username:password_hash"，每行一个用户。
//如果文件格式不正确，函数会忽略该行。
//返回值：无返回值，但会更新全局变量 users 和 user_count。
//
void load_users() {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) return; // 文件不存在

    user_count = 0;
    while (user_count < MAX_CLIENTS && 
           fscanf(fp, "%[^:]:%s\n", users[user_count].username, users[user_count].password_hash) == 2) {
        user_count++;
    }

    fclose(fp);
}

int save_users() {
    FILE *fp = fopen(USER_DB_FILE, "w");
    if (!fp) return 0;

    for (int i = 0; i < user_count; i++) {
        fprintf(fp, "%s:%s\n", users[i].username, users[i].password_hash);
    }

    fclose(fp);
    return 1;
}

int is_username_unique(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            return 0; // 用户名已存在
        }
    }
    return 1; // 用户名唯一
}

//实现客户端可使用的注册函数：
int register_user(int client_socket) {
    char buffer[BUFFER_SIZE];
    char username[BUFFER_SIZE], password[BUFFER_SIZE];

    // 获取用户名
    send(client_socket, "New username: ", strlen("New username: "), 0);
    int read_size = recv(client_socket, username, BUFFER_SIZE-1, 0);
    if (read_size <= 0) return 0;
    username[read_size] = '\0';

    // 去除换行符（如果存在）
    if (username[strlen(username)-1] == '\n') {
        username[strlen(username)-1] = '\0';
    }

    // 检查用户名是否存在
    if (!is_username_unique(username)) {
        send(client_socket, "Username already exists\n", strlen("Username already exists\n"), 0);
        return 0;
    }

    // 获取密码
    send(client_socket, "New password: ", strlen("New password: "), 0);
    read_size = recv(client_socket, password, BUFFER_SIZE-1, 0);
    if (read_size <= 0) return 0;
    password[read_size] = '\0';

    // 去除换行符（如果存在）
    if (password[strlen(password)-1] == '\n') {
        password[strlen(password)-1] = '\0';
    }

    // 哈希处理密码
    char hashed_password[65];
    hash_password(password, hashed_password);

    // 添加到用户数据库
    pthread_mutex_lock(&config_mutex);

    if (user_count < MAX_CLIENTS) {
        strcpy(users[user_count].username, username);
        strcpy(users[user_count].password_hash, hashed_password);
        user_count++;

        // 保存到文件
        if (save_users()) {
            send(client_socket, "Registration and log in successful\n", strlen("Registration and log in successful\n\n"), 0);
            pthread_mutex_unlock(&config_mutex);
            return 1;
        }
    }

    pthread_mutex_unlock(&config_mutex);
    send(client_socket, "Registration failed\n", strlen("Registration failed\n"), 0);
    return 0;
}

// 创建一个函数来处理客户端身份验证
// 该函数会提示用户输入用户名和密码，并使用配置文件进行验证。
// 如果验证成功，返回 1；如果失败，返回 0。
// 该函数会在客户端套接字上发送提示信息，并接收用户输入的用户名和密码。
// 该函数会使用哈希处理密码，并与存储的哈希值进行比较。
// 如果用户名不存在或密码不正确，函数会向客户端发送错误信息。
// 如果用户名和密码正确，函数会向客户端发送成功信息，并返回 1。
// 如果发生错误（如接收数据失败），函数会返回 0。
// 注意：该函数假设用户信息已经加载到全局变量 users 中，并且 user_count 已经设置。
// 返回值：1 表示验证成功，0 表示验证失败
int authenticate_client(int client_socket,int *userid) {
    char buffer[BUFFER_SIZE];
    char username[BUFFER_SIZE], password[BUFFER_SIZE];

    // 提示用户注册或登录
    send(client_socket, "Enter 'register' to create an account or 'login' to sign in: ", 
         strlen("Enter 'register' to create an account or 'login' to sign in: "), 0);

    int read_size = recv(client_socket, buffer, BUFFER_SIZE-1, 0);
    if (read_size <= 0) return 0;
    buffer[read_size] = '\0';

    // 去除换行符
    if (buffer[strlen(buffer)-1] == '\n') {
        buffer[strlen(buffer)-1] = '\0';
    }

    // 处理注册
    if (strcasecmp(buffer, "register") == 0) {
        return register_user(client_socket);
    }

    // 否则执行登录流程
    // 获取用户名
    send(client_socket, "Username: ", strlen("Username: "), 0);
    read_size = recv(client_socket, username, BUFFER_SIZE-1, 0);
    if (read_size <= 0) return 0;
    username[read_size] = '\0';

    if (username[strlen(username)-1] == '\n') {
        username[strlen(username)-1] = '\0';
    }

    // 获取密码
    send(client_socket, "Password: ", strlen("Password: "), 0);
    read_size = recv(client_socket, password, BUFFER_SIZE-1, 0);
    if (read_size <= 0) return 0;
    password[read_size] = '\0';

    if (password[strlen(password)-1] == '\n') {
        password[strlen(password)-1] = '\0';
    }

    // 查找用户
    int user_index = -1;
    pthread_mutex_lock(&config_mutex);

    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            user_index = i;
            break;
        }
    }

    pthread_mutex_unlock(&config_mutex);

    if (user_index == -1) {
        send(client_socket, "Login failed - unknown username\n", strlen("Login failed - unknown username\n"), 0);
        return 0;
    }

    // 对输入的密码进行哈希处理
    char hashed_password[65];
    hash_password(password, hashed_password);

    // 与存储的哈希值进行比较
    if (strcmp(users[user_index].password_hash, hashed_password) == 0) {
        send(client_socket, "Login successful\n", strlen("Login successful\n"), 0);
        *userid=user_index;                //把用户ID返回给客户端
        return 1;
    } else {
        send(client_socket, "Login failed - incorrect password\n", strlen("Login failed - incorrect password\n"), 0);
        return 0;
    }
}
void *handle_client(void *arg) {            // 创建一个线程处理客户端。后面可以增加参数用户名
    int client_socket = *(int *)arg;        // 客户端套接字 数组形式的
    int userid = -1;
    char clientname[100];
    char buffer[BUFFER_SIZE];               //客户端接收缓冲区
    int read_size;
    //
    //recv() 函数会从客户端接收数据，并将其存储到 buffer 中，最多接收 BUFFER_SIZE 字节的数据。
    //返回值是接受的字节数
    //
    // 先进行身份验证
    //如果身份验证失败，关闭套接字并退出线程
     if (!authenticate_client(client_socket,&userid)) {
        close(client_socket);
        pthread_exit(NULL);
    }

    if(userid!=-1){
    strcpy(clientname,users[userid].username);} // 获取用户名
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
    //打印接收到的消息,并显示用户名
        printf("Received from %s: %s\n", clientname, buffer);
        // 广播消息给所有客户端
        pthread_mutex_lock(&mutex);//锁定互斥锁，确保线程安全操作客户端数组。
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] != 0 && client_sockets[i] != client_socket) {
                char broadcast_msg[BUFFER_SIZE + 50];
                sprintf(broadcast_msg, "[%s]: %s",  clientname, buffer);
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

    // 初始化用户数据库
    load_users();


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