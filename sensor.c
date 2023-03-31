#include <unistd.h> // process
#include <stdio.h>
#include <stdlib.h>

#include "costumio.h"

int main(int argc, char *argv[]){
//        char sensor_id[], char key[], int interval,  int min, int max){
        int time_interval, min_value, max_value;

        if (argc != 5) {
            printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
            exit(-1);
        }
        else if(!(  convert_int(argv[1], &time_interval)    &&
                    convert_int(argv[3], &min_value)      &&
                    convert_int(argv[4], &max_value)     &&
                    input_str(argv[0], 0)   &&   // sensor_id
                    input_str(argv[2], 0)        // key
                    )){

            printf("sensor {sensor_id} {sending interval (sec) (>=0)} {key} {min value} {max value}\n");
            exit(-1);
        }



    srand(getpid());
    while(1){
        printf("%s#%s#%d", argv[0], argv[2], rand() % (max_value-min_value) + min_value);
        sleep(time_interval);
    }
    return 0;
}

