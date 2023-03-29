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

typedef struct
{
    char sensor_id[32];
} sensor;

int QUEUE_SZ, N_WORKERS, MAX_KEYS, MAX_SENSORS, MAX_ALERTS;
int i;
int shm_alert, shm_sensor, shm_data;
char sensors_id[0][32]; //TODO n de sensors = MAX_SENSORS
key_data *data_base;
alert *alert_list;
sensor *sensor_list;



void sensor_process(char sensor_id[], int interval, char key[], int min, int max){
    srand(getpid());
    while(1){
        printf("%s#%s#%d", sensor_id, key, rand() % (max-min) + min);
        sleep(interval);
    }

}


void user_console_process(char console_id[]){
    char cmd[64], id[32], key[32];
    char str_min[16], str_max[16];
    char *ptr;
    int min, max;

    printf("Menu:\n"
           "- exit\n"
           "- stats\n"
           "- reset\n"
           "- sensors\n"
           "- add_alert [id] [chave] [min] [max]\n"
           "- remove_alert[id]\n"
           "- list_alerts\n\n");
    scanf("%s", cmd);

    while (strcmp(cmd, "exit\n")!=0){

        if(strcmp(cmd, "add_alert")==0){
            scanf("%s %s %s %s", id, key, str_min, str_max);
            min= (int) strtol(str_min, &ptr, 10);
            max= (int) strtol(str_min, &ptr, 10);
        }
        else if(strcmp(cmd, "remove_alert")==0){
            scanf("%s", id);
        }
        else if(strcmp(cmd, "stats")==0){
            printf("%s\n", cmd);
        }
        else if(strcmp(cmd, "reset")==0){
            printf("%s\n", cmd);
        }
        else if(strcmp(cmd, "list_alerts")==0){
            printf("%s\n", cmd);
        }
        scanf("%s", cmd);
    }
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
        fscanf(fp, "%d\n%d\n%d\n%d\n%d\n", &QUEUE_SZ,&N_WORKERS, &MAX_KEYS, &MAX_SENSORS, &MAX_ALERTS);
        if(QUEUE_SZ<1 ||N_WORKERS<1 || MAX_KEYS<1 || MAX_SENSORS<1 || MAX_ALERTS<0)
            printf("Error: data from file %s is not correct", config_file);
    }
    fclose(fp);


//    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data) + MAX_ALERTS * sizeof(alert) + MAX_SENSORS * sizeof(sensor), IPC_CREAT|0700);
//    shmid = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data), IPC_CREAT|0700);
//    if (shmid < 1) exit(0);
//    data_base = (key_data *) shmat(shmid, NULL, 0);
//    if ((void*) data_base == (void*) -1) exit(0);

    shm_data = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(key_data), IPC_CREAT|0700);
    if (shm_data < 1) exit(0);
    data_base = (key_data *) shmat(shm_data, NULL, 0);
    if ((void*) data_base == (void*) -1) exit(0);

    shm_alert = shmget(IPC_PRIVATE, MAX_KEYS * sizeof(alert), IPC_CREAT|0700);
    if (shm_alert < 1) exit(0);
    alert_list = (alert *) shmat(shm_alert, NULL, 0);
    if ((void*) alert_list == (void*) -1) exit(0);

    shm_sensor = shmget(IPC_PRIVATE, MAX_SENSORS * sizeof(sensor), IPC_CREAT|0700);
    if (shm_sensor < 1) exit(0);
    sensor_list = (sensor *) shmat(shm_sensor, NULL, 0);
    if ((void*) sensor_list == (void*) -1) exit(0);


    while (i < N_WORKERS){
        if ((childpid = fork()) == 0)
        {
            //worker process
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



int main() {


    return 0;
}
