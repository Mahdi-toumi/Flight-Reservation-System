#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8082
#define BUFFER_SIZE 1024

void afficherMenu() {
    printf("\n===== Menu Agence =====\n");
    printf("1. Réserver des places\n");
    printf("2. Annuler une réservation\n");
    printf("3. Quitter\n");
    printf("4. Générer les factures\n"); 
    printf("Choix : ");
}

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char requete[BUFFER_SIZE];
    int choix, refVol, nbPlaces;

    // Création du socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Erreur socket");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Adresse invalide");
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connexion échouée");
        return 1;
    }

    printf("Connecté au serveur.\n");

    while (1) {
        afficherMenu();
        scanf("%d", &choix);

        if (choix == 1 || choix == 2) {
            printf("Entrez la référence du vol : ");
            scanf("%d", &refVol);
            printf("Entrez le nombre de places : ");
            scanf("%d", &nbPlaces);

            // Construire la requête à envoyer
            sprintf(requete, "%s %d %d", (choix == 1 ? "DEMANDE" : "ANNULATION"), refVol, nbPlaces);
            send(sock, requete, strlen(requete), 0);

            // Lire la réponse du serveur
            memset(buffer, 0, BUFFER_SIZE);
            read(sock, buffer, BUFFER_SIZE);
            printf("Réponse du serveur : %s\n", buffer);

        } else if (choix == 3) {
            printf("Déconnexion.\n");
            break;
        }
	else if (choix == 4) {
	    strcpy(requete, "FACTURE");
	    send(sock, requete, strlen(requete), 0);
	    memset(buffer, 0, BUFFER_SIZE);
	    read(sock, buffer, BUFFER_SIZE);
   	 printf("Réponse du serveur : %s\n", buffer);
	}

       	else {
            printf("Choix invalide. Réessayez.\n");
        }
    }

    close(sock);
    return 0;
}

