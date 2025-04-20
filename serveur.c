#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h> // Pour socket/bind/listen/accept

#define PORT 8082
#define BUFFER_SIZE 1024

typedef struct {
    int ref;
    char destination[50];
    int places;
    int prix;
} Vol;

// Déclaration des fonctions
int chargerVols(Vol vols[], int max);
void sauvegarderVols(Vol vols[], int n);
void ajouterHistorique(int refVol, int idAgence, const char *transaction, int valeur, const char *resultat);
char* traiterRequete(char *requete, int idAgence);
void genererFactures(); 
// Fonction pour lire les vols depuis le fichier
int chargerVols(Vol vols[], int max) {
    FILE *f = fopen("vols.txt", "r");
    if (!f) {
        perror("Erreur ouverture vols.txt");
        return 0;
    }

    char ligne[128];
    int count = 0;

    fgets(ligne, sizeof(ligne), f); // Ignorer l'en-tête

    while (fgets(ligne, sizeof(ligne), f) && count < max) {
        sscanf(ligne, "%d %s %d %d", &vols[count].ref, vols[count].destination, &vols[count].places, &vols[count].prix);
        count++;
    }

    fclose(f);
    return count;
}

// Fonction pour sauvegarder les vols dans le fichier
void sauvegarderVols(Vol vols[], int n) {
    FILE *f = fopen("vols.txt", "w");
    if (!f) return;

    fprintf(f, "Référence Vol  Destination  Nombre Places  Prix Place\n");
    for (int i = 0; i < n; i++) {
        fprintf(f, "%d %s %d %d\n", vols[i].ref, vols[i].destination, vols[i].places, vols[i].prix);
    }

    fclose(f);
}

// Historique des transactions
void ajouterHistorique(int refVol, int idAgence, const char *transaction, int valeur, const char *resultat) {
    FILE *f = fopen("histo.txt", "a");
    if (!f) {
        perror("Erreur ouverture histo.txt");
        return;
    }

    fprintf(f, "%d %d %s %d %s\n", refVol, idAgence, transaction, valeur, resultat);
    fclose(f);
}

// Traitement de la requête reçue
char* traiterRequete(char *requete, int idAgence) {
    static char reponse[BUFFER_SIZE];
    char type[20];
    int ref, nb;

    Vol vols[100];
    int n = chargerVols(vols, 100);

    sscanf(requete, "%s %d %d", type, &ref, &nb);

    for (int i = 0; i < n; i++) {
        if (vols[i].ref == ref) {
            if (strcmp(type, "DEMANDE") == 0) {
                if (vols[i].places >= nb) {
                    vols[i].places -= nb;
                    sauvegarderVols(vols, n);
                    ajouterHistorique(ref, idAgence, "Demande", nb, "succès");
                    snprintf(reponse, sizeof(reponse), "Réservation réussie pour %d places sur vol %d", nb, ref);
                } else {
                    ajouterHistorique(ref, idAgence, "Demande", nb, "impossible");
                    snprintf(reponse, sizeof(reponse), "Réservation impossible : pas assez de places");
                }
            } else if (strcmp(type, "ANNULATION") == 0) {
                vols[i].places += nb;
                sauvegarderVols(vols, n);
                ajouterHistorique(ref, idAgence, "Annulation", nb, "succès");
                snprintf(reponse, sizeof(reponse), "Annulation de %d places sur vol %d effectuée", nb, ref);
            }else if (strcmp(type, "FACTURE") == 0) {
		    genererFactures();
   		    snprintf(reponse, sizeof(reponse), "Factures mises à jour dans facture.txt");
		}
	    else {
                snprintf(reponse, sizeof(reponse), "Commande inconnue.");
            }
            return reponse;
        }
    }

    snprintf(reponse, sizeof(reponse), "Vol non trouvé.");
    return reponse;
}
void genererFactures() {
    FILE *f_histo = fopen("histo.txt", "r");
    if (!f_histo) {
        perror("Erreur ouverture histo.txt");
        return;
    }

    FILE *f_facture = fopen("facture.txt", "w");
    if (!f_facture) {
        perror("Erreur ouverture facture.txt");
        fclose(f_histo);
        return;
    }

    // Lire les vols pour récupérer les prix
    Vol vols[100];
    int nbVols = chargerVols(vols, 100);

    int factures[100] = {0};  // factures[ID_agence] = montant

    char ligne[256];
    fgets(ligne, sizeof(ligne), f_histo); // Ignorer en-tête

    int refVol, idAgence, valeur;
    char transaction[20], resultat[20];

    while (fgets(ligne, sizeof(ligne), f_histo)) {
        sscanf(ligne, "%d %d %s %d %s", &refVol, &idAgence, transaction, &valeur, resultat);

        if (strcmp(resultat, "succès") != 0)
            continue;

        // Trouver le prix du vol
        int prix = 0;
        for (int i = 0; i < nbVols; i++) {
            if (vols[i].ref == refVol) {
                prix = vols[i].prix;
                break;
            }
        }

        if (strcmp(transaction, "Demande") == 0) {
            factures[idAgence] += valeur * prix;
        } else if (strcmp(transaction, "Annulation") == 0) {
            factures[idAgence] -= valeur * prix;
            factures[idAgence] += valeur * prix / 10; // pénalité 10%
        }
    }

    fprintf(f_facture, "Référence Agence  Somme à payer\n");
    for (int i = 0; i < 100; i++) {
        if (factures[i] > 0)
            fprintf(f_facture, "%d %d\n", i, factures[i]);
    }

    fclose(f_histo);
    fclose(f_facture);
}


    

// Point d'entrée principal
int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Erreur socket");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Réutilisation rapide du port après arrêt du serveur
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Erreur bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Erreur listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", PORT);
    while (1) {
    socklen_t addrlen = sizeof(address);
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
        perror("Erreur accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    read(new_socket, buffer, BUFFER_SIZE);
    printf("Message reçu : %s\n", buffer);

    char *rep = traiterRequete(buffer, 1);  // idAgence = 1 en dur ici
    send(new_socket, rep, strlen(rep), 0);
    printf("Réponse envoyée.\n");

    close(new_socket);
    }
    close(server_fd);
    return 0;
}

