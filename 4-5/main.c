#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CUSTOMERS 5

#define BARBER_SEM "/barber_semaphore"
#define CUSTOMER_SEM "/customer_semaphore"
#define MUTEX_SEM "/mutex_semaphore"

struct SharedData {
    int waiting_customers[MAX_CUSTOMERS];
    int num_waiting;
};
struct SharedData *shared_data = NULL;
sem_t *barber_semaphore = NULL;
sem_t *customer_semaphore = NULL;
sem_t *mutex_semaphore = NULL;
int NUM_CHAIRS;

void cleanup_resources() {
    sem_close(barber_semaphore);
    sem_unlink(BARBER_SEM);
    sem_close(customer_semaphore);
    sem_unlink(CUSTOMER_SEM);
    sem_close(mutex_semaphore);
    sem_unlink(MUTEX_SEM);

    munmap(shared_data, sizeof(struct SharedData));
    shm_unlink("/barbershop_shared_memory");
}

void signal_handler(int sig) {
    cleanup_resources();
    exit(0);
}

void barberProcess() {
    while (1) {
        printf("The barber is sleeping...\n");
        sem_wait(barber_semaphore);
        sem_wait(mutex_semaphore);
        if (shared_data->num_waiting > 0) {
            int customer = shared_data->waiting_customers[0];
            printf("The barber is cutting hair for customer %d\n", customer);
            for (int i = 0; i < shared_data->num_waiting - 1; ++i) {
                shared_data->waiting_customers[i] = shared_data->waiting_customers[i + 1];
            }
            shared_data->num_waiting--;
            sem_post(mutex_semaphore);
            usleep(rand() % 5000000 + 1000);
            printf("The barber has finished cutting hair for customer %d\n", customer);
            sem_post(customer_semaphore);
        } else {
            sem_post(mutex_semaphore);
        }
    }
}

void customerProcess(int index) {
    usleep(rand() % 5000000 + 1000);
    sem_wait(mutex_semaphore);
    if (shared_data->num_waiting < NUM_CHAIRS) {
        shared_data->waiting_customers[shared_data->num_waiting] = index;
        shared_data->num_waiting++;
        printf("Customer %d is waiting in the waiting room\n", index);
        sem_post(mutex_semaphore);
        sem_post(barber_semaphore);
        sem_wait(customer_semaphore);
        printf("Customer %d has finished getting a haircut\n", index);
    } else {
        printf("Customer %d is leaving because the waiting room is full\n", index);
        sem_post(mutex_semaphore);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <num_chairs>\n", argv[0]);
        return 1;
    }
    NUM_CHAIRS = atoi(argv[1]);
    if (NUM_CHAIRS < 1 || NUM_CHAIRS > MAX_CUSTOMERS) {
        fprintf(stderr, "Invalid number of chairs. It must be between 1 and %d\n", MAX_CUSTOMERS);
        return 1;
    }
    srand(time(NULL));
    barber_semaphore = sem_open(BARBER_SEM, O_CREAT | O_EXCL, 0666, 0);
    customer_semaphore = sem_open(CUSTOMER_SEM, O_CREAT | O_EXCL, 0666, 0);
    mutex_semaphore = sem_open(MUTEX_SEM, O_CREAT | O_EXCL, 0666, 1);
    int shared_memory_fd = shm_open("/barbershop_shared_memory", O_CREAT | O_RDWR, 0666);
    ftruncate(shared_memory_fd, sizeof(struct SharedData));
    shared_data = (struct SharedData *) mmap(NULL, sizeof(struct SharedData), PROT_READ | PROT_WRITE,
                                             MAP_SHARED, shared_memory_fd, 0);
    close(shared_memory_fd);

    pid_t barber_pid = fork();
    if (barber_pid == 0) {
        barberProcess();
        exit(EXIT_SUCCESS);
    }

    int num_customers = 0;
    for (int i = 0; i < MAX_CUSTOMERS; ++i) {
        pid_t customer_pid = fork();
        if (customer_pid == 0) {
            customerProcess(i);
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
