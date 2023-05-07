//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294

#define DEBUG //remove this line to remove debug messages (...)

#include <unistd.h> // process
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/msg.h>

#include "costumio.h"
#define PIPE_NAME "SENSOR_PIPE"
#define BUF_SIZE 64
int pipe_id;

int main(int argc, char *argv[]){
//        char sensor_id[], int interval, char key[],  int min, int max){
    int time_interval, min_value, max_value;

    //validacao dos argumentos
    if (argc != 6) {
        printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
        exit(-1);
    }
    if(!(convert_int(argv[2], &time_interval)    &&
                convert_int(argv[4], &min_value)      &&
                convert_int(argv[5], &max_value)     &&
                input_str(argv[1], 0)   &&   // sensor_id
                input_str(argv[3], 0)        // key
                )){
        printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
        exit(-1);
    } else if(time_interval<=0 || min_value>=max_value){
        printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
        exit(-1);
    }
    //abrir sensor para leitura
    if ((pipe_id = open(PIPE_NAME, O_WRONLY)) < 0) {
            perror("Cannot open pipe for writing!\n");
            exit(-1); 
    }
    srand(getpid());

    char msg[BUF_SIZE];
    while(1){
        sprintf(msg, "%s#%s#%d", argv[1], argv[3], rand() % (max_value-min_value+1) + min_value);
#ifdef DEBUG
        printf("%s\n", msg);
#endif
        //EPIPE
        write(pipe_id, &msg, BUF_SIZE);
        sleep(time_interval);
    }
    return 0;
}

