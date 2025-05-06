#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define VOL_FILE "vols.txt"
#define HISTO_FILE "histo.txt"
#define FACTURE_FILE "facture.txt"
#define END_MARKER "END\n"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void logHisto(int ref, const char *agence, const char *operation, int valeur, const char *resultat) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(HISTO_FILE, "a");
    if (f) {
        fprintf(f, "%d %s %s %d %s\n", ref, agence, operation, valeur, resultat);
        fclose(f);
    }
    pthread_mutex_unlock(&mutex);
}

void updateFacture(const char *agence, int montant) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(FACTURE_FILE, "r");
    FILE *tmp = fopen("temp_facture.txt", "w");
    char line[BUFFER_SIZE];
    int found = 0;

    if (!f) {
        if (!tmp) { pthread_mutex_unlock(&mutex); return; }
        fprintf(tmp, "%s %d\n", agence, montant);
        fclose(tmp);
        rename("temp_facture.txt", FACTURE_FILE);
        pthread_mutex_unlock(&mutex);
        return;
    }

    if (!tmp) { fclose(f); pthread_mutex_unlock(&mutex); return; }

    while (fgets(line, sizeof(line), f)) {
        char ag[50];
        int somme;
        if (sscanf(line, "%s %d", ag, &somme) == 2) {
            if (strcmp(ag, agence) == 0) {
                somme += montant;
                found = 1;
            }
            fprintf(tmp, "%s %d\n", ag, somme);
        }
    }

    if (!found) {
        fprintf(tmp, "%s %d\n", agence, montant);
    }

    fclose(f);
    fclose(tmp);
    remove(FACTURE_FILE);
    rename("temp_facture.txt", FACTURE_FILE);
    pthread_mutex_unlock(&mutex);
}

void sendVols(int sock) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(VOL_FILE, "r");
    if (!f) {
        char *err = "Erreur : Impossible d’ouvrir vols.txt\n";
        write(sock, err, strlen(err));
        write(sock, END_MARKER, strlen(END_MARKER));
        pthread_mutex_unlock(&mutex);
        return;
    }
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), f)) {
        write(sock, line, strlen(line));
    }
    fclose(f);
    pthread_mutex_unlock(&mutex);
    write(sock, END_MARKER, strlen(END_MARKER));
}

void reserverVol(int sock, int ref, int nb_places, const char *agence) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;

    if (!f || !tmp) {
        char *err = "Erreur : Fichier vols.txt introuvable\n";
        write(sock, err, strlen(err));
        write(sock, END_MARKER, strlen(END_MARKER));
        pthread_mutex_unlock(&mutex);
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        int r, places, prix;
        char dest[50];
        if (sscanf(line, "%d %s %d %d", &r, dest, &places, &prix) == 4) {
            if (r == ref) {
                trouvé = 1;
                if (places >= nb_places) {
                    places -= nb_places;
                    fprintf(tmp, "%d %s %d %d\n", r, dest, places, prix);
                    char msg[BUFFER_SIZE];
                    sprintf(msg, "Réservation confirmée : %d places vol %d\n", nb_places, ref);
                    write(sock, msg, strlen(msg));
                    write(sock, END_MARKER, strlen(END_MARKER));
                    logHisto(ref, agence, "RESERVATION", nb_places, "OK");
                    updateFacture(agence, nb_places * prix);
                } else {
                    fprintf(tmp, "%s", line);
                    char msg[BUFFER_SIZE];
                    sprintf(msg, "Erreur : places insuffisantes (%d dispo)\n", places);
                    write(sock, msg, strlen(msg));
                    write(sock, END_MARKER, strlen(END_MARKER));
                    logHisto(ref, agence, "RESERVATION", nb_places, "ECHEC");
                }
            } else {
                fprintf(tmp, "%s", line);
            }
        } else {
            fprintf(tmp, "%s", line);
        }
    }

    fclose(f);
    fclose(tmp);
    if (!trouvé) {
        char *msg = "Référence de vol introuvable\n";
        write(sock, msg, strlen(msg));
        write(sock, END_MARKER, strlen(END_MARKER));
        remove("temp.txt");
        logHisto(ref, agence, "RESERVATION", nb_places, "INCONNU");
    } else {
        remove(VOL_FILE);
        rename("temp.txt", VOL_FILE);
    }
    pthread_mutex_unlock(&mutex);
}

