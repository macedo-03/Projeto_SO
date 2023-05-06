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
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <string.h>

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



pthread_mutex_t shm_update_mutex = PTHREAD_MUTEX_INITIALIZER; //protects shm access -> no read or write; pairs with shm_alert_watcher_cv
pthread_mutex_t reader_mutex = PTHREAD_MUTEX_INITIALIZER; //protects variable n_readers -> no read or write
//pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; //protects log file access

pthread_mutex_t sensors_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_sensors
pthread_mutex_t alerts_counter_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to count_alerts

pthread_mutex_t internal_queue_mutex = PTHREAD_MUTEX_INITIALIZER; //protects access to internal queue


pthread_cond_t shm_alert_watcher_cv = PTHREAD_COND_INITIALIZER; //Alert alert_watcher that the shm has been updated

pthread_t thread_console_reader, thread_sensor_reader, thread_dispatcher;

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
char QUEUE_SZ_str[MY_MAX_INPUT], N_WORKERS_str[MY_MAX_INPUT], MAX_KEYS_str[MY_MAX_INPUT], MAX_SENSORS_str[MY_MAX_INPUT], MAX_ALERTS_str[MY_MAX_INPUT];
int count_sensors, count_alerts, count_key_data;
int i, j, k; //iterators
int shmid;
int console_pipe_id, sensor_pipe_id;
int **disp_work_pipe;
int mq_id;
FILE *log_file;

sem_t *sem_data_base_reader, *sem_data_base_writer, *sem_alert_list, *sem_sensor_list_reader, *sem_sensor_list_writer, *sem_workers_bitmap, *sem_keys_bitmap, *log_mutex, *sem_free_worker_count;
sem_t internal_queue_count;

InternalQueue *internal_queue_console;
InternalQueue *internal_queue_sensor;

key_data *data_base;
Alert *alert_list;
char **sensor_list;
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
    sem_wait(log_mutex);
    //write messages
    get_time();
    printf("%s %s\n", temp, message_to_log);
    fprintf(log_file, "%s %s\n", temp, message_to_log);
    sem_post(log_mutex);
}

