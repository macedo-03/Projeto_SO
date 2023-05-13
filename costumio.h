// José Francisco Branquinho Macedo - 2021221301
// Miguel Filipe Mota Cruz - 2021219294

#ifndef PROJETO_SO_COSTUMIO_H
#define PROJETO_SO_COSTUMIO_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

#define MY_MAX_INPUT 32

int my_atoi(char str[], int *int_number);
int convert_int(char str[], int *number);
int input_str(char str[], int underscore);
void string_to_upper(char str[]);

#endif // PROJETO_SO_COSTUMIO_H
