#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

// Définitions des constantes
#define PORT 8080 // Port du serveur
#define BUFFER_SIZE 1024 // Taille maximale des buffers pour les messages
#define MAX_DATAGRAM_SIZE 512 // Taille maximale des datagrammes UDP
#define SERVER_IP "127.0.0.1" // Adresse IP du serveur

// Enumération pour les protocoles supportés
typedef enum { PROTO_TCP, PROTO_UDP } Protocol;

// Structure pour l'en-tête des messages UDP
typedef struct {
    uint32_t seq; // Numéro de séquence du message
    char type[5]; // Type de message (ex. LIST, RSRV) + terminateur nul
    uint32_t len; // Longueur de la charge utile
} UdpHeader;

// Affiche un message de débogage avec horodatage
void debug_print(const char *msg, int sockfd) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    printf("[DEBUG] %s: %s (socket %d)\n", time_str, msg, sockfd);
}

// Crée un socket pour le protocole spécifié
int create_socket(Protocol proto) {
    // Créer un socket TCP ou UDP selon le protocole
    int sockfd = socket(AF_INET, proto == PROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Échec de la création du socket");
        return -1;
    }
    debug_print("Socket créé avec succès", sockfd);
    return sockfd;
}

// Programme principal
int main(int argc, char *argv[]) {
    // Vérifier les arguments
    if (argc != 2 || (strcmp(argv[1], "tcp") != 0 && strcmp(argv[1], "udp") != 0)) {
        fprintf(stderr, "Usage: %s <tcp|udp>\n", argv[0]);
        return 1;
    }
    Protocol proto = strcmp(argv[1], "tcp") == 0 ? PROTO_TCP : PROTO_UDP;

    // Créer le socket
    int sockfd = create_socket(proto);
    if (sockfd < 0) {
        return 1;
    }

    // Configurer l'adresse du serveur
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("Adresse IP invalide");
        close(sockfd);
        return 1;
    }

    if (proto == PROTO_TCP) {
        // Établir une connexion TCP
        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("Échec de la connexion au serveur");
            close(sockfd);
            return 1;
        }
        debug_print("Connexion TCP établie", sockfd);

        char command[BUFFER_SIZE];
        char response[BUFFER_SIZE];
        while (1) {
            // Lire la commande de l'utilisateur
            printf("Entrez une commande (LIST, RESERVER <ref> <nb> <agence>, ANNULER <ref> <nb> <agence>, FACTURE <agence>, ou quit pour quitter) : ");
            if (fgets(command, sizeof(command), stdin) == NULL) {
                break;
            }
            command[strcspn(command, "\n")] = '\0';
            if (strcmp(command, "quit") == 0) {
                break;
            }

            // Envoyer la commande au serveur
            if (write(sockfd, command, strlen(command)) < 0) {
                perror("Échec de l'envoi de la commande");
                break;
            }
            debug_print("Commande envoyée", sockfd);

            // Lire les réponses du serveur
            while (1) {
                ssize_t n = read(sockfd, response, BUFFER_SIZE - 1);
                if (n < 0) {
                    perror("Échec de la lecture de la réponse");
                    break;
                }
                if (n == 0) {
                    debug_print("Serveur déconnecté", sockfd);
                    break;
                }
                response[n] = '\0';
                printf("%s", response);
                // Arrêter la lecture si la réponse est complète
                if (strstr(response, "END\n") || strstr(response, "Réservation confirmée") || 
                    strstr(response, "Annulation confirmée") || strstr(response, "Succès : Facture") || 
                    strstr(response, "Impossible : Aucune facture") || strstr(response, "Erreur")) {
                    break;
                }
            }
        }
    } else {
        // Gérer les communications UDP
        char command[BUFFER_SIZE];
        char response[MAX_DATAGRAM_SIZE];
        socklen_t serv_len = sizeof(serv_addr);
        uint32_t seq = 0;

        while (1) {
            // Lire la commande de l'utilisateur
            printf("Entrez une commande (LIST, RESERVER <ref> <nb> <agence>, ANNULER <ref> <nb> <agence>, FACTURE <agence>, ou quit pour quitter) : ");
            if (fgets(command, sizeof(command), stdin) == NULL) {
                break;
            }
            command[strcspn(command, "\n")] = '\0';
            if (strcmp(command, "quit") == 0) {
                break;
            }

            // Préparer le datagramme UDP
            UdpHeader header = { seq++, "", (uint32_t)strlen(command) };
            strncpy(header.type, strncmp(command, "LIST", 4) == 0 ? "LIST" :
                                  strncmp(command, "RESERVER", 8) == 0 ? "RSRV" :
                                  strncmp(command, "ANNULER", 7) == 0 ? "ANUL" :
                                  strncmp(command, "FACTURE", 7) == 0 ? "FACT" : "ERR", 5);
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), command, strlen(command));

            // Envoyer le datagramme
            if (sendto(sockfd, packet, sizeof(UdpHeader) + strlen(command), 0, (struct sockaddr *)&serv_addr, serv_len) < 0) {
                perror("Échec de l'envoi de la commande");
                continue;
            }
            debug_print("Commande UDP envoyée", sockfd);

            // Recevoir la réponse
            ssize_t n = recvfrom(sockfd, response, MAX_DATAGRAM_SIZE, 0, (struct sockaddr *)&serv_addr, &serv_len);
            if (n < 0) {
                perror("Échec de la réception de la réponse");
                continue;
            }
            if (n >= sizeof(UdpHeader)) {
                UdpHeader resp_header;
                memcpy(&resp_header, response, sizeof(UdpHeader));
                char *payload = response + sizeof(UdpHeader);
                payload[resp_header.len] = '\0';
                printf("%s", payload);
            }
        }
    }

    // Fermer le socket
    close(sockfd);
    debug_print("Socket client fermé", sockfd);
    return 0;
}
