///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2020. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

// APIs that implement POSIX semaphores.

#ifndef ZOS_SEMAPHORE_H_
#define ZOS_SEMAPHORE_H_

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/sem.h>

#if __WORDSIZE == 64
#define __SIZEOF_SEM_T 32
#else
#define __SIZEOF_SEM_T 16
#endif

#define SEM_FAILED ((sem_t *)0)

typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  unsigned int value;
} sem_t;

int sem_init(sem_t *semid, int pshared, unsigned int value);

int sem_destroy(sem_t *semid);

int sem_wait(sem_t *semid);

int sem_trywait(sem_t *semid);

int sem_post(sem_t *semid);

int sem_timedwait(sem_t *semid, const struct timespec *timeout);

#endif // ZOS_SEMAPHORE_H_
