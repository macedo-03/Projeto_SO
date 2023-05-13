// José Francisco Branquinho Macedo - 2021221301
// Miguel Filipe Mota Cruz - 2021219294

#ifndef PROJETO_SO_INTERNAL_QUEUE_H
#define PROJETO_SO_INTERNAL_QUEUE_H

// #includes here
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    long message_id;
    int type; // 0 - origem:user; 1 - origem:sensor
    char cmd[256];
} Message;

// estrutura de dados que representa o nó para cada message
// contém a struct message e um ponteiro para o nó seguinte
typedef struct NoInternalQueue
{
    Message message;
    struct NoInternalQueue *next;
} NoInternalQueue;

// estrutura de dados que é a lista de alunos
// contém um ponteiro para o primeiro nó da lista de alunos e um inteiro onde é armazenado o número de alunos da lista
typedef struct InternalQueue
{
    NoInternalQueue *start;
    NoInternalQueue *end;
    int size;
} InternalQueue;

int internal_queue_size; // soma dos size das duas queues

// TODO: antes de chamar insert_internal_queue para inserir nova mensagem verificar internal_queue_size?
//  ou chama na mesma e ignora essa mensagem dentro desta funcao?

InternalQueue *create_internal_queue();
void insert_internal_queue(InternalQueue *this_internal_queue, Message *message_to_insert);
Message get_next_message(InternalQueue *internal_queue_console, InternalQueue *internal_queue_sensor);
Message delete_node(InternalQueue *this_internal_queue);

#endif // PROJETO_SO_INTERNAL_QUEUE_H
