#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>

#define PORT 8080
#define MAX_CONNEXIONS 10
#define BUFFER_SIZE 1024
#define MAX_VOLS 100

typedef struct {
    int ref;
    char villeDepart[50];
    char villeArrivee[50];
    int nbPlaces;
    int nbPlacesDispo;
    float prix;
} Vol;

Vol vols[MAX_VOLS];
int nbVols = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void lireVolsDepuisFichier() {
    FILE *fichier = fopen("vols.txt", "r");
    if (!fichier) {
        perror("Erreur ouverture vols.txt");
        exit(EXIT_FAILURE);
    }

    while (fscanf(fichier, "%d %s %s %d %f", 
                  &vols[nbVols].ref, 
                  vols[nbVols].villeDepart, 
                  vols[nbVols].villeArrivee, 
                  &vols[nbVols].nbPlaces, 
                  &vols[nbVols].prix) == 5) {
        vols[nbVols].nbPlacesDispo = vols[nbVols].nbPlaces;
        nbVols++;
    }

    fclose(fichier);
}

void send_message(int sock, const char *message) {
    int len = strlen(message);
    if (write(sock, &len, sizeof(int)) != sizeof(int)) {
        perror("Erreur envoi taille");
        return;
    }
    if (write(sock, message, len) != len) {
        perror("Erreur envoi message");
    }
}

int read_message(int sock, char *buffer, int bufsize) {
    int len;
    int r = read(sock, &len, sizeof(int));
    if (r <= 0 || len <= 0 || len >= bufsize) return -1;

    int total = 0;
    while (total < len) {
        int n = read(sock + total, buffer + total, len - total);
        if (n <= 0) return -1;
        total += n;
    }

    buffer[total] = '\0';
    return total;
}

void *gerer_client(void *arg) {
    int sock = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];

    while (1) {
        int n = read(sock, buffer, BUFFER_SIZE - 1);
        if (n <= 0) break;

        buffer[n] = '\0';
        char *commande = strtok(buffer, " \n");

        if (!commande) continue;

        if (strcmp(commande, "LIST") == 0) {
            pthread_mutex_lock(&mutex);
            char message[BUFFER_SIZE] = "";
            for (int i = 0; i < nbVols; i++) {
                char ligne[200];
                snprintf(ligne, sizeof(ligne), "Ref: %d | %s -> %s | Places: %d dispo / %d | Prix: %.2f€\n",
                         vols[i].ref, vols[i].villeDepart, vols[i].villeArrivee,
                         vols[i].nbPlacesDispo, vols[i].nbPlaces, vols[i].prix);
                strncat(message, ligne, sizeof(message) - strlen(message) - 1);
            }
            pthread_mutex_unlock(&mutex);
            send_message(sock, message);
        }

        else if (strcmp(commande, "RESERVER") == 0) {
            int ref, nb;
            char agence[50];
            sscanf(strtok(NULL, " "), "%d", &ref);
            sscanf(strtok(NULL, " "), "%d", &nb);
            sscanf(strtok(NULL, " "), "%s", agence);

            pthread_mutex_lock(&mutex);
            int trouve = 0;
            char message[BUFFER_SIZE];
            for (int i = 0; i < nbVols; i++) {
                if (vols[i].ref == ref) {
                    trouve = 1;
                    if (vols[i].nbPlacesDispo >= nb) {
                        vols[i].nbPlacesDispo -= nb;
                        snprintf(message, sizeof(message), 
                                 "Réservation réussie pour %d place(s) sur vol %d par %s\n", nb, ref, agence);
                    } else {
                        snprintf(message, sizeof(message), 
                                 "Pas assez de places disponibles pour vol %d.\n", ref);
                    }
                    break;
                }
            }
            if (!trouve) snprintf(message, sizeof(message), "Vol %d non trouvé.\n", ref);
            pthread_mutex_unlock(&mutex);

            send_message(sock, message);
        }

        else if (strcmp(commande, "ANNULER") == 0) {
            int ref, nb;
            char agence[50];
            sscanf(strtok(NULL, " "), "%d", &ref);
            sscanf(strtok(NULL, " "), "%d", &nb);
            sscanf(strtok(NULL, " "), "%s", agence);

            pthread_mutex_lock(&mutex);
            int trouve = 0;
            char message[BUFFER_SIZE];
            for (int i = 0; i < nbVols; i++) {
                if (vols[i].ref == ref) {
                    trouve = 1;
                    vols[i].nbPlacesDispo += nb;
                    if (vols[i].nbPlacesDispo > vols[i].nbPlaces) {
                        vols[i].nbPlacesDispo = vols[i].nbPlaces;
                    }
                    snprintf(message, sizeof(message), 
                             "Annulation de %d place(s) sur vol %d par %s réussie.\n", nb, ref, agence);
                    break;
                }
            }
            if (!trouve) snprintf(message, sizeof(message), "Vol %d non trouvé.\n", ref);
            pthread_mutex_unlock(&mutex);

            send_message(sock, message);
        }

        else if (strcmp(commande, "FACTURE") == 0) {
            char agence[50];
            sscanf(strtok(NULL, " "), "%s", agence);

            pthread_mutex_lock(&mutex);
            float total = 0;
            for (int i = 0; i < nbVols; i++) {
                int placesReservees = vols[i].nbPlaces - vols[i].nbPlacesDispo;
                total += placesReservees * vols[i].prix;
            }
            pthread_mutex_unlock(&mutex);

            char message[BUFFER_SIZE];
            snprintf(message, sizeof(message), 
                     "Facture pour %s : Total = %.2f€\n", agence, total);
            send_message(sock, message);
        }

        else {
            send_message(sock, "Commande inconnue.\n");
        }
    }

    close(sock);
    return NULL;
}

int main() {
    int serveur_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);

    lireVolsDepuisFichier();

    if ((serveur_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(serveur_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(serveur_fd, MAX_CONNEXIONS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", PORT);

    while (1) {
        client_fd = accept(serveur_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        int *pclient = malloc(sizeof(int));
        *pclient = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, gerer_client, pclient);
        pthread_detach(tid);
    }

    close(serveur_fd);
    return 0;
}
