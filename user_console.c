//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "costumio.h"


int main(int argc, char *argv[]){
    if (argc != 2) {
        printf("user_console {console id}\n");
        exit(-1);
    }
    else if(!input_str(argv[1], 0)){ // console id

        printf("user_console {console id}\n");
        exit(-1);

    }


    char cmd[64], id[32], key[32];
    char str_min[16], str_max[16];
    int min, max;

    printf("Menu:\n"
           "- exit\n"
           "- stats\n"
           "- reset\n"
           "- sensors\n"
           "- add_alert [id] [chave] [min] [max]\n"
           "- remove_alert [id]\n"
           "- list_alerts\n\n");


    scanf("%s", cmd);
    if(!input_str(cmd, 1)){
        printf("Erro de formatacao do comando\n");
        exit(-1);
    }


    while (strcmp(cmd, "EXIT")!=0){

        if(strcmp(cmd, "ADD_ALERT")==0){
            scanf("%s %s %s %s", id, key, str_min, str_max);

            if(!(convert_int(str_min, &min) &&
                    convert_int(str_max, &max) &&
                    input_str(id, 0) &&
                    input_str(key, 0))){
                printf("Erro de formatacao dos argumentos\n");
                exit(-1);
            } else if(max<=min){
                printf("Valor maximo tem de ser maior que o minimo (max > min)\n");
                exit(-1);
            }
            printf("add_alert\n");
        }
        else if(strcmp(cmd, "REMOVE_ALERT")==0){
            scanf("%s", id);
            if(!input_str(id, 0)){
                printf("Erro de formatacao do argumento\n");
                exit(-1);
            }
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
            exit(-1);
        }


        scanf("%s", cmd);
        if(!input_str(cmd, 1)){
            printf("Erro de formatacao do comando\n");
            exit(-1);
        }
    }

    return 0;
}

