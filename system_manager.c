#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread


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

//typedef struct
//{
//    char sensor_id[32];
//} sensor;

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
int internal_queue_size, workers, keys, sensors, alerts;
int i, n_readers=0;
int shmid;
char sensors_id[0][32]; //TODO n de sensors = MAX_SENSORS

//int shm_alert, shm_sensor, shm_data;
key_data *data_base;
alert *alert_list;
char *sensor_list;


void worker_process(){
//    char* const argv[] = { "/usr/bin/xterm", "-e", "/bin/bash", NULL };
//    execv("/usr/bin/xterm", argv);
}

void alerts_watcher_process(){

}

void *sensor_reader(void *id){

}

void *console_reader(void *id){

}

void *dispatcher(void *id){

}


void system_manager(char config_file[]){
    pid_t childpid;

    FILE *fp = fopen(config_file, "r");
    if (fp == NULL)
    {
        printf("Error: could not open file %s", config_file);
    }
    else{
        fscanf(fp, "%d\n%d\n%d\n%d\n%d", &QUEUE_SZ,&N_WORKERS, &MAX_KEYS, &MAX_SENSORS, &MAX_ALERTS);
        if(QUEUE_SZ<1 ||N_WORKERS<1 || MAX_KEYS<1 || MAX_SENSORS<1 || MAX_ALERTS<0)
            printf("Error: data from file %s is not correct", config_file);
    }
    fclose(fp);


    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]), IPC_CREAT|0700);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
    data_base = (key_data*) shm_global;
    alert_list = (alert*) shm_global + MAX_KEYS * sizeof(key_data);
    sensor_list = (char *)shm_global + MAX_ALERTS * sizeof(alert);


//    shm_data = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data), IPC_CREAT|0700);
//    if (shm_data < 1) exit(0);
//    data_base = (key_data *) shmat(shm_data, NULL, 0);
//    if ((void*) data_base == (void*) -1) exit(0);
//
//    shm_alert = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(alert), IPC_CREAT|0700);
//    if (shm_alert < 1) exit(0);
//    alert_list = (alert *) shmat(shm_alert, NULL, 0);
//    if ((void*) alert_list == (void*) -1) exit(0);
//
//    shm_sensor = shmget(IPC_PRIVATE, MAX_SENSORS * sizeof(sensor), IPC_CREAT|0700);
//    if (shm_sensor < 1) exit(0);
//    sensor_list = (sensor *) shmat(shm_sensor, NULL, 0);
//    if ((void*) sensor_list == (void*) -1) exit(0);


    while (i < N_WORKERS){
        if ((childpid = fork()) == 0)
        {
            workers++;
            //execv();//worker process
            exit(0);
        }
        else if (childpid == -1)
        {
            perror("Failed to create worker process");
            exit(1);
        }
        ++i;
    }

    if ((childpid = fork()) == 0){
        //alerts watcher
        exit(0);
    }
    else if (childpid == -1)
    {
        perror("Failed to create alert_watcher process");
        exit(1);
    }

    pthread_t thread; long id=0;
    pthread_create(&thread, NULL, console_reader, (void*) &id); id++;
    pthread_create(&thread, NULL, sensor_reader, (void*) &id); id++;
    pthread_create(&thread, NULL, dispatcher, (void*) &id);


}


pthread_mutex_t shm_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write
pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access

int main() {

    char log_name[] = "log.txt";
    FILE *log_file = fopen(log_name, "a+"); //read and append
    if (log_file == NULL)
    {
        printf("Error: could not open file %s", log_name);
    }
    else{
        fscanf(log_file, "%d\n%d\n%d\n%d\n%d", &QUEUE_SZ,&N_WORKERS, &MAX_KEYS, &MAX_SENSORS, &MAX_ALERTS);
        //verificacao de inputs
        if(QUEUE_SZ<1 ||N_WORKERS<1 || MAX_KEYS<1 || MAX_SENSORS<1 || MAX_ALERTS<0)
            printf("Error: data from file %s is not correct", log_name);
    }
    fclose(log_file);


    //LogFile - system_manager, workers, alert_watcher
    pthread_mutex_lock(&log_mutex);
    //write messages
    pthread_mutex_unlock(&log_mutex);



    //Shared memory - workers and alert_watcher

    //when read-only process tries to access shared memory
    pthread_mutex_lock(&reader_mutex);
    n_readers++;
    if(n_readers==1) pthread_mutex_lock(&shm_mutex);
    pthread_mutex_unlock(&reader_mutex);

    // >> read from shm

    pthread_mutex_lock(&reader_mutex);
    n_readers--;
    if(n_readers==0) pthread_mutex_unlock(&shm_mutex);
    pthread_mutex_unlock(&reader_mutex);


    //when write process tries to access shared memory
    pthread_mutex_lock(&shm_mutex);
    // >> write to shm
    pthread_mutex_unlock(&shm_mutex);


    return 0;
}