void worker_process(int worker_number, int from_dispatcher_pipe[2]){
    char alert_id[STR_SIZE], key[STR_SIZE], sensor_id[STR_SIZE];
//    char str_min[16], str_max[16];
    int min, max, value;
    int validated, new_sensor, new_key;

    Message feedback;
    feedback.type=1;

    char message_to_log[BUF_SIZE];
    char main_cmd[STR_SIZE];

    //while(1)
    sprintf(message_to_log, "WORKER %d READY", worker_number+1);
    write_to_log(message_to_log);



    workers_bitmap[worker_number] = 1; //1 - esta disponivel

    //read instruction from dispatcher pipe


    Message message_to_process;
    read(from_dispatcher_pipe[0], &message_to_process, sizeof(Message));
    feedback.message_id = message_to_process.message_id;


    if(message_to_process.type == 0){//mensagem do user
        sscanf(message_to_process.cmd , "%s", main_cmd);
        if(strcmp(main_cmd, "ADD_ALERT")==0){

            sscanf(message_to_process.cmd, "%s %s %d %d", alert_id, key, &min, &max);
            validated=1;
            //sincronizacao lock
            if(count_alerts < MAX_ALERTS){
                for (i = 0; i < count_alerts; ++i) {
                    if(strcmp(alert_list[i].alert_id, alert_id)==0){
                        validated=0;
                        break;
                    }
                }
            }
            else{
                validated=0;
            }

            if (validated){
                Alert *new_alert = malloc(sizeof(Alert));
                new_alert->alert_min = min;
                new_alert->alert_max = max;
                new_alert->user_console_id = message_to_process.message_id;
                strcpy(new_alert->key, key);
                strcpy(new_alert->alert_id, alert_id);

                //TODO: encontrar espaco livre
                memcpy(&alert_list[count_alerts++], new_alert, sizeof(Alert));
                sprintf(feedback.cmd, "OK");
            }
            else{
                sprintf(feedback.cmd, "ERROR");
            }
            //sincronizacao unlock

        }
        else if(strcmp(main_cmd, "REMOVE_ALERT")==0) {
            sscanf(message_to_process.cmd, "%s", alert_id);
            //bla bla bla we fucked
            sprintf(feedback.cmd, "OK");
        }
        else if(strcmp(main_cmd, "STATS")==0){
            //lock leitura
            for (i = 0; i < count_key_data; ++i) {
                sprintf(feedback.cmd, "%s %d %d %d %.2f %d", data_base[i].key, data_base->last_value, data_base[i].min_value, data_base[i].max_value, data_base[i].average, data_base[i].n_updates);
                //send feedback to msg queue
            }
        }
        else if(strcmp(main_cmd, "RESET")==0){
            //lock escrita
            //apagar tudo em data_base
            count_key_data=0; //is this enough?
            sprintf(feedback.cmd, "OK");
            //send feedback to msg queue

        }
        else if(strcmp(main_cmd, "LIST_ALERTS")==0){
            //lock leitura
            for (i = 0; i < count_alerts; ++i) {
                sprintf(feedback.cmd, "%s %s %d %d", alert_list[i].alert_id,alert_list[i].key, alert_list[i].alert_min, alert_list[i].alert_max);
                //send feedback to msg queue
            }
        }
        else if(strcmp(main_cmd, "SENSORS")==0){
            //lock leitura
            for (i = 0; i < count_sensors; ++i) {
                sprintf(feedback.cmd, "%s", sensor_list[i]);
                //send feedback to msg queue
            }
        }

    }
    else{ //mensagem sensor
        //parse input
        char *token;
        token = strtok(message_to_process.cmd, "#");
        strcpy(sensor_id, token);

        token = strtok(NULL, "#");
        strcpy(key, token);

        token = strtok(NULL, "\n");
        my_atoi(token, &value);


        validated= new_key = new_sensor = 1;

        //lock escrita
        for(i = 0; i < count_sensors; i++){
            if(strcmp(sensor_list[i], sensor_id)==0){
                new_sensor = 0;
                break;
            }
        }
        if(new_sensor == 1 && count_sensors == MAX_SENSORS){
            validated = 0;
        }

        for(i = 0; i < count_key_data && validated; i++){
            if(strcmp(data_base[i].key, key)==0){
                new_key = 0;
                data_base[i].last_value = value;
                data_base[i].average = (data_base[i].average * data_base[i].n_updates + value) / data_base[i].n_updates+1;
                data_base[i].n_updates++;
                if(value > data_base[i].max_value) data_base[i].max_value = value;
                if(value < data_base[i].min_value) data_base[i].min_value = value;
                break;
            }
        }
        if(new_key == 1 && validated){
            if(count_key_data < MAX_KEYS){
                key_data *new_key_data = malloc(sizeof(key_data));
                strcpy(new_key_data->key, key);
                new_key_data->last_value = new_key_data->min_value = new_key_data->max_value = value;
                new_key_data->average = (double) value;
                new_key_data->n_updates=1;

                memcpy(&data_base[count_key_data], new_key_data ,sizeof(key_data));
            }
        }
        if(new_sensor == 1 && validated){
            memcpy(sensor_list[count_sensors++],sensor_id, sizeof(char[STR_SIZE]));
        }
        else if(!validated){
            sprintf(message_to_log, "Sensor message discarded: %s", message_to_process.cmd);
            write_to_log(message_to_log);
        }






    }




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


    for (j = 0; j < count_key_data; ++j) {

        if(keys_bitmap[j] == 1) { //aquela key foi atualizada
            keys_bitmap[j] = 0;
            for (k = 0; k < count_alerts; ++k) {
                if (alert_list[k].key == data_base[j].key && (data_base[j].last_value < alert_list[k].alert_min || data_base[j].last_value > alert_list[k].alert_max )) {
                    Message msg_to_send;
                    msg_to_send.type = 0;
                    msg_to_send.message_id = alert_list[k].user_console_id;
                    sprintf(msg_to_send.cmd, "ALERT!! The Alert '%s', related to the key '%s' was activated!\n",
                            alert_list[k].alert_id, alert_list[k].key);
                    //TODO: send message to message queue
                }
            }
        }
    }
}

void *sensor_reader(){
    write_to_log("THREAD SENSOR_READER CREATE");
    Message sensor_message;
    char sensor_info[STR_SIZE];
    int sem_value;

    while(1){ //condicao dos pipes
        //read sensor string from pipe
        read(sensor_pipe_id, sensor_info, STR_SIZE);

        //sensor_message = malloc(sizeof(Message));
        sensor_message.type=1;
        sensor_message.message_id = 0;
        strcpy(sensor_message.cmd, sensor_info);

        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        //get_value of semaphore. if internal queue is full -> continue;
        sem_getvalue(&internal_queue_count, &sem_value);
        //semaphore
        if (sem_value < QUEUE_SZ){
            sem_post(&internal_queue_count);
            insert_internal_queue(internal_queue_sensor, &sensor_message);
        }
        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);

    }


    pthread_exit(NULL);
}

void *console_reader(){
    write_to_log("THREAD CONSOLE_READER CREATED");
    Message console_message;
    int sem_value;

    while (1){
        //read Message struct from pipe
        read(console_pipe_id, &console_message, sizeof(Message));
        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        //get_value of semaphore. if internal queue is full -> continue;
        sem_getvalue(&internal_queue_count, &sem_value);
        //semaphore
        if (sem_value < QUEUE_SZ){
            sem_post(&internal_queue_count);
            insert_internal_queue(internal_queue_console, &console_message);
        }
        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
    }
    pthread_exit(NULL);
}

