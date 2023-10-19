/*
    Reverse Proxy server written in C

    Author: @kirillzhosul (kirillzhosul@florgon.com)
    License: MIT

    TODO:
    - Faster polling of socket events and etc / threading
    - Configuration loader
    - Windows / WSL support
*/

#ifdef linux
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
// Currently, Windows is not supported
#include <stdlib.h>
#endif

// Temporary: TODO!
// #define EWOULDBLOCK EAGAIN /* Operation would block */

// Configuration currently is handled as struct without even loading from some place
// TODO: Configuration loader
struct Config
{
    int SOCKET_MAX_PENDING;
    int SOCKET_BIND_PORT;
    int SOCKET_REUSE_ADDR;
    int TIMEOUT_504_SECONDS;
    int MEMORY_CONNECTION_BUFFER;
    int LOGGING_VERBOSE;
    int TARGET_PORT;
    char *TARGET_HOST;
    int ERROR_SHOW_VERSION;
};
struct Config config;
const int GATEWAY_STATUS_OK = 0;
const int GATEWAY_STATUS_BAD = 1;
const int GATEWAY_STATUS_TIMEOUT = 2;

#define TEMP_BUFFER_SIZE 1024 // TODO: Configure size

#define NAME "Reverse Proxy Server"
#define VERSION "0.1/dev"
#define HIDE_HEADER 0

void default_error_response(char *response, char *status)
{
    char *version = "";
    if (config.ERROR_SHOW_VERSION)
    {
        version = VERSION;
    }

    snprintf(response, config.MEMORY_CONNECTION_BUFFER,
             "HTTP/1.1 %s\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<html>"
             "<head><title>%s</title></head>"
             "<body><h2>%s</h2><h4>%s %s</h4></body>"
             "</html>",
             status, status, status, NAME, version);
}

void print_welcome_header()
{
    /*
        Shows application welcome header
    */
    if (!HIDE_HEADER)
    {

        printf("Reverse Proxy server\n");
        printf("Author: @kirillzhosul\n");
        printf("Licensed under MIT license\n");
        printf("\n");
    }
}

int create_socket(int set_opts)
{
    /*
        Creates socket, set options and returns it
    */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1)
    {
        printf("ERROR: Unable to create socket!\n");
        return -1;
    }

    if (set_opts == 1)
    {
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &config.SOCKET_REUSE_ADDR, sizeof(int)) < 0)
        {
            printf("ERROR: Unable to set socket options!\n");
            return 1;
        }
    }

    if (config.LOGGING_VERBOSE)
    {
        printf("INFO: Created socket!\n");
    }

    return socket_fd;
}

int bind_and_listen_socket(int socket_fd)
{
    /*
        Binds and listens to socket descriptor
    */

    if (socket_fd < 0)
    {
        return socket_fd;
    }

    // Bind address
    struct sockaddr_in socket_addr;
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = INADDR_ANY;
    socket_addr.sin_port = htons(config.SOCKET_BIND_PORT);
    struct sockaddr *bind_addr = (struct sockaddr *)&socket_addr;

    if (bind(socket_fd, bind_addr, sizeof(socket_addr)) < 0)
    {
        printf("ERROR: Unable to bind socket!\n");
        return -1;
    }

    if (listen(socket_fd, config.SOCKET_MAX_PENDING) < 0)
    {
        printf("ERROR: Unable to listen socket!\n");
        return -1;
    }

    if (config.LOGGING_VERBOSE)
    {
        printf("INFO: Bind and listen socket!\n");
    }

    return socket_fd;
}

