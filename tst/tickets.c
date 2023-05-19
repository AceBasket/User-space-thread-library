#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "thread.h"

#define BUFFER_SIZE 5
#define NUM_ITEMS 10

int buffer[BUFFER_SIZE];
int count = 0; // Nombre d'éléments dans le tampon
int in = 0;    // Indice d'insertion dans le tampon
int out = 0;   // Indice de retrait dans le tampon

thread_mutex_t mutex;

// Fonction exécutée par le producteur
void *producer(void *arg)
{
    int i, item;
    for (i = 0; i < NUM_ITEMS; i++)
    {
        item = rand() % 100; // Générer un nombre aléatoire entre 0 et 99 inclus
        thread_mutex_lock(&mutex);
        while (count == BUFFER_SIZE)
        { // Attente active si le tampon est plein
            thread_mutex_unlock(&mutex);
            thread_yield(); // Relâcher le CPU pour permettre au consommateur de s'exécuter
            thread_mutex_lock(&mutex);
        }
        buffer[in] = item;
        in = (in + 1) % BUFFER_SIZE;
        count++;
        printf("Producteur a produit l'élément %d\n", item);
        thread_mutex_unlock(&mutex);
    }
    thread_exit(NULL);
    return NULL;
}

// Fonction exécutée par le consommateur
void *consumer(void *arg)
{
    int i, item;
    for (i = 0; i < NUM_ITEMS; i++)
    {
        thread_mutex_lock(&mutex);
        while (count == 0)
        { // Attente active si le tampon est vide
            thread_mutex_unlock(&mutex);
            thread_yield(); // Relâcher le CPU pour permettre au producteur de s'exécuter
            thread_mutex_lock(&mutex);
        }
        item = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        count--;
        printf("Consommateur a consommé l'élément %d\n", item);
        thread_mutex_unlock(&mutex);
    }
    thread_exit(NULL);
    return NULL;
}

int main()
{
    thread_t threads[2];
    srand(time(NULL)); // Initialiser le générateur de nombres aléatoires

    // Créer les threads producteur et consommateur
    thread_create(&threads[0], producer, NULL);
    thread_create(&threads[1], consumer, NULL);

    // Attendre la fin des threads
    int i;
    for (i = 0; i < 2; i++)
    {
        thread_join(threads[i], NULL);
    }

    return 0;
}
