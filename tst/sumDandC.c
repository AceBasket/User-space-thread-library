#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define NUM_THREADS 2

// Structure qui contient les paramètres pour la fonction sum_thread()
typedef struct
{
    int *arr;
    int start;
    int end;
    int sum;
} ThreadArg;

int N;

// Fonction qui calcule la somme d'un tableau dans un thread
void *sum_thread(void *arg)
{
    ThreadArg *targ = (ThreadArg *)arg;
    targ->sum = 0;
    int i;
    for (i = targ->start; i < targ->end; i++)
    {
        targ->sum += targ->arr[i];
    }
    pthread_exit(NULL);
}

// Fonction qui calcule la somme d'un tableau en utilisant la technique "diviser pour régner"
int sum_divide(int arr[], int start, int end)
{
    if (end - start == 1)
    { // Cas de base : tableau de taille 1
        return arr[start];
    }
    else
    { // Cas récursif : diviser en deux parties et sommer les deux parties
        int mid = (start + end) / 2;

        // Créer les arguments pour les threads
        ThreadArg targ_left = {arr, start, mid, 0};
        ThreadArg targ_right = {arr, mid, end, 0};

        // Créer les threads
        pthread_t threads[NUM_THREADS];
        pthread_create(&threads[0], NULL, sum_thread, (void *)&targ_left);
        pthread_create(&threads[1], NULL, sum_thread, (void *)&targ_right);

        // Attendre la fin des threads et récupérer les résultats
        int i, sum = 0;
        for (i = 0; i < NUM_THREADS; i++)
        {
            pthread_join(threads[i], NULL);
            sum += (i == 0) ? targ_left.sum : targ_right.sum;
        }

        return sum;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error : triCole needs 1 argument (len of list to sort)");
        return 1;
    }
    N = atoi(argv[1]);
    int i, s;
    int arr[N];
    srand(time(NULL)); // Initialiser le générateur de nombres aléatoires

    // Initialiser le tableau avec des entiers aléatoires entre 0 et 99 inclus
    for (i = 0; i < N; i++)
    {
        arr[i] = i + 1;
    }

    // Calculer la somme du tableau en utilisant la fonction sum_divide()
    s = sum_divide(arr, 0, N);
    printf("Somme du tableau : %d\n", s);

    return 0;
}
