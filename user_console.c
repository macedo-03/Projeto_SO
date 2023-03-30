#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]){


    char console_id = argv[0];

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
    //verificacao de input
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
        //verificacao de inputs
    }
    }
}
