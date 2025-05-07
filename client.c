#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sockfd = -1;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    // Input validation for agency name
    printf("Nom de l'agence : ");
    if (fgets(agence, sizeof(agence), stdin) == NULL) {
        fprintf(stderr, "Erreur de lecture du nom de l'agence\n");
        return 1;
    }
    agence[strcspn(agence, "\n")] = 0; // Remove newline
    if (strlen(agence) == 0) {
        fprintf(stderr, "Nom de l'agence vide\n");
        return 1;
    }
    printf("Agence saisie : %s\n", agence); // Debug output

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erreur création socket");
        return 1;
    }
    printf("Socket créé avec succès\n"); // Debug output

    // Configure server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "192.168.1.178", &serv_addr.sin_addr) <= 0) {
        perror("Erreur adresse IP");
        close(sockfd);
        return 1;
    }
    printf("Adresse serveur configurée\n"); // Debug output

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erreur connexion serveur");
        close(sockfd);
        return 1;
    }
    printf("Connexion au serveur établie\n"); // Debug output

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
            while (getchar() != '\n'); // Clear invalid input
            printf("Choix invalide\n");
            continue;
        }
        while (getchar() != '\n'); // Clear input buffer

        if (choix == 0) {
            printf("Déconnexion...\n");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);

        switch (choix) {
            case 1: {
                if (write(sockfd, "LIST", 4) != 4) {
                    perror("Erreur envoi commande LIST");
                    close(sockfd);
                    return 1;
                }
                printf("Commande LIST envoyée\n"); // Debug output
                printf("\nListe des vols :\n");
                while (1) {
                    ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
                    if (n < 0) {
                        perror("Erreur lecture réponse LIST");
                        close(sockfd);
                        return 1;
                    }
                    if (n == 0) {
                        printf("Connexion fermée par le serveur\n");
                        close(sockfd);
                        return 1;
                    }
                    buffer[n] = '\0';
                    printf("%s", buffer); // Print each chunk
                    if (strstr(buffer, "END\n") != NULL) {
                        printf("Marqueur END reçu\n"); // Debug output
                        break;
                    }
                }
                printf("Liste des vols affichée\n"); // Debug output
                break;
            }

            case 2: {
                int ref, nb;
                printf("Référence du vol : ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("Référence invalide\n");
                    while (getchar() != '\n');
                    continue;
                }
                printf("Nombre de places : ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("Nombre de places invalide\n");
                    while (getchar() != '\n');
                    continue;
                }
                while (getchar() != '\n');
                snprintf(buffer, BUFFER_SIZE, "RESERVER %d %d %s", ref, nb, agence);
                size_t len = strlen(buffer);
                if (write(sockfd, buffer, len) != len) {
                    perror("Erreur envoi réservation");
                    close(sockfd);
                    return 1;
                }
                printf("Commande RESERVER envoyée\n"); // Debug output
                break;
            }

            case 3: {
                int ref, nb;
                printf("Référence du vol à annuler : ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("Référence invalide\n");
                    while (getchar() != '\n');
                    continue;
                }
                printf("Nombre de places à annuler : ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("Nombre de places invalide\n");
                    while (getchar() != '\n');
                    continue;
                }
                while (getchar() != '\n');
                snprintf(buffer, BUFFER_SIZE, "ANNULER %d %d %s", ref, nb, agence);
                size_t len = strlen(buffer);
                if (write(sockfd, buffer, len) != len) {
                    perror("Erreur envoi annulation");
                    close(sockfd);
                    return 1;
                }
                printf("Commande ANNULER envoyée\n"); // Debug output
                break;
            }

            case 4: {
                snprintf(buffer, BUFFER_SIZE, "FACTURE %s", agence);
                size_t len = strlen(buffer);
                if (write(sockfd, buffer, len) != len) {
                    perror("Erreur envoi demande facture");
                    close(sockfd);
                    return 1;
                }
                printf("Commande FACTURE envoyée\n"); // Debug output
                break;
            }

            default:
                printf("Choix invalide\n");
                continue;
        }

        if (choix != 1) {
            ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
            if (n < 0) {
                perror("Erreur lecture réponse");
                close(sockfd);
                return 1;
            }
            if (n == 0) {
                printf("Connexion fermée par le serveur\n");
                close(sockfd);
                return 1;
            }
            buffer[n] = '\0';
            printf("Réponse :\n%s\n", buffer);
        }
    }

    close(sockfd);
    printf("Connexion fermée\n"); // Debug output
    return 0;
}
