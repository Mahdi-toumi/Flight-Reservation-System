#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int read_message(int sock, char *buffer, int bufsize) {
    int len;
    int n = read(sock, &len, sizeof(int));
    if (n <= 0 || len <= 0 || len >= bufsize) return -1;

    int total_read = 0;
    while (total_read < len) {
        int r = read(sock, buffer + total_read, len - total_read);
        if (r <= 0) return -1;
        total_read += r;
    }

    buffer[total_read] = '\0';
    return total_read;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    if (argc == 0) {
        fprintf(stderr, "Erreur : nom de l'exécutable non disponible\n");
        exit(1);
    }

    char *exe_name = basename(argv[0]);
    strncpy(agence, exe_name, sizeof(agence) - 1);
    agence[sizeof(agence) - 1] = '\0';

    printf("Nom de l'agence détecté : %s\n", agence);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "172.22.1.1", &serv_addr.sin_addr) <= 0) {
        perror("Adresse invalide");
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    char input[100];
    int choix;

    while (1) {
        printf("\n--- MENU ---\n");
        printf("1. Afficher tous les vols\n");
        printf("2. Réserver un vol\n");
        printf("3. Annuler une réservation\n");
        printf("4. Voir la facture\n");
        printf("0. Quitter\n");
        printf("Choix : ");

        fgets(input, sizeof(input), stdin);
        if (sscanf(input, "%d", &choix) != 1) {
            fprintf(stderr, "Entrée invalide.\n");
            continue;
        }

        if (choix == 0) break;

        switch (choix) {
            case 1:
                write(sockfd, "LIST", 4);
                break;
            case 2: {
                int ref, nb;
                printf("Vol réf : ");
                fgets(input, sizeof(input), stdin);
                if (sscanf(input, "%d", &ref) != 1) { printf("Réf invalide\n"); continue; }

                printf("Nb places : ");
                fgets(input, sizeof(input), stdin);
                if (sscanf(input, "%d", &nb) != 1) { printf("Nb invalide\n"); continue; }

                snprintf(buffer, BUFFER_SIZE, "RESERVER %d %d %s", ref, nb, agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            }
            case 3: {
                int ref, nb;
                printf("Vol réf à annuler : ");
                fgets(input, sizeof(input), stdin);
                if (sscanf(input, "%d", &ref) != 1) { printf("Réf invalide\n"); continue; }

                printf("Nb places : ");
                fgets(input, sizeof(input), stdin);
                if (sscanf(input, "%d", &nb) != 1) { printf("Nb invalide\n"); continue; }

                snprintf(buffer, BUFFER_SIZE, "ANNULER %d %d %s", ref, nb, agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            }
            case 4:
                snprintf(buffer, BUFFER_SIZE, "FACTURE %s", agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            default:
                printf("Choix invalide\n");
                continue;
        }

        int n = read_message(sockfd, buffer, BUFFER_SIZE);
        if (n > 0) {
            printf("\n%s", buffer);
        } else {
            printf("Erreur de lecture depuis le serveur.\n");
            continue;
        }
    }

    close(sockfd);
    return 0;
}
