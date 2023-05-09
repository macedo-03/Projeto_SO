//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294


// ./home_iot config_file.txt
// ./user_console 123
// ./sensor 2342 7 TEMP10 1 10

// add_alert alert1 TEMP1 3 6

//#define DEBUG //remove this line to remove debug messages (...)


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
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
    int last_value, min_value, max_value,n_updates ;
    double average;
} key_data;

typedef struct
{
    long user_console_id;
    char alert_id[STR_SIZE], key[STR_SIZE];
    int alert_min, alert_max;
} Alert;



//pthread_mutex_t shm_update_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write; pairs with shm_alert_watcher_cv
//pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
////pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access
//
//pthread_mutex_t sensors_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_sensors
//pthread_mutex_t alerts_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_alerts

pthread_mutex_t internal_queue_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to internal queue

pthread_mutex_t int_queue_size_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable internal_queue_size -> no read or write
pthread_cond_t new_message_cv = PTHREAD_COND_INITIALIZER; //Alert alert_watcher that the shm has been updated

pthread_t thread_console_reader, thread_sensor_reader, thread_dispatcher;

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];


int shmid;
int console_pipe_id, sensor_pipe_id;
int **disp_work_pipe;
int mq_id;
FILE *log_file;

sem_t *sem_data_base_reader, *sem_data_base_writer, *sem_alert_list_reader, *sem_alert_list_writer, *sem_sensor_list_reader, *sem_sensor_list_writer, *sem_workers_bitmap, *sem_keys_bitmap, *log_mutex, *sem_free_worker_count, *sem_alert_watcher, *sem_alert_worker;
sem_t internal_queue_full_count, internal_queue_empty_count;

InternalQueue *internal_queue_console;
InternalQueue *internal_queue_sensor;

key_data *data_base;
Alert *alert_list;
char **sensor_list;
int *workers_bitmap;
int *keys_bitmap;
int *count_sensors, *count_alerts, *count_key_data;
int *sensor_readers, *alert_readers, *database_readers;


time_t t;
struct tm *time_info;
char temp[10];

void get_time(){
//    printf("timer\n");
    time(&t);
    time_info = localtime(&t);
    sprintf(temp, "%2d:%2d:%2d", time_info->tm_hour, time_info->tm_min, time_info->tm_sec);

//    printf("end timer\n");
}

void write_to_log(char *message_to_log){
    sem_wait(log_mutex);
    //write messages
    get_time();
    printf("%s %s\n", temp, message_to_log);
    fprintf(log_file, "%s %s\n", temp, message_to_log);
    sem_post(log_mutex);
}

