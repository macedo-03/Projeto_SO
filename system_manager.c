// José Francisco Branquinho Macedo - 2021221301
// Miguel Filipe Mota Cruz - 2021219294

// ./home_iot config_file.txt
// ./user_console 123
// ./sensor 2342 7 TEMP10 1 10
// add_alert alert1 TEMP1 3 6

// #define DEBUG //remove this line to remove debug messages (...)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>

#include "costumio.h"
#include "internal_queue.h"

#define CONSOLE_PIPE "CONSOLE_PIPE"
#define SENSOR_PIPE "SENSOR_PIPE"
#define MQ_KEY 4444
#define STR_SIZE 64
#define BUF_SIZE 1024

typedef struct
{
    char key[STR_SIZE];
    int last_value, min_value, max_value, n_updates;
    double average;
} key_data;

typedef struct
{
    long user_console_id;
    char alert_id[STR_SIZE], key[STR_SIZE];
    int alert_min, alert_max;
} Alert;

void get_time();
void write_to_log(char *message_to_log);
void worker_process(int worker_number, int from_dispatcher_pipe[2]);
void alerts_watcher_process();
void *sensor_reader();
void *console_reader();
void *dispatcher();
void cleaner();
void error_cleaner();
void handler(int signum);
int main(int argc, char *argv[]);

pthread_mutex_t internal_queue_mutex = PTHREAD_MUTEX_INITIALIZER; // protects access to internal queue

pthread_t thread_console_reader; // pthread responsible for reading user console messages
pthread_t thread_sensor_reader;  // pthread responsible for reading sensor messages
pthread_t thread_dispatcher;     // pthread responsible for dispatching messages to workers

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS; // values from configuration file
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];

int ppid, alert_watcher_id, thread_count, worker_count, worker_index;

int shmid;
void *shm_global;
int console_pipe_id, sensor_pipe_id;
int **disp_work_pipe;
int mq_id;
FILE *log_file;

sem_t internal_queue_full_count;  // semaphore whose value corresponds to the number of full slots in internal queue
sem_t internal_queue_empty_count; // semaphore whose value corresponds to the number of empty slots in internal queue

sem_t *sem_data_base_reader;   // semaphore to restrict access to variable 'database_readers' to sync access to 'data_base'
sem_t *sem_data_base_writer;   // semaphore to exclusive lock access to 'data_base'
sem_t *sem_alert_list_reader;  // semaphore to restrict access to variable 'alert_readers' to sync access to 'alert_list'
sem_t *sem_alert_list_writer;  // semaphore to exclusive lock access to 'alert_list'
sem_t *sem_sensor_list_reader; // semaphore to restrict access to variable 'sensor_readers' to sync access to 'sensor_list'
sem_t *sem_sensor_list_writer; // semaphore to exclusive lock access to 'sensor_list'
sem_t *sem_keys_bitmap;        // sempahore to exclusive lock access to 'keys_bitmap'
sem_t *log_mutex;              // sempahore to sync exclusive lock the writing to log_file
sem_t *sem_free_worker_count;  // semaphore whose value corresponds to the number of workers available
sem_t *sem_alert_watcher;      // semaphore to let alert_watcher verify data_base values
sem_t *sem_alert_worker;       // semaphore to let worker end sensor message processing after alert_watcher has verified it

InternalQueue *internal_queue_console; // linked list to internal queue with console messages
InternalQueue *internal_queue_sensor;  // linked list to internal queue with sensor messages

key_data *data_base;                                    // data_base (data from sensors) - shm adress
Alert *alert_list;                                      // list of alerts - shm adress
char **sensor_list;                                     // list of sensors - shm adress
int *workers_bitmap;                                    // bitmap to register workers availability - shm adress
int *keys_bitmap;                                       // bitmap to register which indice of the data_base has been modified
int *count_sensors, *count_alerts, *count_key_data;     // variables to register the number of current sensors, alerts and keys
int *sensor_readers, *alert_readers, *database_readers; // variables to register the number of processess reading each section of the shm (data_base, alert_list, sensor_list)-> lets multiple readers access it and block any writer

time_t t;
struct tm *time_info;
char temp[10];

struct sigaction action;
sigset_t block_set, block_extra_set;

