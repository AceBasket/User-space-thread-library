#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include "thread.h"

int *arr;
int *tmp_arr;
int N;

struct sort_args
{
    int start;
    int end;
};

void merge(int start, int mid, int end)
{
    int i, j, k;
    i = start;
    j = mid + 1;
    k = start;
    while (i <= mid && j <= end)
    {
        if (arr[i] <= arr[j])
        {
            tmp_arr[k++] = arr[i++];
        }
        else
        {
            tmp_arr[k++] = arr[j++];
        }
    }
    while (i <= mid)
    {
        tmp_arr[k++] = arr[i++];
    }
    while (j <= end)
    {
        tmp_arr[k++] = arr[j++];
    }
    for (i = start; i <= end; i++)
    {
        arr[i] = tmp_arr[i];
    }
}

void *sort(void *args)
{
    struct sort_args *sort_args = (struct sort_args *)args;
    int start = sort_args->start;
    int end = sort_args->end;

    if (start >= end)
    {
        return NULL;
    }

    int mid = (start + end) / 2;

    struct sort_args left_args = {start, mid};
    struct sort_args right_args = {mid + 1, end};

    thread_t left_thread, right_thread;
    thread_create(&left_thread, sort, &left_args);
    thread_create(&right_thread, sort, &right_args);
    thread_join(left_thread, NULL);
    thread_join(right_thread, NULL);

    merge(start, mid, end);

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error : triCole needs 1 argument (len of list to sort)");
        return 1;
    }
    N = atoi(argv[1]);

    arr = (int *)malloc(N * sizeof(int));
    tmp_arr = (int *)malloc(N * sizeof(int));

    srand(time(NULL));
    for (int i = 0; i < N; i++)
    {
        arr[i] = rand() % 100;
    }

    struct sort_args args = {0, N - 1};

    thread_t main_thread;
    thread_create(&main_thread, sort, &args);
    thread_join(main_thread, NULL);

    for (int i = 0; i < N; i++)
    {
        printf("%d ", arr[i]);
    }
    printf("\n");

    free(arr);
    free(tmp_arr);

    return 0;
}
