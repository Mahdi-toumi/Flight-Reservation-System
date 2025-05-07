#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>

// Définitions des constantes
#define PORT 8080 // Port d'écoute du serveur
#define BUFFER_SIZE 1024 // Taille maximale des buffers pour les messages
#define MAX_DATAGRAM_SIZE 512 // Taille maximale des datagrammes UDP
#define VOL_FILE "vols.txt" // Fichier contenant la liste des vols
#define HISTO_FILE "histo.txt" // Fichier pour l'historique des opérations
#define FACTURE_FILE "facture.txt" // Fichier pour les factures des agences

// Enumération pour les protocoles supportés
typedef enum { PROTO_TCP, PROTO_UDP } Protocol;

// Mutex globaux pour protéger l'accès aux fichiers
pthread_mutex_t vols_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex pour le fichier des vols
pthread_mutex_t histo_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex pour le fichier d'historique
pthread_mutex_t facture_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex pour le fichier des factures

// Structure pour l'en-tête des messages UDP
typedef struct {
    uint32_t seq; // Numéro de séquence du message
    char type[5]; // Type de message (ex. LIST, WAIT) + terminateur nul
    uint32_t len; // Longueur de la charge utile
} UdpHeader;

// Affiche un message de débogage avec horodatage
void debug_print(const char *msg, const struct sockaddr_in *cli_addr, int sockfd) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    
    // Si l'adresse du client est fournie, inclure ses informations
    if (cli_addr) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr->sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("[DEBUG] %s: %s (client %s:%d)\n", time_str, msg, client_ip, ntohs(cli_addr->sin_port));
    } else if (sockfd >= 0) {
        // Sinon, inclure le descripteur de socket
        printf("[DEBUG] %s: %s (socket %d)\n", time_str, msg, sockfd);
    } else {
        printf("[DEBUG] %s: %s\n", time_str, msg);
    }
}

// Crée un socket pour le protocole spécifié
int create_socket(Protocol proto) {
    // Créer un socket TCP ou UDP selon le protocole
    int sockfd = socket(AF_INET, proto == PROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Échec de la création du socket");
        return -1;
    }
    // Configurer l'option SO_REUSEADDR pour réutiliser l'adresse
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Échec de la configuration des options du socket");
        close(sockfd);
        return -1;
    }
    debug_print("Socket créé avec succès", NULL, sockfd);
    return sockfd;
}

// Envoie un message d'attente au client si une ressource est en cours d'utilisation
void send_wait_message(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, const char *resource, Protocol proto, uint32_t seq) {
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "ATTENTE : Un autre client accède à %s", resource);
    debug_print("Envoi d'un message d'attente", cli_addr, sock);
    if (proto == PROTO_TCP) {
        // Envoi via TCP
        if (write(sock, msg, strlen(msg)) < 0) {
            perror("Échec de l'envoi du message d'attente");
        }
    } else {
        // Envoi via UDP avec en-tête
        UdpHeader header = { seq, "WAIT", (uint32_t)strlen(msg) };
        char packet[MAX_DATAGRAM_SIZE];
        memcpy(packet, &header, sizeof(UdpHeader));
        memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
        if (sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len) < 0) {
            perror("Échec de l'envoi du message d'attente via UDP");
        }
    }
}

// Enregistre une opération dans l'historique
void logHisto(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, int ref, const char *agence, const char *operation, int valeur, const char *resultat, Protocol proto, uint32_t seq) {
    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Enregistrement dans l'historique : référence=%d, agence=%s, opération=%s, valeur=%d, résultat=%s", ref, agence, operation, valeur, resultat);
    debug_print(debug_msg, cli_addr, sock);
    
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&histo_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "fichier d'historique", proto, seq);
        pthread_mutex_lock(&histo_mutex);
    }
    // Ouvre le fichier d'historique en mode ajout
    FILE *f = fopen(HISTO_FILE, "a");
    if (!f) {
        perror("Échec de l'ouverture du fichier d'historique");
        pthread_mutex_unlock(&histo_mutex);
        return;
    }
    // Écrire l'entrée dans le fichier
    fprintf(f, "%d %s %s %d %s\n", ref, agence, operation, valeur, resultat);
    fclose(f);
    debug_print("Historique enregistré avec succès", cli_addr, sock);
    pthread_mutex_unlock(&histo_mutex);
}