void *dispatcher(){

    write_to_log("THREAD DISPATCHER CREATED");
    printf("THREAD DISPATCHER CREATED");

    while(1){
        //prende pelo semaforo dos workers
        sem_wait(sem_free_worker_count);
        //lock internal queue
        pthread_mutex_lock(&internal_queue_mutex);
        //prende pelo semaforo da internal queue
        sem_wait(&internal_queue_count);
        //executa codigo em baixo

        //unlock internal queue
        pthread_mutex_unlock(&internal_queue_mutex);
        break; //TODO: TIRAR ESTE BREAK!!
    }
    //dispatch the next message
    //get next message
    Message message_to_dispatch = get_next_message(internal_queue_console, internal_queue_sensor);
    for (i = 0; i < N_WORKERS; ++i) {
        if(workers_bitmap[i] == 1){
            workers_bitmap[i] = 0; //mark worker as busy
            write(disp_work_pipe[i][1], &message_to_dispatch, sizeof(Message));
            break;
        }
    }


    pthread_exit(NULL);

}


void cleaner(){
    pthread_mutex_destroy(&shm_update_mutex);
    pthread_mutex_destroy(&reader_mutex);
    pthread_mutex_destroy(&sensors_counter_mutex);
    pthread_mutex_destroy(&alerts_counter_mutex);

    pthread_cond_destroy(&shm_alert_watcher_cv);
    shmctl(shmid, IPC_RMID, NULL);

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
    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char[32]) + N_WORKERS * sizeof(int) + MAX_KEYS * sizeof(int), IPC_CREAT | 0777);
    if (shmid < 1) exit(0);
    void *shm_global = (key_data *) shmat(shmid, NULL, 0);
    if ((void*) shm_global == (void*) -1) exit(0);
    data_base = (key_data*) shm_global;                                 //store data sent by sensors //2semaforos
    alert_list = (Alert*) ((char*)shm_global + MAX_KEYS * sizeof(key_data));     //store alerts
    sensor_list = (char **) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert));      //store sensors //2semaforos
    workers_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char[32]));
    keys_bitmap = (int *) ((char*)shm_global + MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(Alert) + MAX_SENSORS * sizeof(char[32]) + N_WORKERS * sizeof(int));

    //cretes unamed semaphores to protect the shared memory
    if ((sem_data_base_reader = sem_open("/sem_data_base_reader", O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_data_base_writer = sem_open("/sem_data_base_writer", O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_alert_list = sem_open("/sem_alert_list", O_CREAT | O_EXCL, 0644, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_sensor_list_reader = sem_open("/sem_sensor_list_reader", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_sensor_list_writer = sem_open("/sem_sensor_list_writer", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_workers_bitmap = sem_open("/sem_workers_bitmap", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_keys_bitmap = sem_open("/sem_keys_bitmap", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((log_mutex = sem_open("/log_mutex", O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if ((sem_free_worker_count = sem_open("/sem_free_worker_count", O_CREAT | O_EXCL, 0777, N_WORKERS)) == SEM_FAILED) {
        perror("named semaphore initialization");
        exit(-1);
    }
    if (sem_init(&internal_queue_count, 0, 0) < 0) {
        perror("unnamed semaphore initialization");
        exit(-1);
    }
    
    for (i = 0; i < N_WORKERS; ++i) {
        workers_bitmap[i] = 0;
    }
    for (i = 0; i < MAX_KEYS; ++i) {
        keys_bitmap[i] = 0;
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
        time(&t);

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


    //create one unnamed pipe for each worker
    disp_work_pipe = malloc(N_WORKERS*sizeof(int*));
    for (j = 0; j < N_WORKERS; ++j) {
        disp_work_pipe[j] = malloc(2*sizeof(int));
        if(pipe(disp_work_pipe[j]) == -1){
            perror("Cannot create unnamed pipe\n");
            exit(-1);
        }

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


//TODO: geral
//signals
//semaforos:
    //mutex read/write internal queue
    //semaforo workers (para dispatcher)
    //semaforo number of messages (para dispatcher)


    //sensor reader - descartar mensagens (qnd a internal queue esta cheia (get_value)) ou quando nao cumpre todos os requesitos)

    //console reader - validacao de input
    //console reader - descartar mensagens (qnd a internal queue esta cheia (get_value)) ou quando nao cumpre todos os requesitos)


//worker processing sensors
//worker processing console

//remove alerts. HOW?

//alterar tamanho da string enviada na msg. HOW? ou mandar cada linha de stats numa mensagem diferente??

//cuidado com os iteradores globais dentro das threads

//cleanup dos semaforos
//quando o worker acabar a tarefa incrementar semaforo: sem_free_worker_count

//TODO: Miguel
//console/sensor reader -> internal queue //falta sincronizacao
//dispatcher forward messages to work //falta sincronizacao



//TODO: Zheeeee?