void worker_process(int worker_number, int from_dispatcher_pipe[2]){
    int i, j;
    char alert_id[STR_SIZE], key[STR_SIZE], sensor_id[STR_SIZE];
//    char str_min[16], str_max[16];
    int min, max, value;
    int validated, new_sensor, new_key;

    Message message_to_process;
    Message feedback;
    feedback.type=1;

    char message_to_log[BUF_SIZE];
    char main_cmd[STR_SIZE];


    workers_bitmap[worker_number] = 1; //1 - esta disponivel
    sprintf(message_to_log, "WORKER %d READY", worker_number+1);
    write_to_log(message_to_log);


    //while(1)
    while (1) {

        //read instruction from dispatcher pipe
        read(from_dispatcher_pipe[0], &message_to_process, sizeof(Message));

        if (message_to_process.type == 0) {//mensagem do user
            feedback.message_id = message_to_process.message_id;
#ifdef DEBUG
            printf("WORKER: message being processed: %s\n", message_to_process.cmd);
#endif
            sscanf(message_to_process.cmd, "%s", main_cmd);

            if (strcmp(main_cmd, "ADD_ALERT") == 0) {
                sscanf(message_to_process.cmd, "%s %s %s %d %d", main_cmd, alert_id, key, &min, &max);
                validated = 1;
                //sincronizacao lock
                sem_wait(sem_alert_list_writer);
                if (*count_alerts < MAX_ALERTS) {
                    for (i = 0; i < *count_alerts; ++i) {
                        if (strcmp(alert_list[i].alert_id, alert_id) == 0) {
#ifdef DEBUG
                            printf("WORKER: JA EXISTE ALERTA\n");
#endif
                            validated = 0;
                            break;
                        }
                    }
                } else {
#ifdef DEBUG
                    printf("WORKER: NUMERO MAXIMO DE ALERTAS\n");
#endif
                    validated = 0;
                }

                if (validated) {
                    Alert *new_alert = malloc(sizeof(Alert));
                    new_alert->alert_min = min;
                    new_alert->alert_max = max;
                    new_alert->user_console_id = message_to_process.message_id;
                    strcpy(new_alert->key, key);
                    strcpy(new_alert->alert_id, alert_id);


                    memcpy(&alert_list[*count_alerts], new_alert, sizeof(Alert));
#ifdef DEBUG
                    printf("WORKER: NOVO ALERTA: %s\n", alert_list[*count_alerts].alert_id);
#endif
                    *count_alerts+=1;
                    sprintf(feedback.cmd, "OK");
                } else {
                    sprintf(feedback.cmd, "ERROR");
                }
                sem_post(sem_alert_list_writer);
                if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                    perror("error sending to message queue");
                    exit(-1);
                }

            } else if (strcmp(main_cmd, "REMOVE_ALERT") == 0) {
                sscanf(message_to_process.cmd, "%s %s", main_cmd, alert_id);
                validated = 0;
                sem_wait(sem_alert_list_writer);
                for (i = 0; i < *count_alerts; ++i) {
                    if (strcmp(alert_list[i].alert_id, alert_id) == 0) {
#ifdef DEBUG
                        printf("WORKER: ALERTA ENCONTRADO\n");
#endif
                        for (j = i + 1; j < *count_alerts; j++) {
                            alert_id[j - 1] = alert_id[j];
                        }
                        validated = 1;
                        *count_alerts -= 1;
                        sprintf(feedback.cmd, "OK");
                        break;
                    }
                }
                sem_post(sem_alert_list_writer);
                if (!validated) {
                    sprintf(feedback.cmd, "ERROR");
                }


#ifdef DEBUG
                printf("%s\n", feedback.cmd);
#endif
                if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                    perror("error sending to message queue");
                    exit(-1);
                }
            } else if (strcmp(main_cmd, "STATS") == 0) {

                //lock leitura
                sem_wait(sem_data_base_reader);
                *database_readers+=1;
                if(*database_readers == 1){
                    sem_wait(sem_data_base_writer);
                }
                sem_post(sem_data_base_reader);


                if(*count_key_data>0){
                    sprintf(feedback.cmd, "Key\tLast\tMin\tMax\tAvg\tCount");
                }
                else{
                    sprintf(feedback.cmd, "No Stats");
                }
                msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0);
                for (i = 0; i < *count_key_data; ++i) {
                    sprintf(feedback.cmd, "%s %d %d %d %.2f %d", data_base[i].key, data_base->last_value,
                            data_base[i].min_value, data_base[i].max_value, data_base[i].average,
                            data_base[i].n_updates);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                        perror("error sending to message queue");
                        exit(-1);
                    }
                    //TODO: return do send
                    //send feedback to msg queue
                }
                sem_wait(sem_data_base_reader);
                *database_readers-=1;
                if(*database_readers == 0){
                    sem_post(sem_data_base_writer);
                }
                sem_post(sem_data_base_reader);

            } else if (strcmp(main_cmd, "RESET") == 0) {

                //lock escrita
                //clean every stats in the data base
                sem_wait(sem_alert_list_writer);
                *count_key_data = 0;
                sem_post(sem_alert_list_writer);
                sprintf(feedback.cmd, "OK");
#ifdef DEBUG
                printf("%s\n", feedback.cmd);
#endif
                if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                    perror("error sending to message queue");
                    exit(-1);
                }
                //send feedback to msg queue


            } else if (strcmp(main_cmd, "LIST_ALERTS") == 0) {

                sem_wait(sem_alert_list_reader);
                *alert_readers+=1;
                if(*alert_readers == 1){
                    sem_wait(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);

#ifdef DEBUG
                printf("\n--LISTA DE ALERTAS--\n");
#endif

                if(*count_alerts>0){
                    sprintf(feedback.cmd, "ID\tKey\tMIN\tMAX");
                }
                else{
                    sprintf(feedback.cmd, "No Alerts");
                }
                if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                    perror("error sending to message queue");
                    exit(-1);
                }
                for (i = 0; i < *count_alerts; ++i) {
                    sprintf(feedback.cmd, "%s %s %d %d", alert_list[i].alert_id, alert_list[i].key,
                            alert_list[i].alert_min, alert_list[i].alert_max);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                        perror("error sending to message queue");
                        exit(-1);
                    }
                    //send feedback to msg queue
                }

                sem_wait(sem_alert_list_reader);
                *alert_readers-=1;
                if(*alert_readers == 0){
                    sem_post(sem_alert_list_writer);
                }
                sem_post(sem_alert_list_reader);

            } else if (strcmp(main_cmd, "SENSORS") == 0) {

                sem_wait(sem_sensor_list_reader);
                *sensor_readers+=1;
                if(*sensor_readers == 1){
                    sem_wait(sem_sensor_list_writer);
                }
                sem_post(sem_sensor_list_reader);
#ifdef DEBUG
                printf("\n--LISTA DE SENSORES--\n");
#endif
                if(*count_sensors>0){
                    sprintf(feedback.cmd, "ID");
                }
                else{
                    sprintf(feedback.cmd, "No Sensors");
                }
                if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                    perror("error sending to message queue");
                    exit(-1);
                }

                for (i = 0; i < *count_sensors; ++i) {
                    sprintf(feedback.cmd, "%s", sensor_list[i]);
#ifdef DEBUG
                    printf("%s\n", feedback.cmd);
#endif
                    if(msgsnd(mq_id, &feedback, sizeof(Message)-sizeof(long), 0)==-1){
                        perror("error sending to message queue");
                        exit(-1);
                    }
                    //send feedback to msg queue
                }
                sem_wait(sem_sensor_list_reader);
                *sensor_readers-=1;
                if(*sensor_readers == 0){
                    sem_post(sem_sensor_list_writer);
                }
                sem_post(sem_sensor_list_reader);
            }
