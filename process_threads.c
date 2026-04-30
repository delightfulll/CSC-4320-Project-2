// on mac, had to use named semaphores (sem_open / sem_unlink)
// because sem_init is deprecated on macOS. On Linux you could use sem_init
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// max number of processes the program will read from the file
#define MAX_PROCESSES 100
// size of the bounded buffer for producer-consumer
#define BUFFER_SIZE 5

// one row from processes.txt, holds all the info for a single process
typedef struct
{
    int pid;          // process id
    int arrival_time; // when the process "arrives" (in seconds)
    int burst_time;   // how long the CPU burst is (in seconds)
    int priority;     // priority (not really used here, just stored)
    int memory_size;  // memory size (also just stored)
} Process;

// shared stuff that every thread needs to see
Process buffer[BUFFER_SIZE]; // the bounded buffer
int in_index = 0;            // where the next producer will write
int out_index = 0;           // where the next consumer will read
int total_processes = 0;     // how many processes the consumer should expect

// buffer_mutex makes sure only one thread changes the buffer at a time
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
// print_mutex is just so the printf output does not get mixed up between threads
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
// counts how many empty slots are left in the buffer (starts at BUFFER_SIZE)
sem_t *empty_slots;
// counts how many full slots are in the buffer (starts at 0)
sem_t *full_slots;

// each process from processes.txt runs this function as its own thread
void *run_process(void *arg)
{
    // copy the process info so we don't have to keep dereferencing the pointer
    Process p = *(Process *)arg;

    // wait until the process "arrives" by sleeping for arrival_time seconds
    // this is how the assignment says to simulate timing
    sleep(p.arrival_time);

    pthread_mutex_lock(&print_mutex);
    printf("Process %d arrived (burst=%d).\n", p.pid, p.burst_time);
    printf("Process %d started.\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    // sleeping here is acting like the actual CPU burst
    sleep(p.burst_time);

    pthread_mutex_lock(&print_mutex);
    printf("Process %d finished.\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    // producer part
    // now this thread becomes a producer and puts itself in the buffer

    pthread_mutex_lock(&print_mutex);
    printf("[Producer %d] Waiting for empty slot...\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    // wait until there is at least one empty slot in the buffer
    // if the buffer is full, sem_wait blocks here until the consumer takes something
    sem_wait(empty_slots);

    pthread_mutex_lock(&print_mutex);
    printf("[Producer %d] Waiting for buffer lock...\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    // lock the buffer so no other thread changes it while we add to it
    pthread_mutex_lock(&buffer_mutex);

    pthread_mutex_lock(&print_mutex);
    printf("[Producer %d] Acquired lock, adding to buffer.\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    // put the process into the buffer at the in_index spot
    buffer[in_index] = p;
    // move in_index forward, wrap around to 0 when it hits the end (circular buffer)
    in_index = (in_index + 1) % BUFFER_SIZE;

    // done writing, release the lock so other threads can use the buffer
    pthread_mutex_unlock(&buffer_mutex);
    // tell the consumer there is one more full slot to read
    sem_post(full_slots);

    pthread_mutex_lock(&print_mutex);
    printf("[Producer %d] Released lock and signaled full slot.\n", p.pid);
    pthread_mutex_unlock(&print_mutex);

    return NULL;
}

// the consumer thread - removes finished processes from the buffer
void *consumer(void *arg)
{
    (void)arg; // unused, but pthread_create requires this signature

    // we know exactly how many processes there are, so loop that many times
    for (int i = 0; i < total_processes; i++)
    {
        pthread_mutex_lock(&print_mutex);
        printf("[Consumer] Waiting for full slot...\n");
        pthread_mutex_unlock(&print_mutex);

        // wait until at least one slot in the buffer is full
        sem_wait(full_slots);

        pthread_mutex_lock(&print_mutex);
        printf("[Consumer] Waiting for buffer lock...\n");
        pthread_mutex_unlock(&print_mutex);

        // lock the buffer before reading from it
        pthread_mutex_lock(&buffer_mutex);

        pthread_mutex_lock(&print_mutex);
        printf("[Consumer] Acquired lock.\n");
        pthread_mutex_unlock(&print_mutex);

        // grab the next process from the buffer
        Process p = buffer[out_index];
        // move out_index forward, wrap around for circular buffer
        out_index = (out_index + 1) % BUFFER_SIZE;

        // done reading, release the lock
        pthread_mutex_unlock(&buffer_mutex);
        // tell the producers there is one more empty slot now
        sem_post(empty_slots);

        pthread_mutex_lock(&print_mutex);
        printf("[Consumer] Removed process %d from buffer.\n", p.pid);
        pthread_mutex_unlock(&print_mutex);
    }

    return NULL;
}

// reads processes from processes.txt into the array
// returns how many processes were read
int read_processes(Process arr[])
{
    FILE *f = fopen("processes.txt", "r");
    if (f == NULL)
    {
        printf("Could not open processes.txt\n");
        return 0;
    }

    // skip the header line (PID Arrival_Time Burst_Time Priority Memory_Size)
    char header[256];
    fgets(header, sizeof(header), f);

    // read each row until the file ends or we hit MAX_PROCESSES
    int count = 0;
    while (count < MAX_PROCESSES)
    {
        Process p;
        int n = fscanf(f, "%d %d %d %d %d",
                       &p.pid, &p.arrival_time, &p.burst_time,
                       &p.priority, &p.memory_size);
        // if fscanf didn't read all 5 numbers, we are done
        if (n != 5)
            break;
        arr[count++] = p;
    }

    fclose(f);
    return count;
}

int main(void)
{
    // load all the processes from the file before making any threads
    Process processes[MAX_PROCESSES];
    total_processes = read_processes(processes);

    if (total_processes <= 0)
    {
        printf("No processes found in processes.txt.\n");
        return 1;
    }

    // macOS named semaphore setup
    // names have to start with "/" and be unique, so add the pid
    // this stops collisions if the program is run more than once
    char empty_name[64], full_name[64];
    snprintf(empty_name, sizeof(empty_name), "/empty_%d", getpid());
    snprintf(full_name, sizeof(full_name), "/full_%d", getpid());

    // unlink first in case an old semaphore with the same name is still around
    sem_unlink(empty_name);
    sem_unlink(full_name);

    // empty_slots starts at BUFFER_SIZE because the buffer is empty
    empty_slots = sem_open(empty_name, O_CREAT, 0600, BUFFER_SIZE);
    // full_slots starts at 0 because nothing has been produced yet
    full_slots = sem_open(full_name, O_CREAT, 0600, 0);

    if (empty_slots == SEM_FAILED || full_slots == SEM_FAILED)
    {
        printf("Could not create semaphores: %s\n", strerror(errno));
        return 1;
    }

    // start the consumer first so it is ready when the producers finish
    pthread_t consumer_tid;
    pthread_create(&consumer_tid, NULL, consumer, NULL);

    // make one thread per process from the file
    pthread_t threads[MAX_PROCESSES];
    for (int i = 0; i < total_processes; i++)
    {
        pthread_create(&threads[i], NULL, run_process, &processes[i]);
    }

    // wait for all the process threads to finish
    for (int i = 0; i < total_processes; i++)
    {
        pthread_join(threads[i], NULL);
    }
    // wait for the consumer to finish too (it stops after total_processes items)
    pthread_join(consumer_tid, NULL);

    // clean up the semaphores so they don't stick around in the system
    sem_close(empty_slots);
    sem_close(full_slots);
    sem_unlink(empty_name);
    sem_unlink(full_name);

    printf("All processes completed.\n");
    return 0;
}
