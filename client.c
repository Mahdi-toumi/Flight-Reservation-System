#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <sys/select.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_DATAGRAM_SIZE 512
#define UDP_TIMEOUT_SEC 1
#define UDP_MAX_RETRIES 3

typedef enum { PROTO_TCP, PROTO_UDP } Protocol;

// UDP message header
typedef struct {
    uint32_t seq; // Sequence number
    char type[5]; // Message type (e.g., LIST, WAIT) + null terminator
    uint32_t len; // Payload length
} UdpHeader;

int create_socket(Protocol proto) {
    int sockfd = socket(AF_INET, proto == PROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Failed to create socket");
        return -1;
    }
    return sockfd;
}

int send_udp_request(int sockfd, struct sockaddr_in *serv_addr, char *buffer, size_t len, char *response, size_t resp_size) {
    static uint32_t seq = 0;
    UdpHeader header = { seq++, "", (uint32_t)len };
    strncpy(header.type, strncmp(buffer, "LIST", 4) == 0 ? "LIST" : 
                        strncmp(buffer, "RESERVER", 8) == 0 ? "RSRV" : 
                        strncmp(buffer, "ANNULER", 7) == 0 ? "ANUL" : 
                        strncmp(buffer, "FACTURE", 7) == 0 ? "FACT" : "UNKN", 5);

    char packet[MAX_DATAGRAM_SIZE];
    memcpy(packet, &header, sizeof(UdpHeader));
    memcpy(packet + sizeof(UdpHeader), buffer, len);
    size_t packet_len = sizeof(UdpHeader) + len;

    struct timeval tv = { UDP_TIMEOUT_SEC, 0 };
    int retries = 0;
    ssize_t n;

    while (retries < UDP_MAX_RETRIES) {
        if (sendto(sockfd, packet, packet_len, 0, (struct sockaddr *)serv_addr, sizeof(*serv_addr)) < 0) {
            perror("Failed to send UDP packet");
            return -1;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        int ready = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            perror("Error in select");
            return -1;
        }
        if (ready == 0) {
            retries++;
            printf("Timeout, retry %d/%d\n", retries, UDP_MAX_RETRIES);
            continue;
        }

        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        n = recvfrom(sockfd, packet, MAX_DATAGRAM_SIZE, 0, (struct sockaddr *)&from_addr, &from_len);
        if (n < 0) {
            perror("Failed to receive UDP packet");
            return -1;
        }

        if (n < sizeof(UdpHeader)) {
            printf("Received datagram too short\n");
            continue;
        }

        UdpHeader recv_header;
        memcpy(&recv_header, packet, sizeof(UdpHeader));
        if (recv_header.seq != header.seq) {
            printf("Incorrect sequence number, ignoring\n");
            continue;
        }

        size_t payload_len = n - sizeof(UdpHeader);
        if (payload_len > resp_size - 1) {
            payload_len = resp_size - 1;
        }
        memcpy(response, packet + sizeof(UdpHeader), payload_len);
        response[payload_len] = '\0';

        if (strncmp(recv_header.type, "WAIT", 4) == 0) {
            printf("%s\n", response);
            continue; // Wait for next packet
        }

        return payload_len;
    }

    printf("Failed after %d retries\n", UDP_MAX_RETRIES);
    return -1;
}

