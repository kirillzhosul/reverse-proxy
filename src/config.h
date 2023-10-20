typedef struct Config
{
    int SOCKET_MAX_PENDING;
    int SOCKET_BIND_PORT;
    int SOCKET_REUSE_ADDR;
    int TIMEOUT_504_SECONDS;
    int MEMORY_CONNECTION_BUFFER;
    int LOGGING_VERBOSE;
    int TARGET_PORT;
    char *TARGET_HOST;
    char *LOG_ERROR_PATH;
    char *LOG_ACCESS_PATH;
    int ERROR_SHOW_VERSION;
} Config;
