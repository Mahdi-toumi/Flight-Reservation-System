#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>  // Pour basename

#define PORT 8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    if (argc == 0) {
        fprintf(stderr, "Erreur : nom de l'exécutable non disponible\n");
        exit(1);
    }

    // Récupération du nom de l'exécutable comme nom d'agence
    char *exe_name = basename(argv[0]);
    strncpy(agence, exe_name, sizeof(agence) - 1);
    agence[sizeof(agence) - 1] = '\0';

    printf("Nom de l'agence détecté : %s\n", agence);

    // Création de la socket
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

    // Connexion au serveur
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    int choix;
    while (1) {
        printf("\n--- MENU ---\n");
        printf("1. Afficher tous les vols\n");
        printf("2. Réserver un vol\n");
        printf("3. Annuler une réservation\n");
        printf("4. Voir la facture\n");
        printf("0. Quitter\n");
        printf("Choix : ");
        if (scanf("%d", &choix) != 1) {
            fprintf(stderr, "Entrée invalide.\n");
            while (getchar() != '\n');  // Vider le buffer
            continue;
        }

        if (choix == 0) break;

        switch (choix) {
            case 1:
                write(sockfd, "LIST", 4);
                break;

            case 2: {
                int ref, nb;
                printf("Vol réf : "); scanf("%d", &ref);
                printf("Nb places : "); scanf("%d", &nb);
                snprintf(buffer, BUFFER_SIZE, "RESERVER %d %d %s", ref, nb, agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            }

            case 3: {
                int ref, nb;
                printf("Vol réf à annuler : "); scanf("%d", &ref);
                printf("Nb places : "); scanf("%d", &nb);
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

        // Lecture de la réponse du serveur
        printf("\n");
        while (1) {
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (n <= 0) break;

            buffer[n] = '\0';

            if (strcmp(buffer, "END\n") == 0 || strcmp(buffer, "END\r\n") == 0) {
                break;
            }

            printf("%s", buffer);
        }
    }

    close(sockfd);
    return 0;
}