// Met à jour la facture d'une agence
void updateFacture(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, const char *agence, int montant, Protocol proto, uint32_t seq) {
    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Mise à jour de la facture pour l'agence %s, montant=%d", agence, montant);
    debug_print(debug_msg, cli_addr, sock);
    
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&facture_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "fichier de facture", proto, seq);
        pthread_mutex_lock(&facture_mutex);
    }
    // Ouvre les fichiers pour lecture et écriture temporaire
    FILE *f = fopen(FACTURE_FILE, "r");
    FILE *tmp = fopen("temp_facture.txt", "w");
    char line[BUFFER_SIZE];
    int found = 0;

    if (!f || !tmp) {
        if (f) fclose(f);
        if (tmp) fclose(tmp);
        perror("Échec de l'accès aux fichiers de facture");
        pthread_mutex_unlock(&facture_mutex);
        return;
    }

    // Parcourir le fichier des factures
    while (fgets(line, sizeof(line), f)) {
        char ag[50];
        int somme;
        if (sscanf(line, "%s %d", ag, &somme) == 2) {
            if (strcmp(ag, agence) == 0) {
                somme += montant; // Mettre à jour le montant pour l'agence
                found = 1;
            }
            fprintf(tmp, "%s %d\n", ag, somme);
        }
    }
    // Si l'agence n'existe pas, ajouter une nouvelle entrée
    if (!found) {
        fprintf(tmp, "%s %d\n", agence, montant);
    }

    fclose(f);
    fclose(tmp);
    // Remplacer le fichier original par le fichier temporaire
    if (remove(FACTURE_FILE) != 0 || rename("temp_facture.txt", FACTURE_FILE) != 0) {
        perror("Échec de la mise à jour du fichier de facture");
    }
    debug_print("Facture mise à jour avec succès", cli_addr, sock);
    pthread_mutex_unlock(&facture_mutex);
}

// Envoie la liste des vols au client
void sendVols(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, Protocol proto, uint32_t seq) {
    debug_print("Envoi de la liste des vols", cli_addr, sock);
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&vols_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "liste des vols", proto, seq);
        pthread_mutex_lock(&vols_mutex);
    }
    // Ouvre le fichier des vols
    FILE *f = fopen(VOL_FILE, "r");
    if (!f) {
        char err[] = "Erreur : Impossible d'ouvrir le fichier des vols\n";
        debug_print("Échec de l'ouverture du fichier des vols", cli_addr, sock);
        if (proto == PROTO_TCP) {
            write(sock, err, strlen(err));
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
        }
        pthread_mutex_unlock(&vols_mutex);
        return;
    }
    // Lire et envoyer chaque ligne du fichier
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), f)) {
        if (proto == PROTO_TCP) {
            if (write(sock, line, strlen(line)) < 0) {
                perror("Échec de l'envoi d'une ligne de vol");
                fclose(f);
                pthread_mutex_unlock(&vols_mutex);
                return;
            }
        } else {
            UdpHeader header = { seq, "LIST", (uint32_t)strlen(line) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), line, strlen(line));
            if (sendto(sock, packet, sizeof(UdpHeader) + strlen(line), 0, (struct sockaddr *)cli_addr, cli_len) < 0) {
                perror("Échec de l'envoi d'une ligne de vol via UDP");
                fclose(f);
                pthread_mutex_unlock(&vols_mutex);
                return;
            }
        }
    }
    // Envoyer un marqueur de fin
    char end[] = "END\n";
    if (proto == PROTO_TCP) {
        if (write(sock, end, strlen(end)) != strlen(end)) {
            perror("Échec de l'envoi du marqueur END");
        }
    } else {
        UdpHeader header = { seq, "END", (uint32_t)strlen(end) };
        char packet[MAX_DATAGRAM_SIZE];
        memcpy(packet, &header, sizeof(UdpHeader));
        memcpy(packet + sizeof(UdpHeader), end, strlen(end));
        if (sendto(sock, packet, sizeof(UdpHeader) + strlen(end), 0, (struct sockaddr *)cli_addr, cli_len) < 0) {
            perror("Échec de l'envoi du marqueur END via UDP");
        }
    }
    debug_print("Liste des vols envoyée avec succès", cli_addr, sock);
    fclose(f);
    pthread_mutex_unlock(&vols_mutex);
}