//            else if(strcmp(main_cmd, "EXIT") == 0){
//
//                sem_wait(sem_alert_list_writer);
//                for (i = 0; i < *count_alerts; ++i) {
//                    if (alert_list[i].user_console_id == message_to_process.message_id) {
//#ifdef DEBUG
//                        printf("WORKER: ALERTA ENCONTRADO\n");
//#endif
//                        for (j = i + 1; j < *count_alerts; j++) {
//                            alert_id[j - 1] = alert_id[j];
//                        }
//                        *count_alerts -= 1;
//                    }
//                }
//                sem_post(sem_alert_list_writer);
//
//            }


        } else { //mensagem sensor

            //parse input
            char *token;
            token = strtok(message_to_process.cmd, "#");
            strcpy(sensor_id, token);

            token = strtok(NULL, "#");
            strcpy(key, token);

            token = strtok(NULL, "\n");
            my_atoi(token, &value);

#ifdef DEBUG
            printf("WORKER: MENSAGEM SENSOR: %s %s %d\n", sensor_id, key, value);
#endif

            validated = new_key = new_sensor = 1; //1 - true

            //lock escrita

#ifdef DEBUG
            printf("WORKER: number of sensors: %d\n", *count_sensors);
#endif
            sem_wait(sem_data_base_writer);
//            printf("WORKER %d passou wait sem_data_base_writer\n", worker_number+1);
            sem_wait(sem_sensor_list_writer);
//            printf("WORKER %d passou wait sem_sensor_list_writer\n", worker_number+1);
//            printf("numero de sensores: %d\n", *count_sensors);
            for (i = 0; i < *count_sensors; i++) {
#ifdef DEBUG
                printf("SENSOR stored/new: %s\t%s\n",sensor_list[i], sensor_id);
#endif
                if (strcmp(sensor_list[i], sensor_id) == 0) {
#ifdef DEBUG
                    printf("WORKER: SENSOR JA EXISTE\n");
#endif
                    new_sensor = 0;
                    break;
                }
            }
            if (new_sensor == 1 && *count_sensors == MAX_SENSORS) {
#ifdef DEBUG
                printf("WORKER: MAX SENSORES ATINGIDO\n");
#endif
                validated = 0;
            }
#ifdef DEBUG
            printf("WORKER: number of keys: %d\n", *count_key_data);
#endif
            for (i = 0; i < *count_key_data && validated; i++) {
#ifdef DEBUG
                printf("KEY stored/new: %s\t%s\n",data_base[i].key, key );
#endif
                if (strcmp(data_base[i].key, key) == 0) { //update key
#ifdef DEBUG
                    printf("WORKER: UPDATE KEY\n");
#endif
                    new_key = 0;
                    data_base[i].last_value = value;
                    data_base[i].average = (data_base[i].average * (double)data_base[i].n_updates + (double) value) / (double)(data_base[i].n_updates + 1);
                    data_base[i].n_updates++;
                    if (value > data_base[i].max_value) data_base[i].max_value = value;
                    if (value < data_base[i].min_value) data_base[i].min_value = value;
                    keys_bitmap[i] = 1;
                    break;
                }
            }
            if (new_key == 1 && validated) {
                if (*count_key_data < MAX_KEYS) {//create key
#ifdef DEBUG
                    printf("WORKER: CREATE KEY\n");
#endif
                    key_data *new_key_data = malloc(sizeof(key_data));
                    strcpy(new_key_data->key, key);
                    new_key_data->last_value = new_key_data->min_value = new_key_data->max_value = value;
                    new_key_data->average = (double) value;
                    new_key_data->n_updates = 1;
                    keys_bitmap[i] = 1;
                    memcpy(&data_base[*count_key_data], new_key_data, sizeof(key_data));
                    *count_key_data += 1;
                }
                else{
                    validated = 0;
                }
            }
            if (new_sensor == 1 && validated) {
#ifdef DEBUG
                printf("WORKER: CREATE SENSOR %d\n", *count_sensors);
#endif
                memcpy(sensor_list[*count_sensors], sensor_id, sizeof(char[STR_SIZE]));
                printf("WORKER: NEW SENSOR %d\n",  *count_sensors);
                *count_sensors+=1;
            } else if (!validated) {
                sprintf(message_to_log, "WORKER: Sensor message discarded: %s", message_to_process.cmd);
                write_to_log(message_to_log);
            }

            if(keys_bitmap[i] == 1){ //let alert watcher work
#ifdef DEBUG
                printf("WORKER %d is unlocking alerts_watcher\n", worker_number+1);
#endif
                sem_post(sem_alert_watcher);
//                printf("WORKER %d passou post sem_alert_watcher\n", worker_number+1);
                sem_wait(sem_alert_worker);
//                printf("WORKER %d passou wait sem_alert_worker\n", worker_number+1);
            }



            sem_post(sem_sensor_list_writer);
//            printf("WORKER %d passou post sem_sensor_list_writer\n", worker_number+1);
            sem_post(sem_data_base_writer);
//            printf("WORKER %d passou post sem_data_base_writer\n", worker_number+1);

        }

        sprintf(message_to_log, "WORKER %d READY", worker_number+1);
        write_to_log(message_to_log);
        //TODO: signals
        workers_bitmap[worker_number] = 1; //1 - esta disponivel
        sem_post(sem_free_worker_count);