void annulerVol(int sock, int ref, int nb_places, const char *agence) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;

    if (!f || !tmp) {
        char *err = "Erreur : Fichier vols.txt introuvable\n";
        write(sock, err, strlen(err));
        write(sock, END_MARKER, strlen(END_MARKER));
        pthread_mutex_unlock(&mutex);
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        int r, places, prix;
        char dest[50];
        if (sscanf(line, "%d %s %d %d", &r, dest, &places, &prix) == 4) {
            if (r == ref) {
                trouvé = 1;
                places += nb_places;
                fprintf(tmp, "%d %s %d %d\n", r, dest, places, prix);
                int penalite = (int)(nb_places * prix * 0.1);
                updateFacture(agence, penalite);
                char msg[BUFFER_SIZE];
                sprintf(msg, "Annulation de %d places vol %d (pénalité %d€)\n", nb_places, ref, penalite);
                write(sock, msg, strlen(msg));
                write(sock, END_MARKER, strlen(END_MARKER));
                logHisto(ref, agence, "ANNULATION", nb_places, "OK");
            } else {
                fprintf(tmp, "%s", line);
            }
        } else {
            fprintf(tmp, "%s", line);
        }
    }

    fclose(f);
    fclose(tmp);
    if (!trouvé) {
        char *msg = "Référence de vol introuvable\n";
        write(sock, msg, strlen(msg));
        write(sock, END_MARKER, strlen(END_MARKER));
        remove("temp.txt");
        logHisto(ref, agence, "ANNULATION", nb_places, "INCONNU");
    } else {
        remove(VOL_FILE);
        rename("temp.txt", VOL_FILE);
    }
    pthread_mutex_unlock(&mutex);
}

void consulterFacture(int sock, const char *agence) {
    pthread_mutex_lock(&mutex);
    FILE *f = fopen(FACTURE_FILE, "r");
    int found = 0;
    if (f) {
        char line[BUFFER_SIZE];
        while (fgets(line, sizeof(line), f)) {
            char ag[50];
            int montant;
            if (sscanf(line, "%s %d", ag, &montant) == 2 && strcmp(ag, agence) == 0) {
                char msg[BUFFER_SIZE];
                sprintf(msg, "Facture %s : %d€\n", agence, montant);
                write(sock, msg, strlen(msg));
                found = 1;
                break;
            }
        }
        fclose(f);
    }
    if (!found) {
        char *msg = "Aucune facture pour cette agence\n";
        write(sock, msg, strlen(msg));
    }
    write(sock, END_MARKER, strlen(END_MARKER));
    pthread_mutex_unlock(&mutex);
}

void *handle_client(void *arg) {
    int sock = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE);
        if (n <= 0) {
            printf("Client déconnecté\n");
            close(sock);
            return NULL;
        }

        if (strncmp(buffer, "LIST", 4) == 0) {
            sendVols(sock);
        }
        else if (strncmp(buffer, "RESERVER", 8) == 0) {
            int ref, nb; char agence[50];
            if (sscanf(buffer+9, "%d %d %s", &ref, &nb, agence) != 3) {
                char *msg = "Commande RESERVER invalide\n";
                write(sock, msg, strlen(msg));
                write(sock, END_MARKER, strlen(END_MARKER));
            } else {
                reserverVol(sock, ref, nb, agence);
            }
        }
        else if (strncmp(buffer, "ANNULER", 7) == 0) {
            int ref, nb; char agence[50];
            if (sscanf(buffer+8, "%d %d %s", &ref, &nb, agence) != 3) {
                char *msg = "Commande ANNULER invalide\n";
                write(sock, msg, strlen(msg));
                write(sock, END_MARKER, strlen(END_MARKER));
            } else {
                annulerVol(sock, ref, nb, agence);
            }
        }
        else if (strncmp(buffer, "FACTURE", 7) == 0) {
            char agence[50];
            if (sscanf(buffer+8, "%s", agence) != 1) {
                char *msg = "Commande FACTURE invalide\n";
                write(sock, msg, strlen(msg));
                write(sock, END_MARKER, strlen(END_MARKER));
            } else {
                consulterFacture(sock, agence);
            }
        }
        else {
            char *msg = "Commande invalide\n";
            write(sock, msg, strlen(msg));
            write(sock, END_MARKER, strlen(END_MARKER));
        }
    }
    return NULL;
}

int main() {
    int sockfd, newsockfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind"); exit(1);
    }

    listen(sockfd, 10);
    printf("Serveur en attente sur le port %d...\n", PORT);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) { perror("accept"); continue; }

        printf("Client connecté\n");
        int *pclient = malloc(sizeof(int));
        *pclient = newsockfd;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);  // Pas besoin de join
    }

    close(sockfd);
    pthread_mutex_destroy(&mutex);
    return 0;
}
