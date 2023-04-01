#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "costumio.h"


int main(int argc, char *argv[]){

    if (argc != 1) {
        printf("user_console {console id}\n");
        exit(-1);
    }
    else{
        if(!input_str(argv[0], 0)){ // console id
            printf("user_console {console id}\n");
            exit(-1);
        }
    }


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

//    do{
//        scanf("%s", cmd);
//    } while (!input_str(cmd, 1));

    scanf("%s", cmd);
    if(!input_str(cmd, 1)){
        printf("Erro de formatacao do comando\n");
        exit(-1);
    }


    while (strcmp(cmd, "exit\n")!=0){
        if(strcmp(cmd, "add_alert")==0){
            scanf("%s %s %s %s", id, key, str_min, str_max);

            if(!(convert_int(str_min, &min) &&
                    convert_int(str_max, &max) &&
                    input_str(id, 0) &&
                    input_str(key, 0))){
                printf("Erro de formatacao dos argumentos\n");
                exit(-1);
            }
        }
        else if(strcmp(cmd, "remove_alert")==0){
            scanf("%s", id);
            if(!input_str(id, 0)){
                printf("Erro de formatacao do argumento\n");
                exit(-1);
            }
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

//        do{
//            scanf("%s", cmd);
//        } while (!input_str(cmd, 1));

        scanf("%s", cmd);
        if(!input_str(cmd, 1)){
            printf("Erro de formatacao do comando\n");
            exit(-1);
        }
    }


}

