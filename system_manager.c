//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // process
#include <sys/shm.h> // shared memory
#include <pthread.h> // thread
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/msg.h>

#include "costumio.h"
#include "internal_queue.h"

#define CONSOLE_PIPE "CONSOLE_PIPE"
#define SENSOR_PIPE "SENSOR_PIPE"
#define MQ_KEY 4444

typedef struct
{
    char key[32];
    int last_value, min_value, max_value,n_updates ;
    double average;
} key_data;

typedef struct
{
    long user_console_id;
    char alert_id[32], key[32];
    int alert_min, alert_max;
} alert;



pthread_mutex_t shm_update_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write; pairs with shm_alert_watcher_cv
pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access

pthread_mutex_t sensors_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_sensors
pthread_mutex_t alerts_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_alerts

pthread_cond_t shm_alert_watcher_cv = PTHREAD_COND_INITIALIZER; //alert alert_watcher that the shm has been updated

pthread_t thread_console_reader, thread_sensor_reader, thread_dispatcher;

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];
int count_sensors, count_alerts, count_key_data;
int i, j;
int shmid;
int console_pipe_id, sensor_pipe_id;
int **disp_work_pipe;
int mq_id;
FILE *log_file;

InternalQueue *internal_queue_console;
InternalQueue *internal_queue_sensor;

key_data *data_base;
alert *alert_list;
char *sensor_list;
int *workers_bitmap;
int *keys_bitmap;

time_t t;
struct tm *time_info;
char temp[10];

void get_time(){
//    printf("timer\n");
    time_info = localtime(&t);
    sprintf(temp, "%2d:%2d:%2d", time_info->tm_hour, time_info->tm_min, time_info->tm_sec);

//    printf("end timer\n");
}

void write_to_log(char *message_to_log){
    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time();
    printf("%s %s\n", temp, message_to_log);
    fprintf(log_file, "%s %s\n", temp, message_to_log);
    pthread_mutex_unlock(&log_mutex);
}

void worker_process(int worker_number, int from_dispatcher_pipe[2]){

    pthread_mutex_lock(&log_mutex);
    //write messages
    get_time(temp);
    printf("%s WORKER %d READY\n", temp, worker_number+1);
    fprintf(log_file, "%s WORKER %d READY\n",temp, worker_number+1);
    pthread_mutex_unlock(&log_mutex);

    workers_bitmap[worker_number] = 1; //1 - esta disponivel

    //read instruction from dispatcher pipe
    Message message_to_process;
    read(from_dispatcher_pipe[1], &message_to_process, sizeof(Message));

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


    write_to_log("PROCESS ALERTS_WATCHER CREATED");


    for (int j = 0; j < count_key_data; ++j) {

        if(keys_bitmap[j] == 1) { //aquela key foi atualizada
            keys_bitmap[j] = 0;
            for (int k = 0; k < count_alerts; ++k) {
                if (alert_list[k].key == data_base[j].key && (data_base[j].last_value < alert_list[k].alert_min || data_base[j].last_value > alert_list[k].alert_max )) {
                    Message msg_to_send;
                    msg_to_send.type = 0;
                    msg_to_send.message_id = alert_list[k].user_console_id;
                    sprintf(msg_to_send.cmd, "ALERT!! The alert '%s', related to the key '%s' was activated!\n",
                            alert_list[k].alert_id, alert_list[k].key);
                    //TODO: send message to message queue
                }
            }
        }
    }
}

void *sensor_reader(){
    write_to_log("THREAD SENSOR_READER CREATE");

    char message_from_sensor[STR_SIZE];
    read(sensor_pipe_id, &message_from_sensor, STR_SIZE);


    pthread_exit(NULL);
}

void *console_reader(){
    write_to_log("THREAD CONSOLE_READER CREATED");


    pthread_exit(NULL);
}

void *dispatcher(){

    write_to_log("THREAD DISPATCHER CREATED");


    //dispatch the next message
    //get next message
    Message message_to_dispatch = get_next_message(internal_queue_console, internal_queue_sensor);
    //TODO: verificar se deu return da mensagem (caso esta funcao n fique a espera de nada)

    pthread_exit(NULL);

}


void cleaner(){
    pthread_mutex_destroy(&shm_update_mutex);
    pthread_mutex_destroy(&reader_mutex);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&sensors_counter_mutex);
    pthread_mutex_destroy(&alerts_counter_mutex);

    pthread_cond_destroy(&shm_alert_watcher_cv);
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
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]) + N_WORKERS * sizeof(int) + MAX_KEYS * sizeof(int), IPC_CREAT|0777);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
    data_base = (key_data*) shm_global;                                 //store data sent by sensors
    alert_list = (alert*) ((char*)shm_global + MAX_KEYS * sizeof(key_data));     //store alerts
    sensor_list = (char *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert));      //store sensors
    workers_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]));
    keys_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(char[32]) + N_WORKERS * sizeof(int));

    //codigo para testar shm
    /*
    key_data *teste_data_base = malloc(sizeof(key_data));
    alert *teste_alert_list = malloc(sizeof(alert));
    char *teste_sensor_list = malloc(sizeof(char[32]));
    int *teste_workers_bitmap = malloc(sizeof(int));

    int abc = 3;
    teste_workers_bitmap = &abc;

    memcpy(data_base + 1, teste_data_base, sizeof(key_data));
    memcpy(alert_list, teste_alert_list, sizeof(alert));
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
        time(&t);

//        write_to_log("HOME_IOT SIMULATOR STARTING");
        pthread_mutex_lock(&log_mutex);
        //write messages
        get_time();
        printf("%s HOME_IOT SIMULATOR STARTING\n", temp);
        fprintf(log_file, "\n\n%s HOME_IOT SIMULATOR STARTING\n", temp);
        fflush(log_file);
        pthread_mutex_unlock(&log_mutex);
    }


    //create internal queue
    internal_queue_console = create_internal_queue();
    internal_queue_sensor = create_internal_queue();


    //open/create msg queue
    if((mq_id = msgget(MQ_KEY, IPC_CREAT | 0777)) <0){
        perror("Cannot open or create message queue");
        exit(-1);
    }


    disp_work_pipe = malloc(N_WORKERS*sizeof(int*));
    for (j = 0; j < N_WORKERS; ++j) {
        disp_work_pipe[j] = malloc(2*sizeof(int));
        pipe(disp_work_pipe[j]);
        //TODO: protecao
    }


    //create unnamed pipes and send it to workers
    //creates WORKERS
    i=0;
    while (i < N_WORKERS){
        if ((childpid = fork()) == 0)
        {
            //worker_process(i);
//            worker_process(i, disp_work_pipe[i]);
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