//        if(feedback.type == 100)break; //TODO: TIRAR ISTO
    }
    //fim while

} //worker_process

void alerts_watcher_process(){
    int j, k;
    write_to_log("PROCESS ALERTS_WATCHER CREATED");
    while (1) {
        sem_wait(sem_alert_watcher);
//        printf("ALERTWATCHER passou wait sem_alert_watcher\n");
        for (j = 0; j < *count_key_data; ++j) {

            if (keys_bitmap[j] == 1) { //aquela key foi atualizada
                keys_bitmap[j] = 0;

                sem_wait(sem_alert_list_reader);
//                printf("ALERTWATCHER passou wait sem_alert_list_reader (1.1)\n");
                *alert_readers += 1;
                if (*alert_readers == 1) {
                    sem_wait(sem_alert_list_writer);
//                    printf("ALERTWATCHER passou wait sem_alert_list_writer\n");
                }
                sem_post(sem_alert_list_reader);
//                printf("ALERTWATCHER passou post sem_alert_list_reader (1.2)\n");
                for (k = 0; k < *count_alerts; ++k) {
#ifdef DEBUG
                    printf("Value: %d\tKeySensor: %s\tKeyAlert: %s\t Min: %d\tMax: %d\n",data_base[j].last_value, data_base[j].key,  alert_list[k].key, alert_list[k].alert_min, alert_list[k].alert_max);
#endif
                    if (strcmp(alert_list[k].key, data_base[j].key) == 0 && (data_base[j].last_value < alert_list[k].alert_min ||
                                                                  data_base[j].last_value > alert_list[k].alert_max)) {

                        Message msg_to_send;
                        msg_to_send.type = 0;
                        msg_to_send.message_id = alert_list[k].user_console_id;
                        sprintf(msg_to_send.cmd, "ALERT!! Alert: '%s' -> value: %d (key: '%s') !!\n",
                                alert_list[k].alert_id,data_base[j].last_value, alert_list[k].key);
                        if (msgsnd(mq_id, &msg_to_send, sizeof(Message) - sizeof(long), 0) == -1) {
                            perror("error sending to message queue");
                            exit(-1);
                        }
#ifdef DEBUG
                        printf("ALERT!! Alert: '%s' -> value: %d (key: '%s') !!\n", alert_list[k].alert_id,data_base[j].last_value, alert_list[k].key);
#endif

                    }
                }
                sem_wait(sem_alert_list_reader);
//                printf("ALERTWATCHER passou wait sem_alert_list_reader (1.2)\n");
                *alert_readers -= 1;
                if (*alert_readers == 0) {
                    sem_post(sem_alert_list_writer);
//                    printf("ALERTWATCHER passou post sem_alert_list_writer\n");
                }
                sem_post(sem_alert_list_reader);
//                printf("ALERTWATCHER passou post sem_alert_list_reader (2.2)\n");
            }

        }
        sem_post(sem_alert_worker);
//        printf("ALERTWATCHER passou post sem_alert_worker\n");
    }
} //alerts_watcher_process