int main(int argc, char *argv[]) {
    Protocol proto = PROTO_TCP;
    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            proto = PROTO_TCP;
        } else if (strcmp(argv[1], "udp") == 0) {
            proto = PROTO_UDP;
        } else {
            fprintf(stderr, "Invalid protocol (use 'tcp' or 'udp')\n");
            return 1;
        }
    }

    int sockfd = -1;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    // Get agency name from executable name
    char *exec_name = basename(argv[0]);
    strncpy(agence, exec_name, sizeof(agence) - 1);
    agence[sizeof(agence) - 1] = '\0';
    if (strlen(agence) == 0) {
        fprintf(stderr, "Agency name cannot be empty\n");
        return 1;
    }

    // Create socket
    sockfd = create_socket(proto);
    if (sockfd < 0) {
        return 1;
    }

    // Configure server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid server IP address");
        close(sockfd);
        return 1;
    }

    // Connect for TCP
    if (proto == PROTO_TCP) {
        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Failed to connect to server");
            close(sockfd);
            return 1;
        }
    }

    int choix;
    while (1) {
        printf("\n===== Flight Reservation Menu =====\n");
        printf("1. List all flights\n");
        printf("2. Reserve a flight\n");
        printf("3. Cancel a reservation\n");
        printf("4. View invoice\n");
        printf("0. Exit\n");
        printf("Enter choice: ");
        if (scanf("%d", &choix) != 1) {
            while (getchar() != '\n');
            printf("Invalid choice\n");
            continue;
        }
        while (getchar() != '\n');

        if (choix == 0) {
            printf("Disconnecting...\n");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);

        switch (choix) {
            case 1: {
                strncpy(buffer, "LIST", 5);
                size_t len = strlen(buffer);
                if (proto == PROTO_TCP) {
                    if (write(sockfd, buffer, len) != len) {
                        perror("Failed to send LIST command");
                        close(sockfd);
                        return 1;
                    }
                    printf("\nAvailable Flights:\n");
                    while (1) {
                        ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
                        if (n < 0) {
                            perror("Error reading LIST response");
                            close(sockfd);
                            return 1;
                        }
                        if (n == 0) {
                            printf("Server closed connection\n");
                            close(sockfd);
                            return 1;
                        }
                        buffer[n] = '\0';
                        if (strncmp(buffer, "WAIT", 4) == 0) {
                            printf("%s\n", buffer + 5);
                            continue;
                        }
                        printf("%s", buffer);
                        if (strstr(buffer, "END\n") != NULL) {
                            break;
                        }
                    }
                } else { // UDP
                    printf("\nAvailable Flights:\n");
                    ssize_t n = send_udp_request(sockfd, &serv_addr, buffer, len, buffer, BUFFER_SIZE);
                    if (n < 0) {
                        close(sockfd);
                        return 1;
                    }
                    while (strncmp(buffer, "END", 3) != 0) {
                        printf("%s", buffer);
                        n = send_udp_request(sockfd, &serv_addr, "LIST", 4, buffer, BUFFER_SIZE);
                        if (n < 0) {
                            close(sockfd);
                            return 1;
                        }
                    }
                    printf("END\n");
                }
                break;
            }

            case 2: {
                int ref, nb;
                printf("Enter flight reference: ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("Invalid flight reference\n");
                    while (getchar() != '\n');
                    continue;
                }
                printf("Enter number of seats: ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("Invalid number of seats\n");
                    while (getchar() != '\n');
                    continue;
                }
                while (getchar() != '\n');
                snprintf(buffer, BUFFER_SIZE, "RESERVER %d %d %s", ref, nb, agence);
                size_t len = strlen(buffer);
                if (proto == PROTO_TCP) {
                    if (write(sockfd, buffer, len) != len) {
                        perror("Failed to send reservation");
                        close(sockfd);
                        return 1;
                    }
                } else { // UDP
                    ssize_t n = send_udp_request(sockfd, &serv_addr, buffer, len, buffer, BUFFER_SIZE);
                    if (n < 0) {
                        close(sockfd);
                        return 1;
                    }
                }
                break;
            }

            case 3: {
                int ref, nb;
                printf("Enter flight reference to cancel: ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("Invalid flight reference\n");
                    while (getchar() != '\n');
                    continue;
                }
                printf("Enter number of seats to cancel: ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("Invalid number of seats\n");
                    while (getchar() != '\n');
                    continue;
                }
                while (getchar() != '\n');
                snprintf(buffer, BUFFER_SIZE, "ANNULER %d %d %s", ref, nb, agence);
                size_t len = strlen(buffer);
                if (proto == PROTO_TCP) {
                    if (write(sockfd, buffer, len) != len) {
                        perror("Failed to send cancellation");
                        close(sockfd);
                        return 1;
                    }
                } else { // UDP
                    ssize_t n = send_udp_request(sockfd, &serv_addr, buffer, len, buffer, BUFFER_SIZE);
                    if (n < 0) {
                        close(sockfd);
                        return 1;
                    }
                }
                break;
            }

            case 4: {
                snprintf(buffer, BUFFER_SIZE, "FACTURE %s", agence);
                size_t len = strlen(buffer);
                if (proto == PROTO_TCP) {
                    if (write(sockfd, buffer, len) != len) {
                        perror("Failed to send invoice request");
                        close(sockfd);
                        return 1;
                    }
                } else { // UDP
                    ssize_t n = send_udp_request(sockfd, &serv_addr, buffer, len, buffer, BUFFER_SIZE);
                    if (n < 0) {
                        close(sockfd);
                        return 1;
                    }
                }
                break;
            }

            default:
                printf("Invalid choice\n");
                continue;
        }

        if (choix != 1) {
            if (proto == PROTO_TCP) {
                while (1) {
                    ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
                    if (n < 0) {
                        perror("Error reading response");
                        close(sockfd);
                        return 1;
                    }
                    if (n == 0) {
                        printf("Server closed connection\n");
                        close(sockfd);
                        return 1;
                    }
                    buffer[n] = '\0';
                    if (strncmp(buffer, "WAIT", 4) == 0) {
                        printf("%s\n", buffer + 5);
                        continue;
                    }
                    printf("\nResponse:\n%s\n", buffer);
                    break;
                }
            } else { // UDP response already handled in send_udp_request
                printf("\nResponse:\n%s\n", buffer);
            }
        }
    }

    close(sockfd);
    printf("Connection closed\n");
    return 0;
}
