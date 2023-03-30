#include <unistd.h> // process
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){
//        char sensor_id[], int interval, char key[], int min, int max){

        if (argc != 5) {
            printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
            exit(-1);
        }
        //verificar input

    srand(getpid());
    while(1){
        printf("%s#%s#%d", argv[0], argv[1], rand() % (argv[4]-argv[3]) + argv[3]);
        sleep(argv[2]);
    }
    return 0;
}

