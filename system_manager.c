#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread
#include "costumio.h"

pthread_mutex_t shm_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write
pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access

typedef struct
{
    int last_value, min_value, max_value, average;
    double n_updates;
} key_data;

typedef struct
{
    char alert_id[32], key[32];
    int alert_min, alert_max;
} alert;



int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];
int internal_queue_size, keys, sensors, alerts;
int i, n_readers=0;
int shmid;
char sensors_id[0][32]; //TODO n de sensors = MAX_SENSORS
FILE *log_file;


key_data *data_base;
alert *alert_list;
char *sensor_list;


void worker_process(int worker_number){

    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("WORKER %d READY", worker_number);
    fprintf(log_file, "WORKER %d READY", worker_number);
    pthread_mutex_unlock(&log_mutex);


    //user console messages -> stats; sensors; list_alerts
        //when read-only process tries to access shared memory
//        pthread_mutex_lock(&reader_mutex);
//        n_readers++;
//        if(n_readers==1) pthread_mutex_lock(&shm_mutex);
//        pthread_mutex_unlock(&reader_mutex);
//
//        // >> read from shm
//
//        pthread_mutex_lock(&reader_mutex);
//        n_readers--;
//        if(n_readers==0) pthread_mutex_unlock(&shm_mutex);
//        pthread_mutex_unlock(&reader_mutex);


    //user console messages -> reset; add_alert; remove_alert
        //when write process tries to access shared memory
//        pthread_mutex_lock(&shm_mutex);
//        // >> write to shm
//        pthread_mutex_unlock(&shm_mutex);

    //LogFile -> write messages to log and screen
//    pthread_mutex_lock(&log_mutex);
//    //write messages
//    printf("Insert message here");
//    fprintf(log_file, "Insert message here");
//    pthread_mutex_unlock(&log_mutex);



}

void alerts_watcher_process(){

    //when read-only process tries to access shared memory
//        pthread_mutex_lock(&reader_mutex);
//        n_readers++;
//        if(n_readers==1) pthread_mutex_lock(&shm_mutex);
//        pthread_mutex_unlock(&reader_mutex);
//
//        // >> read from shm
//
//        pthread_mutex_lock(&reader_mutex);
//        n_readers--;
//        if(n_readers==0) pthread_mutex_unlock(&shm_mutex);
//        pthread_mutex_unlock(&reader_mutex);

    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("PROCESS ALERTS_WATCHER CREATED");
    fprintf(log_file, "PROCESS ALERTS_WATCHER CREATED");
    pthread_mutex_unlock(&log_mutex);

}

void *sensor_reader(void *id){
    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("THREAD SENSOR_READER CREATED");
    fprintf(log_file, "THREAD SENSOR_READER CREATED");
    pthread_mutex_unlock(&log_mutex);

}

void *console_reader(void *id){
    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("THREAD CONSOLE_READER CREATED");
    fprintf(log_file, "THREAD CONSOLE_READER CREATED");
    pthread_mutex_unlock(&log_mutex);
}

void *dispatcher(void *id){

    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("THREAD DISPATCHER CREATED");
    fprintf(log_file, "THREAD DISPATCHER CREATED");
    pthread_mutex_unlock(&log_mutex);

}


//void system_manager(char config_file[]){
//
//
//
//}



int main(int argc, char *argv[]) {

    if (argc != 1) {
        printf("home_iot {configuration file}\n");
        exit(-1);
    }
    else{
        FILE *fp = fopen(argv[0], "r");
        if (fp == NULL)
        {
            printf("Error: could not open file %s", argv[0]);
            exit(-1);
        }
        else{
            int n_args = fscanf(fp, "%s\n%s\n%s\n%s\n%s", QUEUE_SZ_str,N_WORKERS_str, MAX_KEYS_str, MAX_SENSORS_str, MAX_ALERTS_str);
            if(fscanf(fp, "%s\n%s\n%s\n%s\n%s", QUEUE_SZ_str,N_WORKERS_str, MAX_KEYS_str, MAX_SENSORS_str, MAX_ALERTS_str)!=5){
                printf("Error: data from file %s is not correct", argv[0]);

            }
            if(n_args!=5 ||
                !convert_int(QUEUE_SZ_str, &QUEUE_SZ) ||
                !convert_int(N_WORKERS_str, &N_WORKERS) ||
                !convert_int(MAX_KEYS_str, &MAX_KEYS) ||
                !convert_int(MAX_SENSORS_str, &MAX_SENSORS) ||
                !convert_int(MAX_ALERTS_str, &MAX_ALERTS)){
                printf("Error: data from file %s is not correct", argv[0]);
                exit(-1);
            }
            if(QUEUE_SZ<1 ||N_WORKERS<1 || MAX_KEYS<1 || MAX_SENSORS<1 || MAX_ALERTS<0){
                printf("Error: data from file %s is not correct", argv[0]);
                exit(-1);
            }
        }
        fclose(fp);
    }


    id_t childpid;

    //creates memory
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]), IPC_CREAT|0700);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
    data_base = (key_data*) shm_global;
    alert_list = (alert*) shm_global + MAX_KEYS * sizeof(key_data);
    sensor_list = (char *)shm_global + MAX_ALERTS * sizeof(alert);




    //creates log
    char log_name[] = "log.txt";
    log_file = fopen(log_name, "a+"); //read and append
    if (log_file == NULL)
    {
        printf("Error: could not open file %s", log_name);
    }
    else{
        printf("Error: data from file %s is not correct", log_name);
    }

    pthread_mutex_lock(&log_mutex);
    //write messages
    printf("HOME_IOT SIMULATOR STARTING");
    fprintf(log_file, "HOME_IOT SIMULATOR STARTING");
    pthread_mutex_unlock(&log_mutex);




    //creates WORKERS
    while (i < N_WORKERS){
        if ((childpid = fork()) == 0)
        {
            worker_process(i);
            exit(0);
        }
        else if (childpid == -1)
        {
            perror("Failed to create worker process");
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
        perror("Failed to create alert_watcher process");
        exit(1);
    }

    //creates threads
    pthread_t thread; long id=0;
    pthread_create(&thread, NULL, console_reader, (void*) &id); id++;
    pthread_create(&thread, NULL, sensor_reader, (void*) &id); id++;
    pthread_create(&thread, NULL, dispatcher, (void*) &id);

//    //LogFile - system_manager, workers, alert_watcher
//    pthread_mutex_lock(&log_mutex);
//    //write messages
//    pthread_mutex_unlock(&log_mutex);
//
//
//
//    //Shared memory - workers and alert_watcher
//
//        //when read-only process tries to access shared memory
//            pthread_mutex_lock(&reader_mutex);
//            n_readers++;
//            if(n_readers==1) pthread_mutex_lock(&shm_mutex);
//            pthread_mutex_unlock(&reader_mutex);
//
//            // >> read from shm
//
//            pthread_mutex_lock(&reader_mutex);
//            n_readers--;
//            if(n_readers==0) pthread_mutex_unlock(&shm_mutex);
//            pthread_mutex_unlock(&reader_mutex);
//
//
//        //when write process tries to access shared memory
//            pthread_mutex_lock(&shm_mutex);
//            // >> write to shm
//            pthread_mutex_unlock(&shm_mutex);


    return 0;
}
