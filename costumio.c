// José Francisco Branquinho Macedo - 2021221301
// Miguel Filipe Mota Cruz - 2021219294

#include "costumio.h"

// funcao semelhante ao atoi() mas que indica se houve erro de conversão
// caso a função seja bem sucedida -> return true
// caso seja detetado algum erro -> return false
int my_atoi(char str[], int *int_number)
{
    char *a = NULL;
    long number;
    errno = 0;

    // conversao da string que entra como primeiro parametro para um numero long int (base decimal)
    number = strtol(str, &a, 10);
    // caso tenho ocorrido over ou underflow
    if (errno == ERANGE)
        return 0;
    // caso algum erro nao específicado tenha ocorrido
    else if (errno != 0 && number == 0)
        return 0;
    // caso o número obtido seja superior ao limite maximo de um numero inteiro (definido na macro INT_MAX)
    else if (number > INT_MAX)
        return 0;

    // converte o número int long para int
    *int_number = (int)number;
    return 1;
}

// funcao que verifica se todos os caracteres de uma string sao digitos e converte-a para int
int convert_int(char str[], int *number)
{
    //    printf("%s\n", str);
    int i = 0;
    int len = (int)strlen(str);
    if (str[i] == '-')
        i++;
    while (i < len)
    {
        if (!isdigit(str[i]))
        {
            return 0;
        }
        i++;
    }
    if (number != NULL && !my_atoi(str, number))
    {
        return 0;
    }
    return 1;
}

// underscore == 1:
//   comandos user_console
// underscore == 0:
//   id consola, id alerta, id sensor
//   chave

// funcao que verifica a composicao de uma string e a torna os caracteres em maiusculas
int input_str(char str[], int underscore)
{
    int len = (int)strlen(str);
    // se a string estiver vazia -> return false
    if (len < 3)
        return 0;
    if (len > MY_MAX_INPUT)
        return 0;
    int i = 0;
    // enquanto os caracteres da string forem letras ou underscore _
    while (i < len)
    {
        // adiciona o dígito à string "converting"
        if (underscore == 1 && !(isalpha(str[i]) || isdigit(str[i]) || str[i] == '_'))
        {
            //            printf("1 asneira: %c\n", str[i]);
            return 0;
        }
        else if (underscore == 0 && !(isalpha(str[i]) || isdigit(str[i])))
        {
            //            printf("0 asneira: %c\n", str[i]);
            return 0;
        }
        str[i] = (char)toupper(str[i]);
        i++;
    }
    return 1; // true
}

// funcao que torna os caracteres de uma string em maiusculas
void string_to_upper(char str[])
{
    int i = 0;
    int len = (int)strlen(str);
    while (i < len)
    {
        str[i] = (char)toupper(str[i]);
        i++;
    }
}