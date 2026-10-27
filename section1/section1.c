#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

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
    off_t start;
    off_t end;
    Result *res;
} ThreadArgs;

typedef struct
{
    const char *log1;
    const char *log2;
    int n;
} thread_args_t;

typedef struct
{
    const char *filename;
    int n;
    Result total;
} FileArgs;

void *thread_count(void *arg)
{
    ThreadArgs *targ = (ThreadArgs *)arg;

    for (off_t pos = targ->start; pos < targ->end;)
    {
        char buffer[BUFFER_SIZE];
        int length = 0;
        char c;
        ssize_t seen = 1;

        while (pos < targ->end && (seen = pread(targ->fd, &c, 1, pos)) == 1)
        {
            pos++;
            if (length < BUFFER_SIZE - 1)
            {
                buffer[length++] = c;
            }
            if (c == '\n')
            {
                break;
            }
        }

        buffer[length] = '\0';

        if (strstr(buffer, "CRITICAL") != NULL)
        {
            targ->res->critical++;
        }
        if (strstr(buffer, "ERROR") != NULL)
        {
            targ->res->error++;
        }
        if (strstr(buffer, "FAILED_LOGIN") != NULL)
        {
            targ->res->failed++;
        }

        if (seen <= 0)
        {
            break;
        }
    }

    return NULL;
}

void separate_file(int n, int fd, off_t *separations)
{
    off_t size = lseek(fd, 0, SEEK_END);
    separations[0] = 0;

    for (int i = 1; i < n; i++)
    {
        off_t target = (size * i) / n;
        lseek(fd, target, SEEK_SET);

        char c;
        while (read(fd, &c, 1) == 1)
        {
            if (c == '\n')
            {
                break;
            }
        }

        separations[i] = lseek(fd, 0, SEEK_CUR);
    }

    separations[n] = size;
}

void process_file_threads(const char *filename, int n, int *crit_total, int *err_total, int *fail_total)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        perror("open");
        exit(1);
    }

    off_t separations[n + 1];
    pthread_t threads[n];
    ThreadArgs args[n];
    Result results[n];
    memset(results, 0, sizeof(results));

    separate_file(n, fd, separations);

    for (int i = 0; i < n; i++)
    {
        args[i].fd = fd;
        args[i].start = separations[i];
        args[i].end = separations[i + 1];
        args[i].res = &results[i];

        if (pthread_create(&threads[i], NULL, thread_count, &args[i]) != 0)
        {
            fprintf(stderr, "Erreur pthread_create\n");
            exit(1);
        }
    }

    for (int i = 0; i < n; i++)
    {
        pthread_join(threads[i], NULL);
    }

    *crit_total = 0;
    *err_total = 0;
    *fail_total = 0;

    for (int i = 0; i < n; i++)
    {
        *crit_total += results[i].critical;
        *err_total += results[i].error;
        *fail_total += results[i].failed;
    }

    close(fd);
}

void *file_thread(void *arg)
{
    FileArgs *file = (FileArgs *)arg;
    process_file_threads(file->filename, file->n,
                         &file->total.critical, &file->total.error, &file->total.failed);
    return NULL;
}

void thread_task_wrapper(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    FileArgs files[2] = {
        {args->log1, args->n, {0}},
        {args->log2, args->n, {0}}
    };
    pthread_t threads[2];

    for (int i = 0; i < 2; i++)
    {
        if (pthread_create(&threads[i], NULL, file_thread, &files[i]) != 0)
        {
            fprintf(stderr, "Erreur pthread_create\n");
            exit(1);
        }
    }

    for (int i = 0; i < 2; i++)
    {
        pthread_join(threads[i], NULL);
    }

    FILE *fptr = fopen("RESULT_THREADS.txt", "w");
    if (fptr == NULL)
    {
        perror("fopen");
        exit(1);
    }

    fprintf(fptr, "Log 1:\n");
    fprintf(fptr, "CRITICAL: %d\n", files[0].total.critical);
    fprintf(fptr, "ERROR: %d\n", files[0].total.error);
    fprintf(fptr, "FAILED_LOGIN: %d\n", files[0].total.failed);

    fprintf(fptr, "\nLog 2:\n");
    fprintf(fptr, "CRITICAL: %d\n", files[1].total.critical);
    fprintf(fptr, "ERROR: %d\n", files[1].total.error);
    fprintf(fptr, "FAILED_LOGIN: %d\n", files[1].total.failed);

    fprintf(fptr, "\nTotal:\n");
    fprintf(fptr, "CRITICAL: %d\n", files[0].total.critical + files[1].total.critical);
    fprintf(fptr, "ERROR: %d\n", files[0].total.error + files[1].total.error);
    fprintf(fptr, "FAILED_LOGIN: %d\n", files[0].total.failed + files[1].total.failed);

    fclose(fptr);
}

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4)
    {
        printf("Usage: %s <log1> <log2> [N]\n", argv[0]);
        return 1;
    }

    thread_args_t args;
    args.log1 = argv[1];
    args.log2 = argv[2];

    if (argc == 4)
    {
        args.n = atoi(argv[3]);
    }
    else
    {
        printf("Enter the value of n: ");
        if (scanf("%d", &args.n) != 1)
        {
            return 1;
        }
    }

    if (args.n < 1)
    {
        return 1;
    }

    printf("Logs 1: %s\n", args.log1);
    printf("Logs 2: %s\n", args.log2);
    printf("n: %d\n", args.n);

    thread_task_wrapper(&args);

    return 0;
}