void *sensor_reader(){
    write_to_log("THREAD SENSOR_READER CREATE");
    Message sensor_message;
    char sensor_info[STR_SIZE];
    int sem_value;
    char message_to_log[BUF_SIZE];

    while(1){ //condicao dos pipes
        //read sensor string from pipe

        if (read(sensor_pipe_id, sensor_info, STR_SIZE)==-1){
            perror("error reading from pipe");
            exit(-1);
        }
//#ifdef DEBUG
//        printf("\n%s\n", sensor_info);
//#endif
        //sensor_message = malloc(sizeof(Message));
        sensor_message.type=1;
        sensor_message.message_id = 0;
        strcpy(sensor_message.cmd, sensor_info);



        if(sem_trywait(&internal_queue_empty_count)==-1){ // menos um vazio
            if(errno == EAGAIN){
                sprintf(message_to_log, "READER: Sensor message discarded: %s", sensor_info);
                write_to_log(message_to_log);
                continue;
            }
            else{
                perror("error with internal queue synchronization");
                exit(-1);
            }
        }

        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);

        insert_internal_queue(internal_queue_sensor, &sensor_message);
        sem_post(&internal_queue_full_count); // mais um cheio
//        pthread_cond_signal(&new_message_cv);

#ifdef DEBUG
            printf("sensor message to queue\n");
#endif

        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
    }

    pthread_exit(NULL);
} // sensor_reader

void *console_reader(){
    write_to_log("THREAD CONSOLE_READER CREATED");
    Message console_message;
    int temporario;
    while (1){
        //read Message struct from pipe
        if (read(console_pipe_id, &console_message, sizeof(Message))==-1){
            perror("error reading from pipe");
            exit(-1);
        }

#ifdef DEBUG
        printf("\nmessage received: %s\n", console_message.cmd);
#endif

        sem_wait(&internal_queue_empty_count); // menos um vazio
//        sem_getvalue(&internal_queue_empty_count, &temporario);
//        printf("Console EMPTY: %d\n", temporario);
        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
//        printf("console mutex lock\n");
        insert_internal_queue(internal_queue_console, &console_message);

        sem_post(&internal_queue_full_count); // mais um cheio
//        sem_getvalue(&internal_queue_full_count, &temporario);
//        printf("console FULL: %d\n", temporario);
//            pthread_mutex_unlock(&int_queue_size_mutex);
#ifdef DEBUG
            printf("console message to queue\n");
#endif

        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
//        printf("console mutex unlock\n");
    }
    pthread_exit(NULL);
} //console_reader

void *dispatcher(){
    int k;
    int temporario;
    write_to_log("THREAD DISPATCHER CREATED");


    while(1){
//        pthread_mutex_lock(&int_queue_size_mutex);
//        while(internal_queue_size==0){
//            pthread_cond_wait(&new_message_cv, &int_queue_size_mutex);
//        }
//        pthread_mutex_unlock(&int_queue_size_mutex);

        sem_wait(&internal_queue_full_count); // menos um cheio
//        sem_getvalue(&internal_queue_full_count, &temporario);
//        printf("dispatcher FULL: %d\n", temporario);


//        sem_getvalue(sem_free_worker_count, &temporario);
//        printf("dispatcher FREE_WORKERS: %d\n", temporario);
        //prende pelo semaforo dos workers
        sem_wait(sem_free_worker_count);


        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
//        printf("dispatcher mutex lock\n");
//        //prende pelo semaforo da internal queue
//        sem_wait(&internal_queue_full_count);
        //executa codigo em baixo
#ifdef DEBUG
        printf("internal queue size before = %d\n", internal_queue_size);
#endif
        Message message_to_dispatch = get_next_message(internal_queue_console, internal_queue_sensor);
        sem_post(&internal_queue_empty_count); // // mais um vazio
//        sem_getvalue(&internal_queue_empty_count, &temporario);
//        printf("dispatcher EMPTY: %d\n", temporario);
#ifdef DEBUG
        printf("internal queue size after = %d\n", internal_queue_size);
#endif
        for (k = 0; k < N_WORKERS; ++k) {
            if(workers_bitmap[k] == 1){
                workers_bitmap[k] = 0; //mark worker as busy
//#ifdef DEBUG
                printf("Message dispatched: %s\t worker: %d\n", message_to_dispatch.cmd, k+1);
//#endif
                write(disp_work_pipe[k][1], &message_to_dispatch, sizeof(Message));


                break;
            }
        }


        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
//        printf("dispatcher mutex unlock\n");
    }
    //dispatch the next message
    //get next message



    pthread_exit(NULL);

}


