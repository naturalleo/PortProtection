
#include "socket_client.h"


void _init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void _cleanup_openssl() {
    EVP_cleanup();
}

SSL_CTX *_create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void _configure_context(SSL_CTX *ctx, char *cert_path, char *key_path) {
    SSL_CTX_set_ecdh_auto(ctx, 1);
    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

int create_socket_with_port(ushort port) {
    int s;
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t) port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (verbose) {
        printf("Initializing socket...\n");
    }

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (s < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(s, 8) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }
    return s;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

void socket_daemon(int socket) {
    while (true) {
        struct sockaddr_in *addr;
        uint len = sizeof(addr);

        addr = malloc(sizeof(struct sockaddr_in));
        int client = accept(socket, (struct sockaddr *) addr, &len);
        if (client < 0) {
            perror("Unable to accept");
            break;
        } else {
            Converter converter;
            converter.data = client;
            pthread_t t;
            pthread_create(&t, NULL, (void *) handle_accept, converter.ptr);
        }
    }
    close(socket);
    SSL_CTX_free(ctx);
    _cleanup_openssl();
    printf("Socket closed with error\n");
    stop_timer(-1);
}

#pragma clang diagnostic pop

void handle_accept(int client) {
    char *response = malloc(strlen(RESPONSE_HTTP_BAD_REQUEST) + 256);
    char *buffer = malloc(BUFFER_SIZE);
    char *tmp = malloc(10);

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, client);

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } else {
        int ret = SSL_read(ssl, buffer, BUFFER_SIZE);
        if (ret > 0) {
            buffer[ret] = 0;//end str
            Request *request = handle_request(buffer);
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            getpeername(client, (struct sockaddr *) &addr, &len);
            request->addr = addr_to_ip_str(addr.sin_addr);
            if (verbose) {
                printf("Connection from %s\n", request->addr);
            }
            if (request->time < 0 || request->type == HTTP_ERROR || request->type == RAW_ERROR || !authorize_with_request(request)) {
                if (request->type == HTTP) {
                    request->type = HTTP_ERROR;
                } else if (request->type == RAW) {
                    request->type = RAW_ERROR;
                }
            }
            if (verbose) {
                if (request->type == HTTP || request->type == RAW) {
                    printf("Authorized client %s, +%dh\n", request->addr, request->time);
                }
            }
            switch (request->type) {
                case HTTP:
                    sprintf(tmp, "+%dh\n", request->time);
                    sprintf(response, RESPONSE_HTTP_OK, strlen(tmp), tmp);
                    SSL_write(ssl, response, (int) strlen(response));
                    break;
                case RAW:
                    sprintf(tmp, "+%dh\n", request->time);
                    sprintf(response, RESPONSE_RAW_OK, tmp);
                    SSL_write(ssl, response, (int) strlen(response));
                    break;
                case HTTP_ERROR:
                    SSL_write(ssl, RESPONSE_HTTP_BAD_REQUEST, (int) strlen(RESPONSE_HTTP_BAD_REQUEST));
                    break;
                case RAW_ERROR:
                    SSL_write(ssl, RESPONSE_RAW_BAD_REQUEST, (int) strlen(RESPONSE_RAW_BAD_REQUEST));
                    break;
                default:
                    break;
            }
            free_request(request);
        }
    }
    SSL_free(ssl);
    close(client);

    free(tmp);
    free(buffer);
    free(response);
}


void run_socket_client(ushort port, char *cert, char *key) {
    _init_openssl();
    ctx = _create_context();

    _configure_context(ctx, cert, key);

    sock = create_socket_with_port(port);

    if (verbose) {
        printf("Creating socket thread...\n");
    }

    Converter converter;
    converter.data = sock;
    pthread_t t;
    pthread_create(&t, NULL, (void *) socket_daemon, converter.ptr);
    //cleaning up is in pthread, maybe never execute
}

char *addr_to_ip_str(struct in_addr addr) {

    /**
     *
     * NOTE: `inet_ntoa` returns a string that will be destroyed the next time it's called,
     * and it cannot be `free` ,
     * so you should use `strdup` to copy it
     */
    char *result = strdup(inet_ntoa(addr));
/*    in_addr_t addr_h = ntohl(addr.s_addr);
    char *result = malloc(strlen("255.255.255.255") + 1);
    sprintf(result,
            "%d.%d.%d.%d",
            addr_h >> 24 & 0xff,
            addr_h >> 16 & 0xff,
            addr_h >> 8 & 0xff,
            addr_h & 0xff);*/
    return result;
}