int get_gateway_response(int *gateway_answered, char *gateway_response_buffer[])
{
    *gateway_answered = 0;

    if (config.LOGGING_VERBOSE)
    {
        printf("INFO: Requesting gateway response!\n");
    }

    int proxy_socket_fd = create_socket(0);

    struct sockaddr_in proxy_addr;
    proxy_addr.sin_family = AF_INET;
    // Raw addr: proxy_addr.sin_addr.s_addr = inet_addr(config.TARGET_HOST);
    proxy_addr.sin_port = htons(config.TARGET_PORT);
    char request[TEMP_BUFFER_SIZE];

    struct hostent *hostname;
    if ((hostname = gethostbyname(config.TARGET_HOST)) == NULL)
    {
        printf("ERROR: Host resolve error!\n");
        return -1;
    }

    memcpy(&proxy_addr.sin_addr, hostname->h_addr_list[0], hostname->h_length);

    // TODO: Routing
    char *user_agent = "Mozilla/5.0 (Windows NT 10.0; WOW64; rv:48.0) Gecko/20100101 Firefox/48.0";
    if (config.TARGET_PORT != 80)
    {
        sprintf(request, "GET / HTTP/1.0\nHost: %s:%d\nUser-Agent: %s\nConnection: close\n\n", config.TARGET_HOST, config.TARGET_PORT, user_agent);
    }
    else
    {
        sprintf(request, "GET / HTTP/1.0\nHost: %s\nUser-Agent: %s\nConnection: close\n\n", config.TARGET_HOST, user_agent);
    }

    if (config.LOGGING_VERBOSE)
    {
        printf(request);
    }

    if (connect(proxy_socket_fd, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0)
    {
        printf("ERROR: Unable to connect gateway!\n");
        return -1;
    }
    else if (config.LOGGING_VERBOSE)
    {
        printf("INFO: Connected gateway!\n");
    }

    write(proxy_socket_fd, request, strlen(request));

    struct timeval timeout;
    timeout.tv_sec = config.TIMEOUT_504_SECONDS;
    timeout.tv_usec = 0;
    setsockopt(proxy_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (recv(proxy_socket_fd, gateway_response_buffer, config.MEMORY_CONNECTION_BUFFER, 0) < 0)
    {
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
        {
            printf("ERROR: Gateway timeout!\n");
            *gateway_answered = 2;
            return -1;
        }
        printf("ERROR: Unable to receive HTTP response from gateway!\n");
        return -1;
    }
    else if (config.LOGGING_VERBOSE)
    {
        printf("INFO: HTTP response from gateway:\n%s\n", gateway_response_buffer);
    }

    *gateway_answered = 1;

    return 0;
}

int build_response(char *response)
{
    /*
        Builds desired response for connection
    */
    char *gateway_response_buffer[TEMP_BUFFER_SIZE];
    int gateway_answered = GATEWAY_STATUS_BAD;

    get_gateway_response(&gateway_answered, gateway_response_buffer);

    if (gateway_answered == GATEWAY_STATUS_BAD)
    {
        default_error_response(response, "502 Bad Gateway");
        return 0;
    }
    else if (gateway_answered == GATEWAY_STATUS_TIMEOUT)
    {
        default_error_response(response, "504 Gateway Timeout");
        return 0;
    }
    else if (gateway_answered == GATEWAY_STATUS_OK)
    {
        snprintf(response, config.MEMORY_CONNECTION_BUFFER,
                 gateway_response_buffer);
        return 0;
    }
    default_error_response(response, "500 Internal Server Error");
    return -1;
}

int serve_connections(int server_fd)
{
    /*
        Serves all connection on the given server socket
    */
    if (server_fd < 0)
    {
        return 1;
    }

    while (1)
    {
        if (config.LOGGING_VERBOSE)
        {
            printf("INFO: Waiting for new connection...\n");
        }

        // Connection address and acceptance
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0)
        {
            printf("WARN: Unable to accept connection!\n");
            continue;
        }

        if (config.LOGGING_VERBOSE)
        {
            printf("INFO: Connection accepted for fd=%d, waiting for the payload...\n", client_fd);
        }

        // Receive bytes from connection
        char *buffer = (char *)malloc(config.MEMORY_CONNECTION_BUFFER * sizeof(char));
        if (recv(client_fd, buffer, config.MEMORY_CONNECTION_BUFFER, 0) > 0)
        {
            if (config.LOGGING_VERBOSE)
            {
                printf("INFO: Received payload from connection...\n");
            }

            char *response = (char *)malloc(config.MEMORY_CONNECTION_BUFFER * 2 * sizeof(char));

            build_response(response);
            send(client_fd, response, (size_t)strlen(response), 0);
            free(response);
        }
        else
        {
            printf("WARN: Got connection, but not received payload!\n");
        }

        close(client_fd);
        free(buffer);

        if (config.LOGGING_VERBOSE)
        {
            printf("INFO: Connection finished and closed!\n");
            printf("\n");
        }
    }
}

int parse_and_load_config(int argc, char **argv)
{
    struct Config loaden_config = {
        .SOCKET_MAX_PENDING = 32,
        .SOCKET_BIND_PORT = 8081,
        .SOCKET_REUSE_ADDR = 1,
        .LOGGING_VERBOSE = 0,
        .TIMEOUT_504_SECONDS = 2,
        .MEMORY_CONNECTION_BUFFER = 1024,
        .TARGET_HOST = "localhost",
        .TARGET_PORT = 3000,
        .ERROR_SHOW_VERSION = 1};

    if (argc > 1)
    {
        loaden_config.TARGET_HOST = argv[1];
        if (argc > 2)
        {
            loaden_config.TARGET_PORT = atoi(argv[2]);
        }
        if (argc > 3)
        {
            loaden_config.LOGGING_VERBOSE = (strcmp(argv[3], "--verbose") == 0);
        }
    }

    if (loaden_config.LOGGING_VERBOSE)
    {
        printf("INFO: Config loaden for target %s:%d!\n", loaden_config.TARGET_HOST, loaden_config.TARGET_PORT);
    }
    else
    {
        printf("Startup... Logging disabled!");
    }

    config = loaden_config;
}

int main(int argc, char **argv)
{
    /*
        Core server launcher
        Currently arguments are not supported due to attended support for WSL
    */

    print_welcome_header();
    parse_and_load_config(argc, argv);
    return serve_connections(bind_and_listen_socket(create_socket(1)));
}
