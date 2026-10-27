#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include "../utils/timer.h"

#define BUFFER_SIZE 4096

typedef struct
{
    int critical;
    int error;
    int failed;
} Result;

typedef struct
{
    int fd;
    int thread_id;
    int nthreads;
    int n_blocks;
    off_t block_size;
    off_t filesize;
    Result total;
} PoolArgs;

off_t adjustBoundaries(int fd, off_t offset, off_t filesize)
{
    if (offset <= 0)
        return 0;
    if (offset >= filesize)
        return filesize;

    char c;
    off_t pos = offset - 1;

    while (pos < filesize && pread(fd, &c, 1, pos) == 1)
    {
        pos++;
        if (c == '\n')
            break;
    }
    return pos;
}

// traite les blocs donnes a chaque threads
void *pool_worker(void *arg)
{
    PoolArgs *a = (PoolArgs *)arg;

    for (int b = a->thread_id; b < a->n_blocks; b += a->nthreads)
    {
        off_t start = (off_t)b * a->block_size;
        off_t end = start + a->block_size;

        if (end > a->filesize)
            end = a->filesize;

        off_t pos = adjustBoundaries(a->fd, start, a->filesize);    // aligne au debut de la ligne
        off_t real_end = adjustBoundaries(a->fd, end, a->filesize); // ajuste la fin

        char buffer[BUFFER_SIZE + 1];

        while (pos < real_end)
        {
            size_t to_read = BUFFER_SIZE;
            off_t restant = real_end - pos;

            if (restant < (off_t)BUFFER_SIZE)
                to_read = (size_t)restant;

            ssize_t bytes = pread(a->fd, buffer, to_read, pos);

            if (bytes <= 0)
                break;

            ssize_t used = bytes;

            // enlever les lignes incompletes, pour pas couper des mots
            if (pos + bytes < real_end)
            {
                ssize_t i = bytes - 1;
                while (i >= 0 && buffer[i] != '\n')
                    i--;

                if (i >= 0)
                    used = i + 1;
            }

            buffer[used] = '\0'; // pour avoir comme string

            char *p = buffer;

            while ((p = strstr(p, "CRITICAL")) != NULL)
            {
                a->total.critical++;
                p++;
            }

            p = buffer;
            while ((p = strstr(p, "ERROR")) != NULL)
            {
                a->total.error++;
                p++;
            }

            p = buffer;
            while ((p = strstr(p, "FAILED_LOGIN")) != NULL)
            {
                a->total.failed++;
                p++;
            }

            pos += used;
        }
    }

    return NULL;
}

void process_file_pool(const char *filename, int n_blocks, int nthreads,
                       int *crit_total, int *err_total, int *fail_total)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        perror(filename);
        return;
    }

    off_t filesize = lseek(fd, 0, SEEK_END);
    if (filesize == -1)
    {
        perror("lseek");
        close(fd);
        return;
    }

    off_t block_size = (filesize + n_blocks - 1) / n_blocks;

    pthread_t threads[nthreads];
    PoolArgs args[nthreads];

    for (int i = 0; i < nthreads; i++)
    {
        args[i].fd = fd;
        args[i].thread_id = i;
        args[i].nthreads = nthreads;
        args[i].n_blocks = n_blocks;
        args[i].block_size = block_size;
        args[i].filesize = filesize;
        args[i].total = (Result){0, 0, 0};

        int threadCreated = pthread_create(&threads[i], NULL, pool_worker, &args[i]);

        if (threadCreated != 0)
        {
            fprintf(stderr, "pthread_create: %s\n", strerror(threadCreated));
            break;
        }
    }

    for (int i = 0; i < nthreads; i++)
    {
        pthread_join(threads[i], NULL);

        *crit_total += args[i].total.critical;
        *err_total += args[i].total.error;
        *fail_total += args[i].total.failed;
    }

    close(fd);
}

typedef struct
{
    const char *log1;
    const char *log2;
    int n_blocks;
    int nthreads;
    Result total;
} pool_args_t;

void pool_task_wrapper(void *arg)
{
    pool_args_t *a = (pool_args_t *)arg;

    a->total = (Result){0, 0, 0};

    process_file_pool(a->log1, a->n_blocks, a->nthreads,
                      &a->total.critical, &a->total.error, &a->total.failed);
    process_file_pool(a->log2, a->n_blocks, a->nthreads,
                      &a->total.critical, &a->total.error, &a->total.failed);
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        printf("Usage: %s <log1> <log2> <N_BLOCS> <N_THREADS>\n", argv[0]);
        return 1;
    }

    pool_args_t args;
    args.log1 = argv[1];
    args.log2 = argv[2];

    printf("Enter the number of threads: ");
    scanf("%d", &args.nthreads);

    printf("Enter the number of blocks: ");
    scanf("%d", &args.n_blocks);
    fflush(stdout);

    if (args.nthreads < 1)
    {
        fprintf(stderr, "N_THREADS doit être >= 1\n");
        return 1;
    }

    if (args.n_blocks < 1)
    {
        fprintf(stderr, "N_BLOCS doit être >= 1\n");
        return 1;
    }

    system("mkdir -p bin");

    double elapsed = measure_time(pool_task_wrapper, &args);
    printf("Temps d'exécution (pool de %d threads, %d blocs) = %.6f secondes\n",
           args.nthreads, args.n_blocks, elapsed);

    printf("\nTotal:\n");
    printf("CRITICAL: %d\n", args.total.critical);
    printf("ERROR: %d\n", args.total.error);
    printf("FAILED_LOGIN: %d\n", args.total.failed);

    return 0;
}
