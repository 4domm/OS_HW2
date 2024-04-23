#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>

#define MAX_CUSTOMERS 5
#define SHM_NAME "/barbershop_shared_memory"

struct SharedData {
    sem_t barber_semaphore;
    sem_t customer_semaphore;
    sem_t mutex;
    int waiting_customers[MAX_CUSTOMERS];
    int num_waiting;
    int waiting_capacity;
};

int shm_fd = -1;
struct SharedData *shm = NULL;

void cleanup_resources() {
    sem_destroy(&shm->barber_semaphore);
    sem_destroy(&shm->customer_semaphore);
    sem_destroy(&shm->mutex);
    if (shm != NULL) {
        if (munmap(shm, sizeof(struct SharedData)) == -1) {
            perror("Incorrect munmap");
        }
        shm = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(SHM_NAME);
        shm_fd = -1;
    }
}

void signal_handler(int sig) {
    cleanup_resources();
    exit(0);
}

void barberProcess(struct SharedData *shared_data) {
    while (1) {
        printf("The barber is sleeping...\n");
        sem_wait(&shared_data->barber_semaphore);
        sem_wait(&shared_data->mutex);
        if (shared_data->num_waiting > 0) {
            int customer = shared_data->waiting_customers[0];
            printf("The barber is cutting hair for customer %d\n", customer);
            for (int i = 0; i < shared_data->num_waiting - 1; ++i) {
                shared_data->waiting_customers[i] = shared_data->waiting_customers[i + 1];
            }
            shared_data->num_waiting--;
            sem_post(&shared_data->mutex);
            usleep(rand() % 5000000 + 1000);
            printf("The barber has finished cutting hair for customer %d\n", customer);
            sem_post(&shared_data->customer_semaphore);
        } else {
            sem_post(&shared_data->mutex);
        }
    }
}

void customerProcess(struct SharedData *shared_data, int index) {
    usleep(rand() % 5000000 + 1000);
    sem_wait(&shared_data->mutex);
    if (shared_data->num_waiting < shared_data->waiting_capacity) {
        shared_data->waiting_customers[shared_data->num_waiting] = index;
        shared_data->num_waiting++;
        printf("Customer %d is waiting in the waiting room\n", index);
        sem_post(&shared_data->mutex);
        sem_post(&shared_data->barber_semaphore);
        sem_wait(&shared_data->customer_semaphore);
        printf("Customer %d has finished getting a haircut\n", index);
    } else {
        printf("Customer %d is leaving because the waiting room is full\n", index);
        sem_post(&shared_data->mutex);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_chairs>\n", argv[0]);
        return 1;
    }

    int num_chairs = atoi(argv[1]);
    if (num_chairs < 1 || num_chairs > MAX_CUSTOMERS) {
        fprintf(stderr, "Invalid number of chairs. It must be between 1 and %d\n", MAX_CUSTOMERS);
        return 1;
    }

    srand(time(NULL));
    signal(SIGINT, signal_handler);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(struct SharedData));
    shm = (struct SharedData *) mmap(NULL, sizeof(struct SharedData), PROT_READ | PROT_WRITE,
                                     MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    sem_init(&shm->barber_semaphore, 1, 0);
    sem_init(&shm->customer_semaphore, 1, 0);
    sem_init(&shm->mutex, 1, 1);

    shm->waiting_capacity = num_chairs;

    pid_t barber_pid = fork();
    if (barber_pid == 0) {
        barberProcess(shm);
        exit(EXIT_SUCCESS);
    }

    int num_customers = 0;
    for (int i = 0; i < MAX_CUSTOMERS; ++i) {
        pid_t customer_pid = fork();
        if (customer_pid == 0) {
            customerProcess(shm, i);
            exit(EXIT_SUCCESS);
        }
        num_customers++;
    }

    for (int i = 0; i < num_customers; ++i) {
        wait(NULL);
    }

    kill(barber_pid, SIGKILL);
    cleanup_resources();

    return 0;
}