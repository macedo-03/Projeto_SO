//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294
/*
 ./sensor 0011 1 TEMP11 1 10 &
 ./sensor 0012 1 TEMP12 1 10 &
 ./sensor 0013 1 TEMP13 1 10 &
 ./sensor 0014 1 TEMP14 1 10 &
 ./sensor 0015 1 TEMP15 1 10 &
 ./sensor 0016 1 TEMP16 1 10 &
 ./sensor 0017 1 TEMP17 1 10 &
 ./sensor 0018 1 TEMP18 1 10 &
 ./sensor 0019 1 TEMP19 1 10 &

 ./sensor 0021 1 TEMP21 1 10 &
 ./sensor 0022 1 TEMP22 1 10 &
 ./sensor 0023 1 TEMP23 1 10 &
 ./sensor 0024 1 TEMP24 1 10 &
 ./sensor 0025 1 TEMP25 1 10 &
 ./sensor 0026 1 TEMP26 1 10 &
 ./sensor 0027 1 TEMP27 1 10 &
 ./sensor 0028 1 TEMP28 1 10 &
 ./sensor 0029 1 TEMP29 1 10 &
*/

/*
 ./sensor 0011 1 TEMP1 1 10 &
 ./sensor 0012 1 TEMP1 1 10 &
 ./sensor 0013 1 TEMP1 1 10 &
 ./sensor 0014 1 TEMP1 1 10 &
 ./sensor 0015 1 TEMP1 1 10 &
 ./sensor 0016 1 TEMP1 1 10 &
 ./sensor 0017 1 TEMP1 1 10 &
 ./sensor 0018 1 TEMP1 1 10 &
 ./sensor 0019 1 TEMP1 1 10 &

 ./sensor 0021 1 TEMP1 1 10 &
 ./sensor 0022 1 TEMP1 1 10 &
 ./sensor 0023 1 TEMP1 1 10 &
 ./sensor 0024 1 TEMP1 1 10 &
 ./sensor 0025 1 TEMP1 1 10 &
 ./sensor 0026 1 TEMP1 1 10 &
 ./sensor 0027 1 TEMP1 1 10 &
 ./sensor 0028 1 TEMP1 1 10 &
 ./sensor 0029 1 TEMP1 1 10 &
*/


//#define DEBUG //remove this line to remove debug messages (...)

//} else if(signum == SIGTSTP){
//printf("N. Messages = %lld\n" , n_messages);
//}

#include <unistd.h> // process
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/msg.h>
#include <signal.h>

#include "costumio.h"
#define PIPE_NAME "SENSOR_PIPE"
#define BUF_SIZE 64
int pipe_id;
long long n_messages;
struct sigaction action;
//sigset_t block_extra_set;

void handler(int signum){
    if(signum == SIGTSTP){
        printf("N. Messages = %lld\n" , n_messages);
    }else if(signum == SIGINT){
        close(pipe_id);
        exit(0);
    }
}


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
    } else if(time_interval<0 || min_value>=max_value){
        printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
        exit(-1);
    }



    action.sa_flags=0;
    sigfillset(&action.sa_mask);
    sigdelset(&action.sa_mask, SIGINT);
    sigdelset(&action.sa_mask, SIGTSTP);

    sigprocmask(SIG_SETMASK, &action.sa_mask, NULL);

    action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);


    //abrir sensor para leitura
    if ((pipe_id = open(PIPE_NAME, O_WRONLY)) < 0) {
            perror("Cannot open pipe for writing!\n");
            exit(-1); 
    }
    srand(getpid());

    //define handler function to use after setup
    action.sa_handler = handler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);


    char msg[BUF_SIZE];
    while(1){
        sprintf(msg, "%s#%s#%d", argv[1], argv[3], rand() % (max_value-min_value+1) + min_value);
#ifdef DEBUG
        printf("%s\n", msg);
#endif

        if (write(pipe_id, &msg, BUF_SIZE)==-1){
            close(pipe_id);
            perror("error writing to pipe");
            exit(-1);
        }
        n_messages++;
        sleep(time_interval);
    }
    return 0;
}

