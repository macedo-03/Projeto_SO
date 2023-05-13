// José Francisco Branquinho Macedo - 2021221301
// Miguel Filipe Mota Cruz - 2021219294

// #define DEBUG //remove this line to remove debug messages (...)

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
#define BUF_SIZE 256

typedef struct
{
    long message_id;
    int type; // 0 - origem:user; 1 - origem:sensor
    char cmd[BUF_SIZE];
} Message;

int pipe_id, mq_id, id, valido = 1;

pthread_t mq_reader;
Message msg;
struct sigaction action;
sigset_t block_extra_set;

// function to clean resources and terminate program
void clean()
{
    pthread_kill(mq_reader, SIGUSR1);
    pthread_join(mq_reader, NULL);

    close(pipe_id);
}

// function to handle signals
void handler(int signum)
{
    if (signum == SIGUSR1)
    { // thread
        pthread_exit(NULL);
    }
    else if (signum == SIGINT)
    { // main_process
        clean();
        exit(0);
    }
}

// function for read_msq thread
void *read_msq()
{
    while (1)
    {
        // receive message from message queue and print to screen
        if (msgrcv(mq_id, &msg, sizeof(Message) - sizeof(long), id, 0) == -1)
        {
            perror("error receiving from message queue");
            close(pipe_id);
            exit(-1);
        }
        if (msg.type == 0)
        { // alert
            printf("\n%s\n", msg.cmd);
        }
        else
        { // feedback
            printf("%s\n", msg.cmd);
        }
    }
    pthread_exit(NULL);
    return NULL;
}

int main(int argc, char *argv[])
{
    // verify program arguments
    if (argc != 2)
    {
        printf("user_console {console id}\n");
        exit(-1);
    }
    else if ((int)strlen(argv[1]) != 3 && !convert_int(argv[1], NULL))
    { // console id
        printf("user_console {console id}\n");
        exit(-1);
    }

    char buf[BUF_SIZE], cmd[64], alert_id[32], key[32];
    char str_min[16], str_max[16];
    int min, max;

    // block all signals except SIGINT and SIGUSR1
    action.sa_flags = 0;
    sigfillset(&action.sa_mask);
    sigdelset(&action.sa_mask, SIGINT);
    sigdelset(&action.sa_mask, SIGUSR1);
    sigprocmask(SIG_SETMASK, &action.sa_mask, NULL);

    // set sigset_t with SIGUSR1 and SIGINT
    sigemptyset(&block_extra_set);
    sigaddset(&block_extra_set, SIGINT);
    sigaddset(&block_extra_set, SIGUSR1);

    // ignore SIGINT and SIGUSR1 during setup
    action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);

    // set handler() has the handler function
    action.sa_handler = handler;

    // open message queue
    if ((mq_id = msgget(MQ_KEY, 0777)) < 0)
    {
        perror("Cannot open message queue!\n");
        exit(-1);
    }

    // open pipe to write
    if ((pipe_id = open(CONSOLE_PIPE, O_WRONLY)) < 0)
    {
        perror("Cannot open pipe for writing!\n");
        exit(-1);
    }

    id = getpid();

    // create thread to receive messages from message queue
    if (pthread_create(&mq_reader, NULL, read_msq, NULL) != 0)
    {
        perror("Cannot create thread\n");
        close(pipe_id);
        exit(-1);
    }

    Message m;
    // message id = process id
    m.message_id = id;
    m.type = 0;

    // redirects to handler() when SIGINT or SIGUSR1 is received
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);

    // commands menu
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
    if (!input_str(cmd, 1))
    {
        printf("Erro de formatacao do comando\n");
    }

    while (strcmp(cmd, "EXIT") != 0)
    {
        sigprocmask(SIG_BLOCK, &block_extra_set, NULL);
        valido = 1;
        if (strcmp(cmd, "ADD_ALERT") == 0)
        {
            sscanf(buf, "%s %s %s %s %s", cmd, alert_id, key, str_min, str_max);
            //            printf("%s\t%s\t%s\t%s\n",alert_id, key, str_min, str_max );
            if (!(convert_int(str_min, &min) &&
                  convert_int(str_max, &max) &&
                  input_str(alert_id, 0) &&
                  input_str(key, 0)))
            {
                printf("Erro de formatacao dos argumentos\n");
                valido = 0;
                // exit(-1);
            }
            else if (max <= min)
            {
                printf("Valor maximo tem de ser maior que o minimo (max > min)\n");
                valido = 0;
                // exit(-1);
            }
            // sprintf(buf, "%s %s %s %d %d", cmd, alert_id, key, min, max);
#ifdef DEBUG
            printf("add_alert\n");
#endif
        }
        else if (strcmp(cmd, "REMOVE_ALERT") == 0)
        {
            sscanf(buf, "%s %s", cmd, alert_id);
            if (!input_str(alert_id, 0))
            {
                printf("Erro de formatacao do argumento\n");
                valido = 0;
                // exit(-1);
            }
            // sprintf(buf, "%s %s", cmd, alert_id);
#ifdef DEBUG
            printf("remove_alert\n");
#endif
        }
        else if (strcmp(cmd, "STATS") == 0)
        {
#ifdef DEBUG
            printf("stats\n");
#endif
        }
        else if (strcmp(cmd, "RESET") == 0)
        {
#ifdef DEBUG
            printf("reset\n");
#endif
        }
        else if (strcmp(cmd, "LIST_ALERTS") == 0)
        {
#ifdef DEBUG
            printf("list_alerts\n");
#endif
        }
        else if (strcmp(cmd, "SENSORS") == 0)
        {
#ifdef DEBUG
            printf("sensors\n");
#endif
        }
        else
        {
            printf("Comando nao reconhecido\n");
            valido = 0;
        }
        if (valido)
        {

#ifdef DEBUG
            printf("buf: %s\n", buf);
#endif
            buf[strlen(buf) - 1] = '\0';
            string_to_upper(buf);
            strcpy(m.cmd, buf);
            // write user message to pipe
            if (write(pipe_id, &m, sizeof(Message)) == -1)
            {
                clean();
                perror("error writing to pipe");
                exit(-1);
            }
        }
        // unblock SIGINT and SIGUSR1
        sigprocmask(SIG_UNBLOCK, &block_extra_set, NULL);

        fgets(buf, BUF_SIZE, stdin);
        sscanf(buf, "%s", cmd);
        if (!input_str(cmd, 1))
        {
            printf("Erro de formatacao do Comando\n");
        }
    }
    return 0;
}
