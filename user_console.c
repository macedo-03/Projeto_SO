//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>
#include "costumio.h"

#define CONSOLE_PIPE "CONSOLE_PIPE"
#define MQ_KEY 4444
#define BUF_SIZE 1024

typedef struct
{
    long message_id;
    int type; // 0: pedido user, 1: dados sensor, 2: alerta
    char cmd[BUF_SIZE];
} message;


int pipe_id, mq_id, id, valido = 1;
pthread_t mq_reader;
message msg;
struct sigaction action;

void handler(){
    exit(0);
}

void *read_msq(){
    while (1) {
        msgrcv(mq_id, &msg, sizeof(message) - sizeof(long), id, 0);
        printf("%s", msg.cmd);
    }
    pthread_exit(NULL);
    return NULL;
}

int main(int argc, char *argv[]){
    if (argc != 2) {
        printf("user_console {console id}\n");
        exit(-1);
    }
    else if(!input_str(argv[1], 0)){ // console id

        printf("user_console {console id}\n");
        exit(-1);
    }


    char buf[BUF_SIZE], cmd[64], alert_id[32], key[32];
    char str_min[16], str_max[16];
    int min, max;

    //abrir pipe para escrita
    if ((pipe_id = open(CONSOLE_PIPE, O_WRONLY)) < 0) {
                perror("Cannot open pipe for writing!\n");
                exit(-1); 
    }

    //abrir a message queue
    if ((mq_id = msgget(MQ_KEY, 0777)) < 0) {
                perror("Cannot open message queue!\n");
                exit(-1); 
    }

    id = getpid();
    pthread_create(&mq_reader, NULL, read_msq, NULL);

    printf("Menu:\n"
           "- exit\n"
           "- stats\n"
           "- reset\n"
           "- sensors\n"
           "- add_alert [id] [chave] [min] [max]\n"
           "- remove_alert [id]\n"
           "- list_alerts\n\n");


    fgets(buf, BUF_SIZE, stdin);
    sscanf(buf, "%s", cmd);
    if(!input_str(cmd, 1)){
        printf("Erro de formatacao do comando\n");
        exit(-1);
    }


    while (strcmp(cmd, "EXIT")!=0){
        valido = 1;
        if(strcmp(cmd, "ADD_ALERT")==0){
            sscanf(buf, "%s %s %s %s", alert_id, key, str_min, str_max);

            if(!(convert_int(str_min, &min) &&
                    convert_int(str_max, &max) &&
                    input_str(alert_id, 0) &&
                    input_str(key, 0))){
                printf("Erro de formatacao dos argumentos\n");
                valido = 0;
                //exit(-1);
            } else if(max<=min){
                printf("Valor maximo tem de ser maior que o minimo (max > min)\n");
                valido = 0;
                //exit(-1);
            }
            //sprintf(buf, "%s %s %s %d %d", cmd, alert_id, key, min, max);
            printf("add_alert\n");
        }
        else if(strcmp(cmd, "REMOVE_ALERT")==0){
            sscanf(buf, "%s", alert_id);
            if(!input_str(alert_id, 0)){
                printf("Erro de formatacao do argumento\n");
                valido = 0;
                //exit(-1);
            }
            //sprintf(buf, "%s %s", cmd, alert_id);
            printf("remove_alert\n");
        }
        else if(strcmp(cmd, "STATS")==0){
            printf("stats\n");
        }
        else if(strcmp(cmd, "RESET")==0){
            printf("reset\n");
        }
        else if(strcmp(cmd, "LIST_ALERTS")==0){
            printf("list_alerts\n");
        }
        else if(strcmp(cmd, "SENSORS")==0){
            printf("sensors\n");
        }
        else{
            printf("Comando nao reconhecido\n");
            valido = 0;
            //exit(-1);
        }

        if (valido) {
            message m;
            m.message_id = id;
            m.type = 0;
            strcpy(m.cmd, buf);
            write(pipe_id, &m, sizeof(message));
        }

        fgets(buf, BUF_SIZE, stdin);
        sscanf(buf, "%s", cmd);
        if(!input_str(cmd, 1)){
            printf("Erro de formatacao do Comando\n");
            exit(-1);
        }
    }

    return 0;
}