void cleaner(){
//    pthread_mutex_destroy(&shm_update_mutex);
//    pthread_mutex_destroy(&reader_mutex);
//    pthread_mutex_destroy(&sensors_counter_mutex);
//    pthread_mutex_destroy(&alerts_counter_mutex);

    sem_close(sem_data_base_reader); sem_unlink("/sem_data_base_reader");
    sem_close(sem_data_base_writer); sem_unlink("/sem_data_base_writer");
    sem_close(sem_alert_list_reader); sem_unlink("/sem_alert_list_reader");
    sem_close(sem_alert_list_writer); sem_unlink("/sem_alert_list_writer");
    sem_close(sem_sensor_list_reader); sem_unlink("/sem_sensor_list_reader");
    sem_close(sem_sensor_list_writer); sem_unlink("/sem_sensor_list_writer");
    sem_close(sem_workers_bitmap); sem_unlink("/sem_workers_bitmap");
    sem_close(sem_keys_bitmap); sem_unlink("/sem_keys_bitmap");
    sem_close(log_mutex); sem_unlink("/log_mutex");
    sem_close(sem_free_worker_count); sem_unlink("/sem_free_worker_count");

    sem_destroy(&internal_queue_full_count); //TODO: mantemos isto? deprecated

//    pthread_cond_destroy(&shm_alert_watcher_cv);
    shmctl(shmid, IPC_RMID, NULL);

    //TODO: PIPES

}


void handle_worker(int signum){
    exit(0);
}
void handle_threads(int signum){
    if(signum == SIGUSR1)
        pthread_exit(NULL);
}
void handle_alert_watcher(int signum){}
void handle_main_process(int signum){
    //convem blpquear sinais dentro dos handles 
    pthread_kill(thread_sensor_reader, SIGUSR1);
    pthread_kill(thread_console_reader, SIGUSR1);
    pthread_kill(thread_dispatcher, SIGUSR1);
}



int main(int argc, char *argv[]) {
    int i, j;
    internal_queue_size = 0;

    if (argc != 2) {
        printf("erro\nhome_iot {configuration file}\n");
        exit(-1);
    }
    else{
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL)
        {
            printf("Error: could not open file %s\n", argv[1]);
            exit(-1);
        }
        else{
            int n_args = fscanf(fp, "%s\n%s\n%s\n%s\n%s", QUEUE_SZ_str,N_WORKERS_str, MAX_KEYS_str, MAX_SENSORS_str, MAX_ALERTS_str);

            if(n_args!=5 ||
                !convert_int(QUEUE_SZ_str, &QUEUE_SZ) ||
                !convert_int(N_WORKERS_str, &N_WORKERS) ||
                !convert_int(MAX_KEYS_str, &MAX_KEYS) ||
                !convert_int(MAX_SENSORS_str, &MAX_SENSORS) ||
                !convert_int(MAX_ALERTS_str, &MAX_ALERTS)){
                printf("Error: data from file %s is not correct\n", argv[1]);
                exit(-1);
            }
            if(QUEUE_SZ<1 ||N_WORKERS<1 || MAX_KEYS<1 || MAX_SENSORS<1 || MAX_ALERTS<0){
                printf("Error: data from file %s is not correct\n", argv[1]);
                exit(-1);
            }
        }
        fclose(fp);
    }
