#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread
#include "costumio.h"
#include <time.h>

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

pthread_mutex_t shm_update_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write; pairs with shm_alert_watcher_cv
pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access

pthread_mutex_t sensors_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_sensors
pthread_mutex_t alerts_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_alerts


pthread_cond_t shm_alert_watcher_cv = PTHREAD_COND_INITIALIZER; //alert alert_watcher that the shm has been updated


int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];
int count_sensors, count_alerts;
int i;
int shmid;
FILE *log_file;


key_data *data_base;
alert *alert_list;
char *sensor_list;

time_t t;
struct tm *time_info;
char temp[10];

void get_time(){
//    printf("timer\n");
    time_info = localtime(&t);
    sprintf(temp, "%2d:%2d:%2d", time_info->tm_hour, time_info->tm_min, time_info->tm_sec);

//    printf("end timer\n");
}

void worker_process(int worker_number){

    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time(temp);
    printf("%s WORKER %d READY\n", temp, worker_number+1);
    fprintf(log_file, "%s WORKER %d READY\n",temp, worker_number+1);
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
    get_time();
    printf("%s PROCESS ALERTS_WATCHER CREATED\n", temp);
    fprintf(log_file, "%s PROCESS ALERTS_WATCHER CREATED\n", temp);
    pthread_mutex_unlock(&log_mutex);

}



void *sensor_reader(){
    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time();
    printf("%s THREAD SENSOR_READER CREATED\n", temp);
    fprintf(log_file, "%s THREAD SENSOR_READER CREATED\n", temp);
    pthread_mutex_unlock(&log_mutex);
    pthread_exit(NULL);
}

void *console_reader(){
    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time();
    printf("%s THREAD CONSOLE_READER CREATED\n", temp);
    fprintf(log_file, "%s THREAD CONSOLE_READER CREATED\n", temp);
    pthread_mutex_unlock(&log_mutex);
    pthread_exit(NULL);
}

void *dispatcher(){

    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time();
    printf("%s THREAD DISPATCHER CREATED\n", temp);
    fprintf(log_file, "%s THREAD DISPATCHER CREATED\n", temp);
    pthread_mutex_unlock(&log_mutex);
    pthread_exit(NULL);

}





int main(int argc, char *argv[]) {
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


    id_t childpid;

    //creates memory
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]), IPC_CREAT|0700);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
    data_base = (key_data*) shm_global;                                 //store data sent by sensors
    alert_list = (alert*) shm_global + MAX_KEYS * sizeof(key_data);     //store alerts
    sensor_list = (char *)shm_global + MAX_ALERTS * sizeof(alert);      //store sensors




    //creates log
    char log_name[] = "log.txt";
    log_file = fopen(log_name, "a+"); //read and append
    if (log_file == NULL)
    {
        printf("Error: could not open file %s\n", log_name);
    }
    else{
        time(&t);
        pthread_mutex_lock(&log_mutex);
        //write messages
        get_time();
        printf("%s HOME_IOT SIMULATOR STARTING\n", temp);
        fprintf(log_file, "%s HOME_IOT SIMULATOR STARTING\n", temp);
        fflush(log_file);
        pthread_mutex_unlock(&log_mutex);
    }








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

    //create threads
    pthread_t thread_console_reader, thread_sensor_reader, thread_dispatcher;
    pthread_create(&thread_console_reader, NULL, console_reader, NULL);
    pthread_create(&thread_sensor_reader, NULL, sensor_reader, NULL);
    pthread_create(&thread_dispatcher, NULL, dispatcher, NULL);




    //join threads
    pthread_join(thread_console_reader, NULL);
    pthread_join(thread_sensor_reader, NULL);
    pthread_join(thread_dispatcher, NULL);

    //wait for workers and alert_watcher
    for (i=0;i<N_WORKERS+1; i++) {
        wait(NULL);
    }

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
