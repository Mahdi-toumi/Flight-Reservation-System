#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define VOL_FILE "vols.txt"
#define HISTO_FILE "histo.txt"
#define FACTURE_FILE "facture.txt"

void logHisto(int ref, const char *agence, const char *operation, int valeur, const char *resultat) {
    FILE *f = fopen(HISTO_FILE, "a");
    if (!f) {
        perror("Erreur ouverture histo.txt");
        return;
    }
    fprintf(f, "%d %s %s %d %s\n", ref, agence, operation, valeur, resultat);
    fclose(f);
}

void updateFacture(const char *agence, int montant) {
    FILE *f = fopen(FACTURE_FILE, "r");
    FILE *tmp = fopen("temp_facture.txt", "w");
    char line[BUFFER_SIZE];
    int found = 0;

    if (!f || !tmp) {
        if (f) fclose(f);
        if (tmp) fclose(tmp);
        perror("Erreur manipulation facture");
        return;
    }

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
    if (remove(FACTURE_FILE) != 0 || rename("temp_facture.txt", FACTURE_FILE) != 0) {
        perror("Erreur mise à jour facture");
    }
}

void sendVols(int sock) {
    FILE *f = fopen(VOL_FILE, "r");
    if (!f) {
        char err[] = "Erreur : Impossible d’ouvrir vols.txt\n";
        write(sock, err, strlen(err));
        return;
    }
    char line[BUFFER_SIZE];
    printf("Envoi de la liste des vols\n"); // Debug output
    while (fgets(line, sizeof(line), f)) {
        if (write(sock, line, strlen(line)) < 0) {
            perror("Erreur envoi ligne vol");
            fclose(f);
            return;
        }
    }
    if (write(sock, "END\n", 4) != 4) {
        perror("Erreur envoi marqueur END");
    } else {
        printf("Marqueur END envoyé\n"); // Debug output
    }
    fclose(f);
}

void reserverVol(int sock, int ref, int nb_places, const char *agence) {
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;

    if (!f || !tmp) {
        char err[] = "Erreur : Fichier vols.txt introuvable\n";
        write(sock, err, strlen(err));
        if (f) fclose(f);
        if (tmp) fclose(tmp);
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
                    snprintf(msg, sizeof(msg), "Réservation confirmée : %d places vol %d\n", nb_places, ref);
                    if (write(sock, msg, strlen(msg)) < 0) {
                        perror("Erreur envoi confirmation");
                    }
                    logHisto(ref, agence, "RESERVATION", nb_places, "OK");
                    updateFacture(agence, nb_places * prix);
                } else {
                    fprintf(tmp, "%s", line);
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Erreur : places insuffisantes (%d dispo)\n", places);
                    if (write(sock, msg, strlen(msg)) < 0) {
                        perror("Erreur envoi erreur places");
                    }
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
        char msg[] = "Référence de vol introuvable\n";
        if (write(sock, msg, strlen(msg)) < 0) {
            perror("Erreur envoi erreur ref");
        }
        remove("temp.txt");
        logHisto(ref, agence, "RESERVATION", nb_places, "INCONNU");
    } else {
        if (remove(VOL_FILE) != 0 || rename("temp.txt", VOL_FILE) != 0) {
            perror("Erreur mise à jour vols");
        }
    }
}

void annulerVol(int sock, int ref, int nb_places, const char *agence) {
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;

    if (!f || !tmp) {
        char err[] = "Erreur : Fichier vols.txt introuvable\n";
        write(sock, err, strlen(err));
        if (f) fclose(f);
        if (tmp) fclose(tmp);
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
                snprintf(msg, sizeof(msg), "Annulation de %d places vol %d (pénalité %d€)\n", nb_places, ref, penalite);
                if (write(sock, msg, strlen(msg)) < 0) {
                    perror("Erreur envoi confirmation annulation");
                }
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
        char msg[] = "Référence de vol introuvable\n";
        if (write(sock, msg, strlen(msg)) < 0) {
            perror("Erreur envoi erreur ref");
        }
        remove("temp.txt");
        logHisto(ref, agence, "ANNULATION", nb_places, "INCONNU");
    } else {
        if (remove(VOL_FILE) != 0 || rename("temp.txt", VOL_FILE) != 0) {
            perror("Erreur mise à jour vols");
        }
    }
}

void consulterFacture(int sock, const char *agence) {
    FILE *f = fopen(FACTURE_FILE, "r");
    int found = 0;
    if (f) {
        char line[BUFFER_SIZE];
        while (fgets(line, sizeof(line), f)) {
            char ag[50];
            int montant;
            if (sscanf(line, "%s %d", ag, &montant) == 2 && strcmp(ag, agence) == 0) {
                char msg[BUFFER_SIZE];
                snprintf(msg, sizeof(msg), "Facture %s : %d€\n", agence, montant);
                if (write(sock, msg, strlen(msg)) < 0) {
                    perror("Erreur envoi facture");
                }
                found = 1;
                break;
            }
        }
        fclose(f);
    }
    if (!found) {
        char msg[] = "Aucune facture pour cette agence\n";
        if (write(sock, msg, strlen(msg)) < 0) {
            perror("Erreur envoi aucune facture");
        }
    }
}

int main() {
    int sockfd = -1, newsockfd = -1;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    char buffer[BUFFER_SIZE];

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return 1;
    }

    // Configure server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    // Listen for connections
    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        return 1;
    }
    printf("Serveur en attente sur port %d...\n", PORT);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("accept");
            continue;
        }
        printf("Client connecté\n");

        while (1) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t n = read(newsockfd, buffer, BUFFER_SIZE - 1);
            if (n < 0) {
                perror("Erreur lecture client");
                break;
            }
            if (n == 0) {
                printf("Client déconnecté\n");
                break;
            }
            buffer[n] = '\0';
            printf("Commande reçue : %s\n", buffer); // Debug output

            if (strncmp(buffer, "LIST", 4) == 0) {
                sendVols(newsockfd);
            } else if (strncmp(buffer, "RESERVER", 8) == 0) {
                int ref, nb;
                char agence[50];
                if (sscanf(buffer + 9, "%d %d %s", &ref, &nb, agence) == 3) {
                    reserverVol(newsockfd, ref, nb, agence);
                } else {
                    char err[] = "Commande RESERVER invalide\n";
                    write(newsockfd, err, strlen(err));
                }
            } else if (strncmp(buffer, "ANNULER", 7) == 0) {
                int ref, nb;
                char agence[50];
                if (sscanf(buffer + 8, "%d %d %s", &ref, &nb, agence) == 3) {
                    annulerVol(newsockfd, ref, nb, agence);
                } else {
                    char err[] = "Commande ANNULER invalide\n";
                    write(newsockfd, err, strlen(err));
                }
            } else if (strncmp(buffer, "FACTURE", 7) == 0) {
                char ag[50];
                if (sscanf(buffer + 8, "%s", ag) == 1) {
                    consulterFacture(newsockfd, ag);
                } else {
                    char err[] = "Commande FACTURE invalide\n";
                    write(newsockfd, err, strlen(err));
                }
            } else {
                char err[] = "Commande inconnue\n";
                write(newsockfd, err, strlen(err));
            }
        }

        close(newsockfd);
        printf("Client déconnecté\n"); // Debug output
    }

    close(sockfd);
    return 0;
}
