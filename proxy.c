#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h> /* getprotobyname */
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <regex.h>

#define MAX_REQUEST_LEN 1024
#define PROXY_PORT 1337

int main() {
    int proxy_socket, client_socket;
    struct sockaddr_in proxy_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    /* Create a proxy server socket */
    proxy_socket = socket(AF_INET, SOCK_STREAM, 0);
    
    /* Set up the proxy server address */
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = INADDR_ANY;
    proxy_addr.sin_port = htons(PROXY_PORT);

    /* Bind the proxy server socket */
    bind(proxy_socket, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr));
    
    /* Listen for incoming connections */
    listen(proxy_socket, 5);

    printf("[INFO] Proxy successfully started\n[INFO] Listening at port %d\n", PROXY_PORT);
    
    while (1) {
        /* Initiate a connection with client and proxy */
        client_socket = accept(proxy_socket, (struct sockaddr*)&client_addr, &client_len);

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        printf("[INFO] Connection from %s:%d\n", client_ip, client_addr.sin_port);

        /* Read client HTTP request */
        char client_request[BUFSIZ];
        size_t client_request_len = read(client_socket, client_request, BUFSIZ);

        int server_port = 80; /* Default port */
        char remote_address[BUFSIZ]; /* Default */

        /* Parse URL and Port from HTTP request for TCP connection */
        const char *host_header = "Host: ";
        const char *host_start = strstr(client_request, host_header);
        
        if (host_start != NULL) {
            host_start += strlen(host_header);
            const char *host_end = strchr(host_start, '\n');
            
            if (host_end != NULL) {
                int host_len = host_end - host_start;
                strncpy(remote_address, host_start, host_len);
                remote_address[host_len - 1] = '\0';
            } else {
                printf("[ERROR] Could not parse remote server address from request\n");
                continue;
            }
        } else {
            printf("[ERROR] Host header not found in the HTTP request.\n");
            continue;
        }

        char *port_separator = strstr(remote_address, ":");

        if (port_separator != NULL) {
            const char *port_end = strchr(port_separator, '\0');
            if (port_end != NULL) {
                int port_len = port_end - port_separator;
                char remote_port[6];
                strncpy(remote_port, port_separator + 1, port_len);
                remote_port[port_len - 1] = '\0';
                server_port = atoi(remote_port);

                int idx = 0;
                while (1) {
                    if (remote_address[idx] == ':') {
                        remote_address[idx] = '\0';
                        break;
                    }
                    idx++;
                }
            } else {
                printf("[ERROR] Could not parse remote server port from request\n");
                continue;
            }
        }

        printf("%s\n", remote_address);
        printf("%d\n", server_port);

        /* Modify the request */
        // char modified_request[BUFSIZ];
        // char request_template[] = "GET / HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: custom_proxy/1.1\r\n\r\n";

        // int modified_request_len = snprintf(modified_request, MAX_REQUEST_LEN, request_template, hostname, server_port);
        // if (modified_request_len >= MAX_REQUEST_LEN) {
        //     fprintf(stderr, "[ERROR] Request too big: %d bytes\n", modified_request_len);
        //     continue;
        // }

        /* Build the socket with server */
        struct protoent *protoent = getprotobyname("tcp");
        if (protoent == NULL) {
            perror("[ERROR] getprotobyname");
            continue;
        }

        int server_socket = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
        if (server_socket == -1) {
            perror("[ERROR] socket");
            continue;
        }

        /* Build the address */
        struct hostent *hostent = gethostbyname(remote_address);
        if (hostent == NULL) {
            fprintf(stderr, "[ERROR] gethostbyname(\"%s\")\n", remote_address);
            continue;
        }

        in_addr_t in_addr = inet_addr(inet_ntoa(*(struct in_addr*)*(hostent->h_addr_list)));
        if (in_addr == (in_addr_t)-1) {
            fprintf(stderr, "[ERROR] inet_addr(\"%s\")\n", *(hostent->h_addr_list));
            continue;
        }

        struct sockaddr_in sockaddr_in;
        sockaddr_in.sin_addr.s_addr = in_addr;
        sockaddr_in.sin_family = AF_INET;
        sockaddr_in.sin_port = htons(server_port);

        printf("connecting...\n");

        /* Make connect non-blocking */
        fcntl(server_socket, F_SETFL, O_NONBLOCK);

        /* Establish TCP connection with the server */
        if (connect(server_socket, (struct sockaddr*)&sockaddr_in, sizeof(sockaddr_in)) == -1) {
            perror("[ERROR] connect");
            continue;
        }

        struct timeval tv;
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(server_socket, &fdset);
        tv.tv_sec = 10;             /* 10 second timeout */
        tv.tv_usec = 0;

        if (select(server_socket + 1, NULL, &fdset, NULL, &tv) == 1) {
            int so_error;
            socklen_t len = sizeof so_error;

            getsockopt(server_socket, SOL_SOCKET, SO_ERROR, &so_error, &len);

            if (so_error == 0) {
                printf("%s:%d is open\n", remote_address, server_port);
            }
        }

        /* Send modified HTTP request to the server */
        size_t nbytes_total, nbytes_last;
        nbytes_total = 0;
        while (nbytes_total < client_request_len) {
            nbytes_last = write(server_socket, client_request + nbytes_total, client_request_len - nbytes_total);
            if ((int)nbytes_last == -1) perror("[ERROR] write");
            nbytes_total += nbytes_last;
        }

        /* Send HTTP response from returned by server back to client */
        char server_response[BUFSIZ];
        nbytes_total = read(server_socket, server_response, BUFSIZ);
        write(client_socket, server_response, nbytes_total);
        
        if ((int)nbytes_total == -1) {
            perror("[ERROR] read");
            continue;
        }

        /* Close both client and server sockets */
        close(server_socket);
        close(client_socket);
    }
    
    close(proxy_socket);
    return 0;
}
