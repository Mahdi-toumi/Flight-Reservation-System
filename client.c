#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sockfd = -1;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    char agence[50];

    // Get agency name from executable name
    char *exec_name = basename(argv[0]); // Extract base name (e.g., "Mahdi" from "./Mahdi")
    strncpy(agence, exec_name, sizeof(agence) - 1);
    agence[sizeof(agence) - 1] = '\0'; // Ensure null-termination
    if (strlen(agence) == 0) {
        fprintf(stderr, "%s: Nom de l'agence vide\n", argv[0]);
        return 1;
    }
    printf("%s: Agence saisie : %s\n", argv[0], agence); // Debug output with executable name

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Erreur création socket");
        return 1;
    }
    printf("%s: Socket créé avec succès\n", argv[0]); // Debug output

    // Configure server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "192.168.1.178", &serv_addr.sin_addr) <= 0) {
        perror("Erreur adresse IP");
        close(sockfd);
        return 1;
    }
    printf("%s: Adresse serveur configurée\n", argv[0]); // Debug output

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Erreur connexion serveur");
        close(sockfd);
        return 1;
    }
    printf("%s: Connexion au serveur établie\n", argv[0]); // Debug output

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
            printf("%s: Choix invalide\n", argv[0]);
            continue;
        }
        while (getchar() != '\n'); // Clear input buffer

        if (choix == 0) {
            printf("%s: Déconnexion...\n", argv[0]);
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
                printf("%s: Commande LIST envoyée\n", argv[0]); // Debug output
                printf("\nListe des vols :\n");
                while (1) {
                    ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
                    if (n < 0) {
                        perror("Erreur lecture réponse LIST");
                        close(sockfd);
                        return 1;
                    }
                    if (n == 0) {
                        printf("%s: Connexion fermée par le serveur\n", argv[0]);
                        close(sockfd);
                        return 1;
                    }
                    buffer[n] = '\0';
                    printf("%s", buffer); // Print each chunk
                    if (strstr(buffer, "END\n") != NULL) {
                        printf("%s: Marqueur END reçu\n", argv[0]); // Debug output
                        break;
                    }
                }
                printf("%s: Liste des vols affichée\n", argv[0]); // Debug output
                break;
            }

            case 2: {
                int ref, nb;
                printf("Référence du vol : ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("%s: Référence invalide\n", argv[0]);
                    while (getchar() != '\n');
                    continue;
                }
                printf("Nombre de places : ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("%s: Nombre de places invalide\n", argv[0]);
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
                printf("%s: Commande RESERVER envoyée\n", argv[0]); // Debug output
                break;
            }

            case 3: {
                int ref, nb;
                printf("Référence du vol à annuler : ");
                if (scanf("%d", &ref) != 1 || ref < 0) {
                    printf("%s: Référence invalide\n", argv[0]);
                    while (getchar() != '\n');
                    continue;
                }
                printf("Nombre de places à annuler : ");
                if (scanf("%d", &nb) != 1 || nb <= 0) {
                    printf("%s: Nombre de places invalide\n", argv[0]);
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
                printf("%s: Commande ANNULER envoyée\n", argv[0]); // Debug output
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
                printf("%s: Commande FACTURE envoyée\n", argv[0]); // Debug output
                break;
            }

            default:
                printf("%s: Choix invalide\n", argv[0]);
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
                printf("%s: Connexion fermée par le serveur\n", argv[0]);
                close(sockfd);
                return 1;
            }
            buffer[n] = '\0';
            printf("%s: Réponse :\n%s\n", argv[0], buffer);
        }
    }

    close(sockfd);
    printf("%s: Connexion fermée\n", argv[0]); // Debug output
    return 0;
}
