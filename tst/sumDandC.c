#include <stdio.h>
#include <stdlib.h>
#include "thread.h"
#include <assert.h>
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
    thread_exit(NULL);
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
        thread_t threads[NUM_THREADS];
        thread_create(&threads[0], sum_thread, (void *)&targ_left);
        thread_create(&threads[1], sum_thread, (void *)&targ_right);

        // Attendre la fin des threads et récupérer les résultats
        int i, sum = 0;
        for (i = 0; i < NUM_THREADS; i++)
        {
            thread_join(threads[i], NULL);
            sum += (i == 0) ? targ_left.sum : targ_right.sum;
        }

        return sum;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error : sumDandC needs 1 argument (N for summing int from 0 to N-1)");
        return 1;
    }
    N = atoi(argv[1]);
    int i, s;
    int arr[N];

    // Initialiser le tableau avec la suite d'entiers entre 0 et N-1
    for (i = 0; i < N; i++)
    {
        arr[i] = i + 1;
    }

    // Calculer la somme du tableau en utilisant la fonction sum_divide()
    s = sum_divide(arr, 0, N);
    assert(s == (N * (N + 1)) / 2);
    printf("Array sum : %d\n", s);

    return 0;
}