// function to define current time -> hh:mm:ss
void get_time()
{
    time(&t);
    time_info = localtime(&t);
    sprintf(temp, "%2d:%2d:%2d", time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
}

// function to write messages to log and screen
void write_to_log(char *message_to_log)
{
    // exclusive lock access to log_file
    sem_wait(log_mutex);
    // write messages
    get_time();
    printf("%s %s\n", temp, message_to_log);
    fprintf(log_file, "%s %s\n", temp, message_to_log);
    // unlock access to log_file
    sem_post(log_mutex);
}

// function for each worker process
void worker_process(int worker_number, int from_dispatcher_pipe[2])
{
    int i, j;
    worker_index = worker_number;

    // close unnamed pipes not being used by this worker/process
    for (i = 0; i < N_WORKERS; ++i)
    {
        if (i != worker_number)
        {
            close(disp_work_pipe[i][0]);
            close(disp_work_pipe[i][1]);
        }
    }

    char alert_id[STR_SIZE], key[STR_SIZE], sensor_id[STR_SIZE];
    int min, max, value;
    int validated, new_sensor, new_key;

    Message message_to_process;
    Message feedback;
    feedback.type = 1; // 1 -> feedback message

    char message_to_log[BUF_SIZE];
    char main_cmd[STR_SIZE];

    // block all signals except SIGUSR1 and SIGUSR2
    sigaddset(&action.sa_mask, SIGUSR1);
    sigaddset(&action.sa_mask, SIGUSR2);
    sigprocmask(SIG_SETMASK, &action.sa_mask, NULL);

    // set sigset_t with SIGINT
    sigemptyset(&block_extra_set);
    sigaddset(&block_extra_set, SIGINT);

    // redirects to handler() when SIGINT is received
    sigaction(SIGINT, &action, NULL);

    // mark this worker as available
    workers_bitmap[worker_number] = 1; // 1 - available
    sprintf(message_to_log, "WORKER %d READY", worker_number + 1);
    write_to_log(message_to_log);

    // while(1)
    while (1)
    {
        // read instruction from dispatcher pipe
        if (read(from_dispatcher_pipe[0], &message_to_process, sizeof(Message)) < 1)
        {
            perror("error reading from pipe");
            exit(0);
        }
        // block SIGINT
        sigprocmask(SIG_BLOCK, &block_extra_set, NULL);

        if (message_to_process.type == 0)
        { // mensagem do user
            feedback.message_id = message_to_process.message_id;

#ifdef DEBUG
            printf("WORKER: message being processed: %s\n", message_to_process.cmd);
#endif
            sscanf(message_to_process.cmd, "%s", main_cmd);

            if (strcmp(main_cmd, "ADD_ALERT") == 0)
            {
                sscanf(message_to_process.cmd, "%s %s %s %d %d", main_cmd, alert_id, key, &min, &max);
                validated = 1;

                // lock access to alert_list
                sem_wait(sem_alert_list_writer);
                if (*count_alerts < MAX_ALERTS)
                {
                    for (i = 0; i < *count_alerts; ++i)
                    {
                        if (strcmp(alert_list[i].alert_id, alert_id) == 0)
                        { // if alert_id already exists reject it
#ifdef DEBUG
                            printf("WORKER: JA EXISTE ALERTA\n");
#endif
                            validated = 0;
                            break;
                        }
                    }
                }
                else
                {
#ifdef DEBUG
                    printf("WORKER: NUMERO MAXIMO DE ALERTAS\n");
#endif
                    validated = 0;
                }

                if (validated)
                {
                    Alert *new_alert = malloc(sizeof(Alert));
                    new_alert->alert_min = min;
                    new_alert->alert_max = max;
                    new_alert->user_console_id = message_to_process.message_id;
                    strcpy(new_alert->key, key);
                    strcpy(new_alert->alert_id, alert_id);
                    // store new alert in alert_list
                    memcpy(&alert_list[*count_alerts], new_alert, sizeof(Alert));
#ifdef DEBUG
                    printf("WORKER: NOVO ALERTA: %s\n", alert_list[*count_alerts].alert_id);
#endif
                    *count_alerts += 1;
                    sprintf(feedback.cmd, "OK");
                }
                else
                {
                    sprintf(feedback.cmd, "ERROR");
                }
                // unlock access to alert_list
                sem_post(sem_alert_list_writer);
                // send feedback to user_console
                if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                {
                    perror("error sending to message queue");
                    exit(-1);
                }
            }
            else if (strcmp(main_cmd, "REMOVE_ALERT") == 0)
            {
                sscanf(message_to_process.cmd, "%s %s", main_cmd, alert_id);
                validated = 0;
                // lock access to alert_list
                sem_wait(sem_alert_list_writer);
                for (i = 0; i < *count_alerts; ++i)
                {
                    if (strcmp(alert_list[i].alert_id, alert_id) == 0)
                    { // if alert_id exists, delete it
#ifdef DEBUG
                        printf("WORKER: ALERTA ENCONTRADO\n");
#endif
                        for (j = i + 1; j < *count_alerts; j++)
                        {
                            alert_id[j - 1] = alert_id[j];
                        }
                        validated = 1;
                        *count_alerts -= 1;
                        sprintf(feedback.cmd, "OK");
                        break;
                    }
                }
                // unlock access to alert_list
                sem_post(sem_alert_list_writer);
                if (!validated)
                {
                    sprintf(feedback.cmd, "ERROR");
                }
#ifdef DEBUG
                printf("%s\n", feedback.cmd);
#endif
                // send feedback to user_console
                if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                {
                    perror("error sending to message queue");
                    exit(-1);
                }
            }
            else if (strcmp(main_cmd, "STATS") == 0)
            {

                // if it's the first process currently reading data_base, lock writers access to it
                sem_wait(sem_data_base_reader);
                *database_readers += 1;
                if (*database_readers == 1)
                {
                    sem_wait(sem_data_base_writer);
                }
                sem_post(sem_data_base_reader);

                if (*count_key_data > 0)
                {
                    sprintf(feedback.cmd, "Key\tLast\tMin\tMax\tAvg\tCount");
                }
                else
                {
                    sprintf(feedback.cmd, "No Stats");
                }
                // send feedback to user_console
                msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0);
                for (i = 0; i < *count_key_data; ++i)
                {
                    sprintf(feedback.cmd, "%s %d %d %d %.2f %d", data_base[i].key, data_base->last_value,
                            data_base[i].min_value, data_base[i].max_value, data_base[i].average,
                            data_base[i].n_updates);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    // send stats to msg queue - one message per key
                    if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                    {
                        perror("error sending to message queue");
                        exit(-1);
                    }
                }
                // if it's the last process currently reading data_base, unlock writers access to it
                sem_wait(sem_data_base_reader);
                *database_readers -= 1;
                if (*database_readers == 0)
                {
                    sem_post(sem_data_base_writer);
                }
                sem_post(sem_data_base_reader);
            }
            else if (strcmp(main_cmd, "RESET") == 0)
            { // clean every stats/keys in the data base

                // lock access to data_base
                sem_wait(sem_alert_list_writer);
                *count_key_data = 0;
                // unlock access to data_base
                sem_post(sem_alert_list_writer);
                sprintf(feedback.cmd, "OK");
#ifdef DEBUG
                printf("%s\n", feedback.cmd);
#endif
                // send feedback to user_console
                if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                {
                    perror("error sending to message queue");
                    exit(-1);
                }
            }
            else if (strcmp(main_cmd, "LIST_ALERTS") == 0)
            {
                // if it's the first process currently reading alert_list, lock writers access to it
                sem_wait(sem_alert_list_reader);
                *alert_readers += 1;
                if (*alert_readers == 1)
                {
                    sem_wait(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);

#ifdef DEBUG
                printf("\n--LISTA DE ALERTAS--\n");
#endif

                if (*count_alerts > 0)
                {
                    sprintf(feedback.cmd, "ID\tKey\tMIN\tMAX");
                }
                else
                {
                    sprintf(feedback.cmd, "No Alerts");
                }
                // send feedback to msg queue
                if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                {
                    perror("error sending to message queue");
                    exit(-1);
                }
                for (i = 0; i < *count_alerts; ++i)
                {
                    sprintf(feedback.cmd, "%s %s %d %d", alert_list[i].alert_id, alert_list[i].key,
                            alert_list[i].alert_min, alert_list[i].alert_max);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    // send alerts to msg queue - one message per alert
                    if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                    {
                        perror("error sending to message queue");
                        exit(-1);
                    }
                }
                // if it's the last process currently reading alert_list, unlock writers access to it
                sem_wait(sem_alert_list_reader);
                *alert_readers -= 1;
                if (*alert_readers == 0)
                {
                    sem_post(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);
            }
            else if (strcmp(main_cmd, "SENSORS") == 0)
            {
                // if it's the first process currently reading sensor_list, lock writers access to it
                sem_wait(sem_sensor_list_reader);
                *sensor_readers += 1;
                if (*sensor_readers == 1)
                {
                    sem_wait(sem_sensor_list_writer);
                }
                sem_post(sem_sensor_list_reader);
#ifdef DEBUG
                printf("\n--LISTA DE SENSORES--\n");
#endif
                if (*count_sensors > 0)
                {
                    sprintf(feedback.cmd, "ID");
                }
                else
                {
                    sprintf(feedback.cmd, "No Sensors");
                }
                // send feedback to msg queue
                if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                {
                    perror("error sending to message queue");
                    exit(-1);
                }

                for (i = 0; i < *count_sensors; ++i)
                {
                    sprintf(feedback.cmd, "%s", sensor_list[i]);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    // send sensor_id to msg queue - one message per sensor
                    if (msgsnd(mq_id, &feedback, sizeof(Message) - sizeof(long), 0) == -1)
                    {
                        perror("error sending to message queue");
                        exit(-1);
                    }
                }
                // if it's the last process currently reading sensor_list, unlock writers access to it
                sem_wait(sem_sensor_list_reader);
                *sensor_readers -= 1;
                if (*sensor_readers == 0)
                {
                    sem_post(sem_sensor_list_writer);
                }
                sem_post(sem_sensor_list_reader);
            }
        }
        else
        { // mensagem sensor

            char *token;
            char copy_cmd[256];
            strcpy(copy_cmd, message_to_process.cmd);

            token = strtok(copy_cmd, "#");
            strcpy(sensor_id, token);

            token = strtok(NULL, "#");
            strcpy(key, token);

            token = strtok(NULL, "\n");
            my_atoi(token, &value);

#ifdef DEBUG
            printf("WORKER: MENSAGEM SENSOR: %s %s %d\n", sensor_id, key, value);
#endif

            validated = new_key = new_sensor = 1; // 1 - true

#ifdef DEBUG
            printf("WORKER: number of sensors: %d\n", *count_sensors);
#endif
            // lock access to data_base
            sem_wait(sem_data_base_writer);
            // lock access to sensor_list
            sem_wait(sem_sensor_list_writer);
            for (i = 0; i < *count_sensors; i++)
            {
#ifdef DEBUG
                printf("SENSOR stored/new: %s\t%s\n", sensor_list[i], sensor_id);
#endif
                if (strcmp(sensor_list[i], sensor_id) == 0)
                { // if sensor does already exists keep processing message
#ifdef DEBUG
                    printf("WORKER: SENSOR JA EXISTE\n");
#endif
                    new_sensor = 0;
                    break;
                }
            }
            if (new_sensor == 1 && *count_sensors == MAX_SENSORS)
            { // if it's a new sensor and the maximum number of sensors as been reached, reject the message
#ifdef DEBUG
                printf("WORKER: MAX SENSORES ATINGIDO\n");
#endif
                validated = 0;
            }
#ifdef DEBUG
            printf("WORKER: number of keys: %d\n", *count_key_data);
#endif
            for (i = 0; i < *count_key_data && validated; i++)
            {
#ifdef DEBUG
                printf("KEY stored/new: %s\t%s\n", data_base[i].key, key);
#endif
                if (strcmp(data_base[i].key, key) == 0)
                { // if the key does already exist -> update key
#ifdef DEBUG
                    printf("WORKER: UPDATE KEY\n");
#endif
                    new_key = 0;
                    data_base[i].last_value = value;
                    data_base[i].average = (data_base[i].average * (double)data_base[i].n_updates + (double)value) / (double)(data_base[i].n_updates + 1);
                    data_base[i].n_updates++;
                    if (value > data_base[i].max_value)
                        data_base[i].max_value = value;
                    if (value < data_base[i].min_value)
                        data_base[i].min_value = value;
                    keys_bitmap[i] = 1;
                    break;
                }
            }
            if (new_key == 1 && validated)
            {
                if (*count_key_data < MAX_KEYS)
                { // if it's a new sensor and the maxiumum number of keys hasn't been reached -> create key
#ifdef DEBUG
                    printf("WORKER: CREATE KEY\n");
#endif
                    key_data *new_key_data = malloc(sizeof(key_data));
                    strcpy(new_key_data->key, key);
                    new_key_data->last_value = new_key_data->min_value = new_key_data->max_value = value;
                    new_key_data->average = (double)value;
                    new_key_data->n_updates = 1;
                    keys_bitmap[i] = 1;
                    memcpy(&data_base[*count_key_data], new_key_data, sizeof(key_data));
                    *count_key_data += 1;
                }
                else
                {
                    validated = 0;
                }
            }
            if (new_sensor == 1 && validated)
            { // if it's a new sensor and the maxiumum number of keys hasn't been reached -> create sensor
#ifdef DEBUG
                printf("WORKER: CREATE SENSOR %d\n", *count_sensors);
#endif
                memcpy(sensor_list[*count_sensors], sensor_id, sizeof(char[STR_SIZE]));
#ifdef DEBUG
                pprintf("WORKER: NEW SENSOR %d\n", *count_sensors);
#endif
                *count_sensors += 1;
            }
            else if (!validated)
            { // if it isn't validated -> discard the message
                sprintf(message_to_log, "WORKER: Sensor message discarded: %s", message_to_process.cmd);
                write_to_log(message_to_log);
            }
            // mark key as modified
            if (keys_bitmap[i] == 1)
            { // let alert watcher work
#ifdef DEBUG
                printf("WORKER %d is unlocking alerts_watcher\n", worker_number + 1);
#endif
                // unlock alert_watcher so that it can verify the modifications and alert users
                sem_post(sem_alert_watcher);
                // waits for alert_watcher to end verification and unlock this worker
                sem_wait(sem_alert_worker);
            }

            // unlock access to sensor_list
            sem_post(sem_sensor_list_writer);
            // unlock access to data_base
            sem_post(sem_data_base_writer);
        }
        sprintf(message_to_log, "WORKER%d: '%s' PROCESSING COMPLETE", worker_number + 1, message_to_process.cmd);
        write_to_log(message_to_log);
        // mark this worker as available
        workers_bitmap[worker_number] = 1; // 1 - available
        // increment semaphore of number of workers available
        sem_post(sem_free_worker_count);
        // unblock SIGINT
        sigprocmask(SIG_UNBLOCK, &block_extra_set, NULL);
    }
    // fim while
} // worker_process

// function for alert_watcher process
void alerts_watcher_process()
{
    int j, k;
    alert_watcher_id = getpid();
    // block all signals except SIGUSR1 and SIGINT
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGUSR1);
    sigprocmask(SIG_SETMASK, &action.sa_mask, NULL);

    // set sigset_t with SIGUSR2
    sigemptyset(&block_extra_set);
    sigaddset(&block_extra_set, SIGUSR2);

    // redirects to handler() when SIGINT is received
    sigaction(SIGUSR2, &action, NULL);

    write_to_log("PROCESS ALERTS_WATCHER CREATED");
    while (1)
    {
        // waits for worker to let alert_watcher verify the modifications and alert users
        sem_wait(sem_alert_watcher);
        // block SIGUSR2
        sigprocmask(SIG_BLOCK, &block_extra_set, NULL);

        for (j = 0; j < *count_key_data; ++j)
        {

            if (keys_bitmap[j] == 1)
            {                       // this key has been modified
                keys_bitmap[j] = 0; // mark this key has verified
                // if it's the first process currently reading alert_list, lock writers access to it
                sem_wait(sem_alert_list_reader);
                *alert_readers += 1;
                if (*alert_readers == 1)
                {
                    sem_wait(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);

                for (k = 0; k < *count_alerts; ++k)
                {
#ifdef DEBUG
                    printf("Value: %d\tKeySensor: %s\tKeyAlert: %s\t Min: %d\tMax: %d\n", data_base[j].last_value, data_base[j].key, alert_list[k].key, alert_list[k].alert_min, alert_list[k].alert_max);
#endif
                    // if the last_value received is outside the alert limits (>max or <min)
                    if (strcmp(alert_list[k].key, data_base[j].key) == 0 && (data_base[j].last_value < alert_list[k].alert_min ||
                                                                             data_base[j].last_value > alert_list[k].alert_max))
                    {

                        Message msg_to_send;
                        msg_to_send.type = 0;
                        msg_to_send.message_id = alert_list[k].user_console_id;
                        sprintf(msg_to_send.cmd, "ALERT!! Alert: '%s' -> value: %d (key: '%s') !!\n",
                                alert_list[k].alert_id, data_base[j].last_value, alert_list[k].key);
                        // send alert to msg queue
                        if (msgsnd(mq_id, &msg_to_send, sizeof(Message) - sizeof(long), 0) == -1)
                        {
                            perror("error sending to message queue");
                            exit(-1);
                        }
#ifdef DEBUG
                        printf("ALERT!! Alert: '%s' -> value: %d (key: '%s') !!\n", alert_list[k].alert_id, data_base[j].last_value, alert_list[k].key);
#endif
                    }
                }
                // if it's the last process currently reading alert_list, unlock writers access to it
                sem_wait(sem_alert_list_reader);
                *alert_readers -= 1;
                if (*alert_readers == 0)
                {
                    sem_post(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);
            }
        }
        // unlock worker so that it end processing the message
        sem_post(sem_alert_worker);
        sigprocmask(SIG_UNBLOCK, &block_extra_set, NULL);
    }
} // alerts_watcher_process

// function for sensor_reader thread
void *sensor_reader()
{
    write_to_log("THREAD SENSOR_READER CREATE");
    Message sensor_message;
    char sensor_info[STR_SIZE];
    //    int sem_value;
    char message_to_log[BUF_SIZE];

    while (1)
    { // condicao dos pipes

        // read sensor info from pipe
        if (read(sensor_pipe_id, sensor_info, STR_SIZE) < 1)
        {
            perror("error reading from pipe");
            exit(-1);
        }
#ifdef DEBUG
//        printf("\n%s\n", sensor_info);
#endif

        sensor_message.type = 1; // 1 - sensor message
        sensor_message.message_id = 0;
        strcpy(sensor_message.cmd, sensor_info);

        if (sem_trywait(&internal_queue_empty_count) == -1)
        { // menos um slot vazio
            if (errno == EAGAIN)
            { // if semaphore had value 0, it means the queue is full -> discard sensor message
                sprintf(message_to_log, "READER: Sensor message discarded: %s", sensor_info);
                write_to_log(message_to_log);
                continue;
            }
            else
            {
                perror("error with internal queue synchronization");
                exit(-1);
            }
        }
        // exclusive lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        // insert sensor message into internal queue
        insert_internal_queue(internal_queue_sensor, &sensor_message);
        sem_post(&internal_queue_full_count); // mais um slot cheio

#ifdef DEBUG
        printf("sensor message to queue\n");
#endif
        // unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
    }

    pthread_exit(NULL);
} // sensor_reader

// function for console_reader thread
void *console_reader()
{
    write_to_log("THREAD CONSOLE_READER CREATED");
    Message console_message;
    //    int temporario;
    while (1)
    {
        // read Message struct from pipe
        if (read(console_pipe_id, &console_message, sizeof(Message)) < 1)
        {
            perror("error reading from pipe");
            exit(-1);
        }

#ifdef DEBUG
        printf("\nmessage received: %s\n", console_message.cmd);
#endif
        sem_wait(&internal_queue_empty_count); // menos um slot vazio
        // exclusive lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        // insert console message into internal queue
        insert_internal_queue(internal_queue_console, &console_message);
        sem_post(&internal_queue_full_count); // mais um slot cheio
#ifdef DEBUG
        printf("console message to queue\n");
#endif

        // unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
    }
    pthread_exit(NULL);
} // console_reader

// function for dispatcher thread
void *dispatcher()
{
    int k;
    char message_to_log[BUF_SIZE];
    write_to_log("THREAD DISPATCHER CREATED");

    while (1)
    {

        sem_wait(&internal_queue_full_count); // menos um slot cheio
        // waits for available workers
        sem_wait(sem_free_worker_count);
        // exclusive lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
#ifdef DEBUG
        printf("internal queue size before = %d\n", internal_queue_size);
#endif
        // get first (highest priority) message from internal queue
        Message message_to_dispatch = get_next_message(internal_queue_console, internal_queue_sensor);
        sem_post(&internal_queue_empty_count); // mais um slot vazio
#ifdef DEBUG
        printf("internal queue size after = %d\n", internal_queue_size);
#endif
        for (k = 0; k < N_WORKERS; ++k)
        {
            if (workers_bitmap[k] == 1)
            {
                workers_bitmap[k] = 0; // mark worker as busy
#ifdef DEBUG
                printf("Message dispatched: %s\t worker: %d\n", message_to_dispatch.cmd, k + 1);
#endif
                sprintf(message_to_log, "DISPATCHER: '%s' SENT FOR PROCESSING ON WORKER %d", message_to_dispatch.cmd, k+1);
                write_to_log(message_to_log);
                // send message to worker to be processed
                if (write(disp_work_pipe[k][1], &message_to_dispatch, sizeof(Message)) == -1)
                {
                    perror("write to pipe");
                    exit(-1);
                }
                break;
            }
        }
        // unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
    }
    pthread_exit(NULL);
}

// function to clean resources
void cleaner()
{

    // semaphores
    sem_close(sem_data_base_reader);
    sem_unlink("/sem_data_base_reader");
    sem_close(sem_data_base_writer);
    sem_unlink("/sem_data_base_writer");
    sem_close(sem_alert_list_reader);
    sem_unlink("/sem_alert_list_reader");
    sem_close(sem_alert_list_writer);
    sem_unlink("/sem_alert_list_writer");
    sem_close(sem_sensor_list_reader);
    sem_unlink("/sem_sensor_list_reader");
    sem_close(sem_sensor_list_writer);
    sem_unlink("/sem_sensor_list_writer");
    //    sem_close(sem_workers_bitmap); sem_unlink("/sem_workers_bitmap");
    sem_close(sem_keys_bitmap);
    sem_unlink("/sem_keys_bitmap");
    sem_close(log_mutex);
    sem_unlink("/log_mutex");
    sem_close(sem_free_worker_count);
    sem_unlink("/sem_free_worker_count");

    sem_close(sem_alert_watcher);
    sem_unlink("/sem_alert_watcher");
    sem_close(sem_alert_worker);
    sem_unlink("/sem_alert_worker");

    sem_destroy(&internal_queue_full_count);
    sem_destroy(&internal_queue_empty_count);

    // pthread_mutex
    pthread_mutex_destroy(&internal_queue_mutex);

    // shm
    shmdt(shm_global);
    shmctl(shmid, IPC_RMID, NULL);

    // msg queue
    msgctl(mq_id, IPC_RMID, NULL);

    // internal queue
    free(internal_queue_console);
    free(internal_queue_sensor);

    // named pipes
    close(sensor_pipe_id);
    unlink(SENSOR_PIPE);
    close(console_pipe_id);
    unlink(CONSOLE_PIPE);

    // unnamed pipes
    if (disp_work_pipe != NULL)
    {
        int i;
        for (i = 0; i < N_WORKERS; ++i)
        {
            if (disp_work_pipe[i] != NULL)
            {
                close(disp_work_pipe[i][0]);
                close(disp_work_pipe[i][1]);
            }
            else
                break;
        }
    }
}

// function to terminate process when an error as occured
void error_cleaner()
{
    // block every sinal
    sigfillset(&block_extra_set);
    sigprocmask(SIG_SETMASK, &block_extra_set, NULL);
#ifdef DEBUG
    printf("ERROR CLEANER\nKill threads\n");
#endif
    write_to_log("ERROR! Closing...");

    // kill and join the threads that have been created
    if (thread_count >= 1)
    {
        pthread_kill(thread_console_reader, SIGUSR1);
        pthread_join(thread_console_reader, NULL);
    }
    if (thread_count >= 2)
    {
        pthread_kill(thread_sensor_reader, SIGUSR1);
        pthread_join(thread_sensor_reader, NULL);
    }
    if (thread_count == 3)
    {
        pthread_kill(thread_dispatcher, SIGUSR1);
        pthread_join(thread_dispatcher, NULL);
    }

    int i;
    // send SIGINT to parent process group -> workers and alert_watcher
    kill(0, SIGINT);
#ifdef DEBUG
    printf("KILL %d CHILDREN\n", worker_count);
#endif
    // wait for processess already created
    for (i = 0; i < worker_count; i++)
    {
        wait(NULL);
    }
    // if alert_watcher has been created, send SIGUSR2 and wait for it
    if (alert_watcher_id != -1)
    {
        printf("KILL ALERT WATCHER\n");
        kill(alert_watcher_id, SIGUSR2);
        wait(&alert_watcher_id);
    }

#ifdef DEBUG
    printf("CLEAN ALL\n");
#endif
    // clean resources
    cleaner();
    exit(-1);
}

// function to handle signals
void handler(int signum)
{

    int i;
    char message_to_log[BUF_SIZE];
    if (signum == SIGINT && getpid() == ppid)
    { // main_process
#ifdef DEBUG
        printf("DAD: Sinal '%d' recebido\n", signum);
#endif
        write_to_log("SIGNAL SIGINT RECEIVED");
        write_to_log("HOME_IOT SIMULATOR WAITING FOR LAST TASKS TO FINISH");
        // exclusive lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        // 1. terminate threads
        pthread_kill(thread_sensor_reader, SIGUSR1);
        pthread_kill(thread_console_reader, SIGUSR1);
        pthread_kill(thread_dispatcher, SIGUSR1);

        // 2. wait for threads
        pthread_join(thread_console_reader, NULL);
        pthread_join(thread_sensor_reader, NULL);
        pthread_join(thread_dispatcher, NULL);

        // 3. store messages from internal_queue
        for (i = 0; i < internal_queue_size; ++i)
        {
            sprintf(message_to_log, "Message not processed: %s", &(get_next_message(internal_queue_console, internal_queue_sensor).cmd[0]));
            write_to_log(message_to_log);
        }

        // 4. wait for workers
        for (i = 0; i < N_WORKERS; i++)
        {
            wait(NULL);
        }

        // 5. kill and wait for alertwatcher
        kill(alert_watcher_id, SIGUSR2);
        wait(&alert_watcher_id);

        write_to_log("HOME_IOT SIMULATOR CLOSING");
        fclose(log_file);

        // 6. clean resources
        cleaner();

        exit(0);
    }
    else if (signum == SIGUSR2 && getpid() == ppid)
    { // parent when something goes wrong
#ifdef DEBUG
        printf("Parent received error\n");
#endif
        error_cleaner();
    }
    else if (signum == SIGUSR2 && getpid() == alert_watcher_id)
    { // alert watcher
#ifdef DEBUG
        printf("ALERT WATCHER CLOSING\n");
#endif
        exit(0);
    }
    else if (signum == SIGUSR1)
    { // thread
#ifdef DEBUG
        printf("THREAD CLOSING\n");
#endif
        pthread_exit(NULL);
    }
    else if (signum == SIGINT)
    { // worker
#ifdef DEBUG
        printf("WORKER: Sinal '%d' recebido\n", signum);
        printf("WORKER CLOSING\n");
#endif
        // close file descriptor used by the current worker
        close(disp_work_pipe[worker_index][0]);
        close(disp_work_pipe[worker_index][1]);
        exit(0);
    }
#ifdef DEBUG
    else
    {
        printf("LOST %d: Sinal '%d' recebido\n", getpid(), signum);
    }
#endif
}

int main(int argc, char *argv[])
{
    int i, j;
    pid_t childpid;
    internal_queue_size = 0;
    thread_count = 0;
    alert_watcher_id = -1;
    ppid = getpid();
    // verify program arguments and configuration file values
    if (argc != 2)
    {
        printf("erro\nhome_iot {configuration file}\n");
        exit(-1);
    }
    else
    {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL)
        {
            printf("Error: could not open file %s\n", argv[1]);
            exit(-1);
        }
        else
        {
            int n_args = fscanf(fp, "%s\n%s\n%s\n%s\n%s", QUEUE_SZ_str, N_WORKERS_str, MAX_KEYS_str, MAX_SENSORS_str, MAX_ALERTS_str);

            if (n_args != 5 ||
                !convert_int(QUEUE_SZ_str, &QUEUE_SZ) ||
                !convert_int(N_WORKERS_str, &N_WORKERS) ||
                !convert_int(MAX_KEYS_str, &MAX_KEYS) ||
                !convert_int(MAX_SENSORS_str, &MAX_SENSORS) ||
                !convert_int(MAX_ALERTS_str, &MAX_ALERTS))
            {
                printf("Error: data from file %s is not correct\n", argv[1]);
                exit(-1);
            }
            if (QUEUE_SZ < 1 || N_WORKERS < 1 || MAX_KEYS < 1 || MAX_SENSORS < 1 || MAX_ALERTS < 0)
            {
                printf("Error: data from file %s is not correct\n", argv[1]);
                exit(-1);
            }
        }
        fclose(fp);
    }
#ifdef DEBUG
    printf("%d\n%d\n%d\n%d\n%d\n", QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS);
#endif

    // block all signals except SIGINT, SIGUSR1 and SIGUSR2
    action.sa_flags = 0;
    sigfillset(&action.sa_mask);
    sigdelset(&action.sa_mask, SIGINT);
    sigdelset(&action.sa_mask, SIGUSR1);
    sigdelset(&action.sa_mask, SIGUSR2);
    sigprocmask(SIG_SETMASK, &action.sa_mask, NULL);

    // ignore SIGINT, SIGUSR1 and SIGUSR2 during setup
    action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    // set handler() has the handler function
    action.sa_handler = handler;

    // creates shared memory
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + (MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE])) + N_WORKERS * sizeof(int) + MAX_KEYS * sizeof(int) + 6 * sizeof(int), IPC_CREAT | 0777);
    if (shmid < 1)
    {
        perror("error getting shared memory");
        exit(-1);
    }
    // attach shm
    shm_global = (key_data *)shmat(shmid, NULL, 0);
    if ((void *)shm_global == (void *)-1)
    {
        error_cleaner();
        perror("error attaching shared memory");
        exit(-1);
    }

    data_base = (key_data *)shm_global;                                       // store data sent by sensors
    alert_list = (Alert *)((char *)data_base + MAX_KEYS * sizeof(key_data));  // store alerts
    sensor_list = (char **)((char *)alert_list + MAX_ALERTS * sizeof(Alert)); // store sensors
    for (i = 0; i < MAX_SENSORS; i++)
    {
        sensor_list[i] = (char *)((char *)sensor_list + MAX_SENSORS * sizeof(char *) + i * sizeof(char[STR_SIZE]));
    }
    workers_bitmap = (int *)((char *)sensor_list + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]));
    keys_bitmap = (int *)((char *)workers_bitmap + N_WORKERS * sizeof(int));
    count_key_data = (int *)((char *)keys_bitmap + MAX_KEYS * sizeof(int));
    count_alerts = (int *)((char *)keys_bitmap + (MAX_KEYS + 1) * sizeof(int));
    count_sensors = (int *)((char *)keys_bitmap + (MAX_KEYS + 2) * sizeof(int));
    database_readers = (int *)((char *)keys_bitmap + (MAX_KEYS + 3) * sizeof(int));
    alert_readers = (int *)((char *)keys_bitmap + (MAX_KEYS + 4) * sizeof(int));
    sensor_readers = (int *)((char *)keys_bitmap + (MAX_KEYS + 5) * sizeof(int));

    // mark all workers as busy
    for (i = 0; i < N_WORKERS; ++i)
    {
        workers_bitmap[i] = 0;
    }
    // set all keys as verified
    for (i = 0; i < MAX_KEYS; ++i)
    {
        keys_bitmap[i] = 0;
    }

    *count_key_data = 0;
    *count_alerts = 0;
    *count_sensors = 0;
    *database_readers = 0;
    *alert_readers = 0;
    *sensor_readers = 0;

    // cretes named semaphores to protect the shared memory
    sem_unlink("/sem_data_base_reader");
    if ((sem_data_base_reader = sem_open("/sem_data_base_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        error_cleaner();
        perror("named semaphore initialization");
    }
    sem_unlink("/sem_data_base_writer");
    if ((sem_data_base_writer = sem_open("/sem_data_base_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_alert_list_writer");
    if ((sem_alert_list_writer = sem_open("/sem_alert_list_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_alert_list_reader");
    if ((sem_alert_list_reader = sem_open("/sem_alert_list_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_sensor_list_reader");
    if ((sem_sensor_list_reader = sem_open("/sem_sensor_list_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_sensor_list_writer");
    if ((sem_sensor_list_writer = sem_open("/sem_sensor_list_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_keys_bitmap");
    if ((sem_keys_bitmap = sem_open("/sem_keys_bitmap", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/log_mutex");
    if ((log_mutex = sem_open("/log_mutex", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_free_worker_count");
    if ((sem_free_worker_count = sem_open("/sem_free_worker_count", O_CREAT | O_EXCL, 0777, N_WORKERS)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_alert_watcher");
    if ((sem_alert_watcher = sem_open("/sem_alert_watcher", O_CREAT | O_EXCL, 0777, 0)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    sem_unlink("/sem_alert_worker");
    if ((sem_alert_worker = sem_open("/sem_alert_worker", O_CREAT | O_EXCL, 0777, 0)) == SEM_FAILED)
    {
        perror("named semaphore initialization");
        error_cleaner();
    }
    if (sem_init(&internal_queue_full_count, 0, 0) < 0)
    {
        perror("unnamed semaphore initialization");
        error_cleaner();
    }
    if (sem_init(&internal_queue_empty_count, 0, QUEUE_SZ) < 0)
    {
        perror("unnamed semaphore initialization");
        error_cleaner();
    }

    // creates log
    char log_name[] = "log.txt";
    log_file = fopen(log_name, "a+"); // read and append
    if (log_file == NULL)
    {
        perror("could not open log file");
        error_cleaner();
    }
    else
    {
        // exclusive lock access to log_file
        sem_wait(log_mutex);
        // write messages
        get_time();
        printf("%s HOME_IOT SIMULATOR STARTING\n", temp);
        fprintf(log_file, "\n\n%s HOME_IOT SIMULATOR STARTING\n", temp);
        fflush(log_file);
        // unlock access to log_file
        sem_post(log_mutex);
    }

    // create internal queue
    internal_queue_console = create_internal_queue();
    internal_queue_sensor = create_internal_queue();

    // open/create msg queue
    if ((mq_id = msgget(MQ_KEY, IPC_CREAT | 0777)) < 0)
    {
        perror("Cannot open or create message queue\n");
        error_cleaner();
    }

    // create one unnamed pipe for each worker
    disp_work_pipe = malloc(N_WORKERS * sizeof(int *));
    for (j = 0; j < N_WORKERS; ++j)
    {
        disp_work_pipe[j] = malloc(2 * sizeof(int));
        if (pipe(disp_work_pipe[j]) == -1)
        {
            perror("Cannot create unnamed pipe\n");
            error_cleaner();
        }
    }

    // create workers
    i = 0;
    while (i < N_WORKERS)
    {
        if ((childpid = fork()) == (pid_t)0)
        {
            worker_process(i, disp_work_pipe[i]);
            exit(0);
        }
        else if (childpid == (pid_t)-1)
        {
            perror("Failed to create worker process\n");
            error_cleaner();
        }
        worker_count++;
        ++i;
    }

    // create alert_watcher
    if ((alert_watcher_id = fork()) == (pid_t)0)
    {
        alerts_watcher_process();
        exit(0);
    }
    else if (alert_watcher_id == (pid_t)-1)
    {
        perror("Failed to create alert_watcher process\n");
        error_cleaner();
    }

    // open named pipes
    unlink(CONSOLE_PIPE);
    if ((mkfifo(CONSOLE_PIPE, O_CREAT | O_EXCL | 0777) < 0) && errno != EEXIST)
    {
        perror("Cannot create console pipe.");
        error_cleaner();
    }
    if ((console_pipe_id = open(CONSOLE_PIPE, O_RDWR)) < 0)
    {
        perror("Cannot open console pipe");
        error_cleaner();
    }

    unlink(SENSOR_PIPE);
    if ((mkfifo(SENSOR_PIPE, O_CREAT | O_EXCL | 0777) < 0) && errno != EEXIST)
    {
        perror("Cannot create sensor pipe.");
        error_cleaner();
    }
    if ((sensor_pipe_id = open(SENSOR_PIPE, O_RDWR)) < 0)
    {
        perror("Cannot open sensor pipe");
        error_cleaner();
    }

    // create threads
    if (pthread_create(&thread_console_reader, NULL, console_reader, NULL) != 0)
    {
        perror("Cannot create thread.");
        error_cleaner();
    }
    thread_count++;
    if (pthread_create(&thread_sensor_reader, NULL, sensor_reader, NULL) != 0)
    {
        perror("Cannot create thread.");
        error_cleaner();
    }
    thread_count++;
    if (pthread_create(&thread_dispatcher, NULL, dispatcher, NULL) != 0)
    {
        perror("Cannot create thread.");
        error_cleaner();
    }
    thread_count++;

    // redirects to handler() when SIGINT, SIGUSR1 or SIGUSR2 is received
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);

    // wait for signal to terminate (SIGINT)
    pause();

    return 0;
}
