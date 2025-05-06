#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    printf("Nom de l'agence : ");
    scanf("%s", agence);

    // Création socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

	
    if (inet_pton(AF_INET, "172.22.1.1", &serv_addr.sin_addr) <= 0) {
        perror("Adresse invalide"); exit(1);
    }

    // Connexion
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect"); exit(1);
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
        scanf("%d", &choix);

        if (choix == 0) break;
        switch (choix) {
            case 1:
                write(sockfd, "LIST", 4);
                break;
            case 2: {
                int ref, nb;
                printf("Vol réf : "); scanf("%d", &ref);
                printf("Nb places : "); scanf("%d", &nb);
                sprintf(buffer, "RESERVER %d %d %s", ref, nb, agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            }
            case 3: {
                int ref, nb;
                printf("Vol réf à annuler : "); scanf("%d", &ref);
                printf("Nb places : "); scanf("%d", &nb);
                sprintf(buffer, "ANNULER %d %d %s", ref, nb, agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            }
            case 4:
                sprintf(buffer, "FACTURE %s", agence);
                write(sockfd, buffer, strlen(buffer));
                break;
            default:
                printf("Choix invalide\n");
                continue;
        }

        // Lecture réponse
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sockfd, buffer, BUFFER_SIZE-1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("\n%s", buffer);
        }
    }

    close(sockfd);
    return 0;
}