//    printf("%d\n%d\n%d\n%d\n%d\n", QUEUE_SZ,N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS);


    id_t childpid;

    //creates memory
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + ( MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]) ) + N_WORKERS * sizeof(int) + MAX_KEYS * sizeof(int) + 3 * sizeof(int), IPC_CREAT | 0777);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
//    data_base = (key_data*) shm_global;                                 //store data sent by sensors //2semaforos
//    alert_list = (Alert*) ((char*)shm_global + MAX_KEYS * sizeof(key_data));     //store alerts
//    sensor_list = (char **) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert));      //store sensors //2semaforos
//    for(i =0; i<MAX_SENSORS;i++){
//        sensor_list[i] = (char *) (sensor_list + MAX_SENSORS * sizeof(char *) + i * sizeof(char[STR_SIZE]));
//    }
//    workers_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]));
//    keys_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]) + N_WORKERS * sizeof(int));
//    count_key_data = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]) + (N_WORKERS + MAX_KEYS)* sizeof(int));
//    count_alerts = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]) + (N_WORKERS + MAX_KEYS + 1)* sizeof(int));
//    count_sensors = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]) + (N_WORKERS + MAX_KEYS + 2)* sizeof(int));




    data_base = (key_data*) shm_global;                                 //store data sent by sensors //2semaforos
    alert_list = (Alert*) ((char*)data_base + MAX_KEYS * sizeof(key_data));     //store alerts
    sensor_list = (char **) ((char*)alert_list + MAX_ALERTS * sizeof(Alert));      //store sensors //2semaforos
    for(i =0; i<MAX_SENSORS;i++){
        sensor_list[i] = (char *) ((char *)sensor_list + MAX_SENSORS * sizeof(char *) + i * sizeof(char[STR_SIZE]));
    }
    workers_bitmap = (int *) ((char*)sensor_list +  MAX_SENSORS * sizeof(char *) + MAX_SENSORS * sizeof(char[STR_SIZE]));
    keys_bitmap = (int *) ((char*)workers_bitmap +  N_WORKERS * sizeof(int));
    count_key_data = (int *) ((char*)keys_bitmap +   MAX_KEYS * sizeof(int));
    count_alerts = (int *) ((char*)keys_bitmap  + (MAX_KEYS + 1) * sizeof(int));
    count_sensors = (int *) ((char*)keys_bitmap + (MAX_KEYS + 2) * sizeof(int));
    database_readers = (int *) ((char*)keys_bitmap + (MAX_KEYS + 3) * sizeof(int));
    alert_readers  = (int *) ((char*)keys_bitmap + (MAX_KEYS + 4) * sizeof(int));
    sensor_readers = (int *) ((char*)keys_bitmap + (MAX_KEYS + 5) * sizeof(int));



    for (i = 0; i < N_WORKERS; ++i) {
        workers_bitmap[i] = 0;
    }
    for (i = 0; i < MAX_KEYS; ++i) {
        keys_bitmap[i] = 0;
    }

    *count_key_data = 0;
    *count_alerts = 0;
    *count_sensors = 0;
    *database_readers = 0;
    *alert_readers = 0;
    *sensor_readers = 0;


    //cretes named semaphores to protect the shared memory
    sem_unlink("/sem_data_base_reader");
    if ((sem_data_base_reader = sem_open("/sem_data_base_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_data_base_writer");
    if ((sem_data_base_writer = sem_open("/sem_data_base_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_alert_list_writer");
    if ((sem_alert_list_writer = sem_open("/sem_alert_list_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_alert_list_reader");
    if ((sem_alert_list_reader = sem_open("/sem_alert_list_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_sensor_list_reader");
    if ((sem_sensor_list_reader = sem_open("/sem_sensor_list_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_sensor_list_writer");
    if ((sem_sensor_list_writer = sem_open("/sem_sensor_list_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_workers_bitmap");
    if ((sem_workers_bitmap = sem_open("/sem_workers_bitmap", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_keys_bitmap");
    if ((sem_keys_bitmap = sem_open("/sem_keys_bitmap", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/log_mutex");
    if ((log_mutex = sem_open("/log_mutex", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_free_worker_count");
    if ((sem_free_worker_count = sem_open("/sem_free_worker_count", O_CREAT | O_EXCL, 0777, N_WORKERS)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_alert_watcher");
    if ((sem_alert_watcher = sem_open("/sem_alert_watcher", O_CREAT | O_EXCL, 0777, 0)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    sem_unlink("/sem_alert_worker");
    if ((sem_alert_worker = sem_open("/sem_alert_worker", O_CREAT | O_EXCL, 0777, 0)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if (sem_init(&internal_queue_full_count, 0, 0) < 0) {
        perror("unnamed semaphore initialization");
        exit(-1);
    }
    if (sem_init(&internal_queue_empty_count, 0, QUEUE_SZ) < 0) {
        perror("unnamed semaphore initialization");
        exit(-1);
    }
    


    //codigo para testar shm
    /*
    key_data *teste_data_base = malloc(sizeof(key_data));
    Alert *teste_alert_list = malloc(sizeof(Alert));
    char *teste_sensor_list = malloc(sizeof(char[32]));
    int *teste_workers_bitmap = malloc(sizeof(int));

    int abc = 3;
    teste_workers_bitmap = &abc;

    memcpy(data_base + 1, teste_data_base, sizeof(key_data));
    memcpy(alert_list, teste_alert_list, sizeof(Alert));
    memcpy(sensor_list, teste_sensor_list, sizeof(char[32]));
    memcpy(&workers_bitmap[2], teste_workers_bitmap, sizeof(int));

    printf("\n\nNUMERO NA SHM %d\n\n" , workers_bitmap[2]);
    */

    //creates log
    char log_name[] = "log.txt";
    log_file = fopen(log_name, "a+"); //read and append
    if (log_file == NULL)
    {
        printf("Error: could not open file %s\n", log_name);
    }
    else{


//        write_to_log("HOME_IOT SIMULATOR STARTING");
        sem_wait(log_mutex);
        //write messages
        get_time();
        printf("%s HOME_IOT SIMULATOR STARTING\n", temp);
        fprintf(log_file, "\n\n%s HOME_IOT SIMULATOR STARTING\n", temp);
        fflush(log_file);
        sem_post(log_mutex);
    }


    //create internal queue
    internal_queue_console = create_internal_queue();
    internal_queue_sensor = create_internal_queue();


    //open/create msg queue
    if((mq_id = msgget(MQ_KEY, IPC_CREAT | 0777)) <0){
        perror("Cannot open or create message queue\n");
        exit(-1);
    }

    //TODO: tirar isto qnd tiver handle
    msgctl(mq_id, IPC_RMID, NULL);
    if((mq_id = msgget(MQ_KEY, IPC_CREAT | 0777)) <0){
        perror("Cannot open or create message queue\n");
        exit(-1);
    }
    //


    //create one unnamed pipe for each worker
    disp_work_pipe = malloc(N_WORKERS*sizeof(int*));
    for (j = 0; j < N_WORKERS; ++j) {
        disp_work_pipe[j] = malloc(2*sizeof(int));
        if(pipe(disp_work_pipe[j]) == -1){
            perror("Cannot create unnamed pipe\n");
            exit(-1);
        }
    }


    //create unnamed pipes and send it to workers
    //creates WORKERS
    i=0;
    while (i < N_WORKERS){
        if ((childpid = fork()) == 0)
        {
            worker_process(i, disp_work_pipe[i]);
            exit(0);
        }
        else if (childpid == -1)
        {
            perror("Failed to create worker process\n");
            exit(1);
        }
        ++i;
    }

    //creates ALERT WATCHER
    if ((childpid = fork()) == 0){
        alerts_watcher_process();
        exit(0);
    }
    else if (childpid == -1)
    {
        perror("Failed to create alert_watcher process\n");
        exit(1);
    }


    //opens named pipes
    unlink(CONSOLE_PIPE);
    if((mkfifo(CONSOLE_PIPE, O_CREAT | O_EXCL |0777)<0) && errno!=EEXIST){
        perror("Cannot create pipe.");
        exit(-1);
    }
    if((console_pipe_id = open(CONSOLE_PIPE, O_RDWR)) <0){
        perror("Cannot open pipe");
        exit(-1);
    }

    unlink(SENSOR_PIPE);
    if((mkfifo(SENSOR_PIPE, O_CREAT | O_EXCL |0777)<0) && errno!=EEXIST){
        perror("Cannot create pipe.");
        exit(-1);
    }
    if((sensor_pipe_id = open(SENSOR_PIPE, O_RDWR)) <0){
        perror("Cannot open pipe");
        exit(-1);
    }



    //create threads
    pthread_create(&thread_console_reader, NULL, console_reader, NULL);
    pthread_create(&thread_sensor_reader, NULL, sensor_reader, NULL);
    pthread_create(&thread_dispatcher, NULL, dispatcher, NULL);


    write_to_log("HOME_IOT SIMULATOR WAITING FOR LAST TASKS TO FINISH");


    //join threads
    pthread_join(thread_console_reader, NULL);
    pthread_join(thread_sensor_reader, NULL);
    pthread_join(thread_dispatcher, NULL);

    //wait for workers and alert_watcher
    for (i=0;i<N_WORKERS+1; i++) {
        wait(NULL);
    }

    shmdt(shm_global);
    shmctl(shmid, IPC_RMID, NULL);

    write_to_log("HOME_IOT SIMULATOR CLOSING");
    fclose(log_file);

    cleaner();
    return 0;
}


//TODO: geral
//signals
//semaforos:
    //DONE - mutex read/write internal queue
    //DONE - semaforo workers (para dispatcher)
    //DONE - semaforo number of messages (para dispatcher)


    //QUASE DONE - sensor reader - descartar mensagens (qnd a internal queue esta cheia (get_value))

    //console reader - validacao de input
    //QUASE DONE - console reader - descartar mensagens (qnd a internal queue esta cheia (get_value))


//remove alerts when console exits

//REMOVI OS WARNINGS - alterar tamanho da string enviada na msg. HOW? ou mandar cada linha de stats numa mensagem diferente??
//cuidado com os iteradores globais dentro das threads
//DONE - quando o worker acabar a tarefa incrementar semaforo: sem_free_worker_count

//ALERT WATCHER
//SIGNALS


//TODO: Miguel
//console/sensor reader -> internal queue DONE
//dispatcher forward messages to work DONE
//DONE - cleanup dos semaforos

//trocar unnamed semaphore para variavel de condicao DONE
//trocar iteradores - DONE

//sincronizacao DONE //falta alert_watcher




//TODO: Zheeeee?
//SIGNALS and handles