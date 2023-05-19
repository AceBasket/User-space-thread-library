#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include "thread.h"

/* test pour verifier que prendre un mutex ne desactive pas l'equité envers les autres threads.
 *
 * valgrind doit etre content.
 * Le programme doit finir.
 *
 * support nécessaire:
 * - thread_create()
 * - retour sans thread_exit()
 * - thread_join() sans récupération de la valeur de retour
 * - thread_mutex_init()
 * - thread_mutex_destroy()
 * - thread_mutex_lock()
 * - thread_mutex_unlock()
 */

int fini = 0;
thread_mutex_t lock;

static void * thfunc(void *dummy __attribute__((unused)))
{
    unsigned i;

    /* on incremente progressivement fini jusque 5 pour debloquer le main */
    for(i=0; i<5; i++) {
      thread_yield();
      fini++;
    }

    /* on attend que main remette à 0 */
    thread_mutex_lock(&lock);
    while (fini != 0)
      thread_yield();
    thread_mutex_unlock(&lock);

    thread_exit(NULL);
}

int main(void)
{
  thread_t th;
  int err, i;
  struct timeval tv1, tv2;
  unsigned long us;

  gettimeofday(&tv1, NULL);

  /* on cree le mutex et le thread */
  if (thread_mutex_init(&lock) != 0) {
      fprintf(stderr, "thread_mutex_init failed\n");
      return -1;
  }
  err = thread_create(&th, thfunc, NULL);
  assert(!err);

  /* on prend le lock puis on attend que l'autre mette fini = 5 */
  thread_mutex_lock(&lock);
  while (fini != 5)
    thread_yield();
  thread_mutex_unlock(&lock);

  /* on baisse progressivement jusque 0 */
  for(i=0; i<5; i++) {
    thread_yield();
    fini--;
  }

  /* fini */
  err = thread_join(th, NULL);
  assert(!err);
  gettimeofday(&tv2, NULL);
  thread_mutex_destroy(&lock);

  us = (tv2.tv_sec-tv1.tv_sec)*1000000+(tv2.tv_usec-tv1.tv_usec);
  printf("%lu us\n", us);
  return EXIT_SUCCESS;
}