// Traite une réservation de vol
void reserverVol(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, int ref, int nb_places, const char *agence, Protocol proto, uint32_t seq) {
    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Traitement de la réservation : référence=%d, places=%d, agence=%s", ref, nb_places, agence);
    debug_print(debug_msg, cli_addr, sock);
    
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&vols_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "liste des vols", proto, seq);
        pthread_mutex_lock(&vols_mutex);
    }
    // Ouvre les fichiers pour lecture et écriture temporaire
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;

    if (!f || !tmp) {
        char err[] = "Erreur : Impossible d'accéder au fichier des vols\n";
        debug_print("Échec de l'accès au fichier des vols", cli_addr, sock);
        if (proto == PROTO_TCP) {
            write(sock, err, strlen(err));
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
        }
        if (f) fclose(f);
        if (tmp) fclose(tmp);
        pthread_mutex_unlock(&vols_mutex);
        return;
    }

    // Parcourir le fichier des vols
    while (fgets(line, sizeof(line), f)) {
        int r, places, prix;
        char dest[50];
        if (sscanf(line, "%d %s %d %d", &r, dest, &places, &prix) == 4) {
            if (r == ref) {
                trouvé = 1;
                if (places >= nb_places) {
                    // Réserver les places et mettre à jour le fichier
                    places -= nb_places;
                    fprintf(tmp, "%d %s %d %d\n", r, dest, places, prix);
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Réservation confirmée : %d places sur le vol %d\n", nb_places, ref);
                    if (proto == PROTO_TCP) {
                        if (write(sock, msg, strlen(msg)) < 0) {
                            perror("Échec de l'envoi de la confirmation");
                        }
                    } else {
                        UdpHeader header = { seq, "RSRV", (uint32_t)strlen(msg) };
                        char packet[MAX_DATAGRAM_SIZE];
                        memcpy(packet, &header, sizeof(UdpHeader));
                        memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
                        sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
                    }
                    logHisto(sock, cli_addr, cli_len, ref, agence, "RESERVATION", nb_places, "OK", proto, seq);
                    updateFacture(sock, cli_addr, cli_len, agence, nb_places * prix, proto, seq);
                } else {
                    // Pas assez de places disponibles
                    fprintf(tmp, "%s", line);
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Erreur : Seulement %d places disponibles\n", places);
                    if (proto == PROTO_TCP) {
                        if (write(sock, msg, strlen(msg)) < 0) {
                            perror("Échec de l'envoi du message d'erreur");
                        }
                    } else {
                        UdpHeader header = { seq, "ERR", (uint32_t)strlen(msg) };
                        char packet[MAX_DATAGRAM_SIZE];
                        memcpy(packet, &header, sizeof(UdpHeader));
                        memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
                        sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
                    }
                    logHisto(sock, cli_addr, cli_len, ref, agence, "RESERVATION", nb_places, "FAILED", proto, seq);
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
        // Vol non trouvé
        char msg[] = "Erreur : Référence de vol introuvable\n";
        debug_print("Référence de vol introuvable", cli_addr, sock);
        if (proto == PROTO_TCP) {
            if (write(sock, msg, strlen(msg)) < 0) {
                perror("Échec de l'envoi du message d'erreur");
            }
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(msg) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
        }
        remove("temp.txt");
        logHisto(sock, cli_addr, cli_len, ref, agence, "RESERVATION", nb_places, "UNKNOWN", proto, seq);
    } else {
        // Remplacer le fichier original par le fichier temporaire
        if (remove(VOL_FILE) != 0 || rename("temp.txt", VOL_FILE) != 0) {
            perror("Échec de la mise à jour du fichier des vols");
        }
        debug_print("Fichier des vols mis à jour avec succès", cli_addr, sock);
    }
    pthread_mutex_unlock(&vols_mutex);
}

// Traite une annulation de vol
void annulerVol(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, int ref, int nb_places, const char *agence, Protocol proto, uint32_t seq) {
    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Traitement de l'annulation : référence=%d, places=%d, agence=%s", ref, nb_places, agence);
    debug_print(debug_msg, cli_addr, sock);
    
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&vols_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "liste des vols", proto, seq);
        pthread_mutex_lock(&vols_mutex);
    }
    // Ouvre les fichiers pour lecture et écriture temporaire
    FILE *f = fopen(VOL_FILE, "r");
    FILE *tmp = fopen("temp.txt", "w");
    char line[BUFFER_SIZE];
    int trouvé = 0;
    int prix_vol = 0;

    if (!f || !tmp) {
        char err[] = "Erreur : Impossible d'accéder au fichier des vols\n";
        debug_print("Échec de l'accès au fichier des vols", cli_addr, sock);
        if (proto == PROTO_TCP) {
            write(sock, err, strlen(err));
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
        }
        if (f) fclose(f);
        if (tmp) fclose(tmp);
        pthread_mutex_unlock(&vols_mutex);
        return;
    }

    // Parcourir le fichier des vols
    while (fgets(line, sizeof(line), f)) {
        int r, places, prix;
        char dest[50];
        if (sscanf(line, "%d %s %d %d", &r, dest, &places, &prix) == 4) {
            if (r == ref) {
                trouvé = 1;
                if (places >= 0) {
                    // Annuler les places et appliquer une pénalité
                    places += nb_places;
                    prix_vol = prix;
                    fprintf(tmp, "%d %s %d %d\n", r, dest, places, prix);
                    int montant_reserve = nb_places * prix;
                    int penalite = (int)(montant_reserve * 0.1);
                    updateFacture(sock, cli_addr, cli_len, agence, -montant_reserve + penalite, proto, seq);
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Annulation confirmée : %d places sur le vol %d (pénalité : %d DT)\n", nb_places, ref, penalite);
                    if (proto == PROTO_TCP) {
                        if (write(sock, msg, strlen(msg)) < 0) {
                            perror("Échec de l'envoi de la confirmation d'annulation");
                        }
                    } else {
                        UdpHeader header = { seq, "ANUL", (uint32_t)strlen(msg) };
                        char packet[MAX_DATAGRAM_SIZE];
                        memcpy(packet, &header, sizeof(UdpHeader));
                        memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
                        sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
                    }
                    logHisto(sock, cli_addr, cli_len, ref, agence, "CANCELLATION", nb_places, "OK", proto, seq);
                } else {
                    // Nombre de places invalide
                    fprintf(tmp, "%s", line);
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "Erreur : Nombre de places invalide pour l'annulation\n");
                    if (proto == PROTO_TCP) {
                        if (write(sock, msg, strlen(msg)) < 0) {
                            perror("Échec de l'envoi du message d'erreur");
                        }
                    } else {
                        UdpHeader header = { seq, "ERR", (uint32_t)strlen(msg) };
                        char packet[MAX_DATAGRAM_SIZE];
                        memcpy(packet, &header, sizeof(UdpHeader));
                        memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
                        sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
                    }
                    logHisto(sock, cli_addr, cli_len, ref, agence, "CANCELLATION", nb_places, "FAILED", proto, seq);
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
        // Vol non trouvé
        char msg[] = "Erreur : Référence de vol introuvable\n";
        debug_print("Référence de vol introuvable", cli_addr, sock);
        if (proto == PROTO_TCP) {
            if (write(sock, msg, strlen(msg)) < 0) {
                perror("Échec de l'envoi du message d'erreur");
            }
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(msg) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
        }
        remove("temp.txt");
        logHisto(sock, cli_addr, cli_len, ref, agence, "CANCELLATION", nb_places, "UNKNOWN", proto, seq);
    } else {
        // Remplacer le fichier original par le fichier temporaire
        if (remove(VOL_FILE) != 0 || rename("temp.txt", VOL_FILE) != 0) {
            perror("Échec de la mise à jour du fichier des vols");
        }
        debug_print("Fichier des vols mis à jour avec succès", cli_addr, sock);
    }
    pthread_mutex_unlock(&vols_mutex);
}

// Consulte la facture d'une agence
void consulterFacture(int sock, struct sockaddr_in *cli_addr, socklen_t cli_len, const char *agence, Protocol proto, uint32_t seq) {
    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Récupération de la facture pour l'agence %s", agence);
    debug_print(debug_msg, cli_addr, sock);
    
    // Verrouiller le mutex pour éviter les accès concurrents
    if (pthread_mutex_trylock(&facture_mutex) != 0) {
        send_wait_message(sock, cli_addr, cli_len, "fichier de facture", proto, seq);
        pthread_mutex_lock(&facture_mutex);
    }
    // Ouvre le fichier des factures
    FILE *f = fopen(FACTURE_FILE, "r");
    int found = 0;
    if (f) {
        char line[BUFFER_SIZE];
        while (fgets(line, sizeof(line), f)) {
            char ag[50];
            int montant;
            if (sscanf(line, "%s %d", ag, &montant) == 2 && strcmp(ag, agence) == 0) {
                // Facture trouvée, envoyer le résultat
                char msg[BUFFER_SIZE];
                snprintf(msg, sizeof(msg), "Succès : Facture pour l'agence %s : %d DT\n", agence, montant);
                if (proto == PROTO_TCP) {
                    if (write(sock, msg, strlen(msg)) < 0) {
                        perror("Échec de l'envoi de la facture");
                    }
                } else {
                    UdpHeader header = { seq, "FACT", (uint32_t)strlen(msg) };
                    char packet[MAX_DATAGRAM_SIZE];
                    memcpy(packet, &header, sizeof(UdpHeader));
                    memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
                    sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
                }
                found = 1;
                break;
            }
        }
        fclose(f);
    }
    if (!found) {
        // Aucune facture trouvée
        char msg[] = "Impossible : Aucune facture trouvée pour cette agence\n";
        debug_print("Aucune facture trouvée", cli_addr, sock);
        if (proto == PROTO_TCP) {
            if (write(sock, msg, strlen(msg)) < 0) {
                perror("Échec de l'envoi du message d'absence de facture");
            }
        } else {
            UdpHeader header = { seq, "ERR", (uint32_t)strlen(msg) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), msg, strlen(msg));
            sendto(sock, packet, sizeof(UdpHeader) + strlen(msg), 0, (struct sockaddr *)cli_addr, cli_len);
        }
    }
    debug_print("Requête de facture traitée", cli_addr, sock);
    pthread_mutex_unlock(&facture_mutex);
}

// Gère un client TCP dans un thread séparé
void *handle_tcp_client(void *arg) {
    int newsockfd = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    
    debug_print("Nouveau thread client TCP démarré", NULL, newsockfd);

    while (1) {
        // Lire la commande du client
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t n = read(newsockfd, buffer, BUFFER_SIZE - 1);
        if (n < 0) {
            perror("Erreur lors de la lecture du client");
            break;
        }
        if (n == 0) {
            debug_print("Client déconnecté", NULL, newsockfd);
            break;
        }
        buffer[n] = '\0';
        char debug_msg[BUFFER_SIZE];
        snprintf(debug_msg, sizeof(debug_msg), "Commande reçue : %s", buffer);
        debug_print(debug_msg, NULL, newsockfd);

        // Traiter la commande
        if (strncmp(buffer, "LIST", 4) == 0) {
            sendVols(newsockfd, NULL, 0, PROTO_TCP, 0);
        } else if (strncmp(buffer, "RESERVER", 8) == 0) {
            int ref, nb;
            char agence[50];
            if (sscanf(buffer + 9, "%d %d %s", &ref, &nb, agence) == 3) {
                reserverVol(newsockfd, NULL, 0, ref, nb, agence, PROTO_TCP, 0);
            } else {
                char err[] = "Commande RESERVER invalide\n";
                write(newsockfd, err, strlen(err));
                debug_print("Commande RESERVER invalide", NULL, newsockfd);
            }
        } else if (strncmp(buffer, "ANNULER", 7) == 0) {
            int ref, nb;
            char agence[50];
            if (sscanf(buffer + 8, "%d %d %s", &ref, &nb, agence) == 3) {
                annulerVol(newsockfd, NULL, 0, ref, nb, agence, PROTO_TCP, 0);
            } else {
                char err[] = "Commande ANNULER invalide\n";
                write(newsockfd, err, strlen(err));
                debug_print("Commande ANNULER invalide", NULL, newsockfd);
            }
        } else if (strncmp(buffer, "FACTURE", 7) == 0) {
            char ag[50];
            if (sscanf(buffer + 8, "%s", ag) == 1) {
                consulterFacture(newsockfd, NULL, 0, ag, PROTO_TCP, 0);
            } else {
                char err[] = "Commande FACTURE invalide\n";
                write(newsockfd, err, strlen(err));
                debug_print("Commande FACTURE invalide", NULL, newsockfd);
            }
        } else {
            char err[] = "Commande inconnue\n";
            write(newsockfd, err, strlen(err));
            debug_print("Commande inconnue reçue", NULL, newsockfd);
        }
    }

    close(newsockfd);
    debug_print("Thread client TCP terminé", NULL, newsockfd);
    return NULL;
}

// Traite une requête UDP
void handle_udp_request(int sockfd, char *buffer, ssize_t n, struct sockaddr_in *cli_addr, socklen_t cli_len) {
    // Vérifier si le datagramme est trop court
    if (n < sizeof(UdpHeader)) {
        char err[] = "Datagramme trop court\n";
        UdpHeader header = { 0, "ERR", (uint32_t)strlen(err) };
        char packet[MAX_DATAGRAM_SIZE];
        memcpy(packet, &header, sizeof(UdpHeader));
        memcpy(packet + sizeof(UdpHeader), err, strlen(err));
        sendto(sockfd, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
        debug_print("Datagramme invalide reçu : trop court", cli_addr, sockfd);
        return;
    }

    // Extraire l'en-tête et la charge utile
    UdpHeader header;
    memcpy(&header, buffer, sizeof(UdpHeader));
    char *payload = buffer + sizeof(UdpHeader);
    payload[header.len] = '\0';

    char debug_msg[BUFFER_SIZE];
    snprintf(debug_msg, sizeof(debug_msg), "Commande UDP reçue : %s", payload);
    debug_print(debug_msg, cli_addr, sockfd);

    // Traiter la commande
    if (strncmp(payload, "LIST", 4) == 0) {
        sendVols(sockfd, cli_addr, cli_len, PROTO_UDP, header.seq);
    } else if (strncmp(payload, "RESERVER", 8) == 0) {
        int ref, nb;
        char agence[50];
        if (sscanf(payload + 9, "%d %d %s", &ref, &nb, agence) == 3) {
            reserverVol(sockfd, cli_addr, cli_len, ref, nb, agence, PROTO_UDP, header.seq);
        } else {
            char err[] = "Commande RESERVER invalide\n";
            UdpHeader header_out = { header.seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header_out, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sockfd, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
            debug_print("Commande RESERVER invalide", cli_addr, sockfd);
        }
    } else if (strncmp(payload, "ANNULER", 7) == 0) {
        int ref, nb;
        char agence[50];
        if (sscanf(payload + 8, "%d %d %s", &ref, &nb, agence) == 3) {
            annulerVol(sockfd, cli_addr, cli_len, ref, nb, agence, PROTO_UDP, header.seq);
        } else {
            char err[] = "Commande ANNULER invalide\n";
            UdpHeader header_out = { header.seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header_out, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sockfd, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
            debug_print("Commande ANNULER invalide", cli_addr, sockfd);
        }
    } else if (strncmp(payload, "FACTURE", 7) == 0) {
        char ag[50];
        if (sscanf(payload + 8, "%s", ag) == 1) {
            consulterFacture(sockfd, cli_addr, cli_len, ag, PROTO_UDP, header.seq);
        } else {
            char err[] = "Commande FACTURE invalide\n";
            UdpHeader header_out = { header.seq, "ERR", (uint32_t)strlen(err) };
            char packet[MAX_DATAGRAM_SIZE];
            memcpy(packet, &header_out, sizeof(UdpHeader));
            memcpy(packet + sizeof(UdpHeader), err, strlen(err));
            sendto(sockfd, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
            debug_print("Commande FACTURE invalide", cli_addr, sockfd);
        }
    } else {
        char err[] = "Commande inconnue\n";
        UdpHeader header_out = { header.seq, "ERR", (uint32_t)strlen(err) };
        char packet[MAX_DATAGRAM_SIZE];
        memcpy(packet, &header_out, sizeof(UdpHeader));
        memcpy(packet + sizeof(UdpHeader), err, strlen(err));
        sendto(sockfd, packet, sizeof(UdpHeader) + strlen(err), 0, (struct sockaddr *)cli_addr, cli_len);
        debug_print("Commande inconnue reçue", cli_addr, sockfd);
    }
}

// Programme principal
int main(int argc, char *argv[]) {
    // Vérifier les arguments
    if (argc != 2 || (strcmp(argv[1], "tcp") != 0 && strcmp(argv[1], "udp") != 0)) {
        fprintf(stderr, "Usage: %s <tcp|udp>\n", argv[0]);
        return 1;
    }
    Protocol proto = strcmp(argv[1], "tcp") == 0 ? PROTO_TCP : PROTO_UDP;

    int sockfd = -1;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen = sizeof(cli_addr);

    // Créer le socket
    sockfd = create_socket(proto);
    if (sockfd < 0) {
        return 1;
    }

    // Configurer l'adresse du serveur
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    // Lier le socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Échec de la liaison du socket");
        close(sockfd);
        return 1;
    }
    debug_print("Socket lié avec succès", NULL, sockfd);

    if (proto == PROTO_TCP) {
        // Écouter les connexions TCP
        if (listen(sockfd, 5) < 0) {
            perror("Échec de l'écoute sur le socket");
            close(sockfd);
            return 1;
        }
        printf("Démarrage du serveur TCP sur le port %d...\n", PORT);
        debug_print("Serveur TCP démarré", NULL, sockfd);

        while (1) {
            // Accepter une nouvelle connexion
            int *newsockfd = malloc(sizeof(int));
            if (!newsockfd) {
                perror("Échec de l'allocation de mémoire pour le socket client");
                continue;
            }
            *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
            if (*newsockfd < 0) {
                perror("Échec de l'acceptation de la connexion client");
                free(newsockfd);
                continue;
            }
            char debug_msg[BUFFER_SIZE];
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            snprintf(debug_msg, sizeof(debug_msg), "Nouveau client connecté : %s:%d", client_ip, ntohs(cli_addr.sin_port));
            debug_print(debug_msg, NULL, *newsockfd);

            // Créer un thread pour gérer le client
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_tcp_client, newsockfd) != 0) {
                perror("Échec de la création du thread client");
                close(*newsockfd);
                free(newsockfd);
                continue;
            }

            // Détacher le thread pour éviter les fuites de mémoire
            if (pthread_detach(thread) != 0) {
                perror("Échec du détachement du thread client");
            }
        }
    } else {
        // Gérer les requêtes UDP
        printf("Démarrage du serveur UDP sur le port %d...\n", PORT);
        debug_print("Serveur UDP démarré", NULL, sockfd);
        char buffer[MAX_DATAGRAM_SIZE];

        while (1) {
            // Recevoir un datagramme
            memset(buffer, 0, MAX_DATAGRAM_SIZE);
            ssize_t n = recvfrom(sockfd, buffer, MAX_DATAGRAM_SIZE, 0, (struct sockaddr *)&cli_addr, &clilen);
            if (n < 0) {
                perror("Échec de la réception du paquet UDP");
                continue;
            }
            handle_udp_request(sockfd, buffer, n, &cli_addr, clilen);
        }
    }

    // Fermer le socket et libérer les mutex
    close(sockfd);
    debug_print("Socket serveur fermé", NULL, sockfd);
    pthread_mutex_destroy(&vols_mutex);
    pthread_mutex_destroy(&histo_mutex);
    pthread_mutex_destroy(&facture_mutex);
    return 0;
}
