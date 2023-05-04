//José Francisco Branquinho Macedo - 2021221301
//Miguel Filipe Mota Cruz - 2021219294

#include "internal_queue.h"

//funcao que cria a internal_queue_console
//return ponteiro "InternalQueueConsole*"
InternalQueue* create_internal_queue(){
    InternalQueue *internal_queue = malloc(sizeof(InternalQueue));
    if(internal_queue){
        internal_queue->start=NULL;
        internal_queue->size=0;
    }
    return internal_queue;
}


//funcao que adiciona uma nova mensagem a internal queue
void insert_internal_queue(InternalQueue *this_internal_queue, Message message_to_insert){
    NoInternalQueue *new_message = malloc(sizeof(NoInternalQueue));

    if(new_message){
        new_message->message = message_to_insert;
        new_message->next = NULL;

        //lista vazia
        if(this_internal_queue->size == 0){
            this_internal_queue->start = new_message;
            this_internal_queue->end = new_message;
        }
        //1 ou + nos
        else{
            this_internal_queue->end = new_message;
        }
        this_internal_queue->size +=1;
        internal_queue_size +=1;
    }
}


Message delete_node(InternalQueue* this_internal_queue){
    Message message_to_dispatch = this_internal_queue->start->message;
    NoInternalQueue *node_to_delete = this_internal_queue->start;

    if(this_internal_queue->size == 1){
        this_internal_queue->start = this_internal_queue->end = NULL;
        this_internal_queue->size=0;
    }else{
        this_internal_queue->start = this_internal_queue->start->next;
    }
    this_internal_queue->size--;
    internal_queue_size--;

    free(node_to_delete);
    return message_to_dispatch;
}

Message get_next_message(InternalQueue* internal_queue_console, InternalQueue* internal_queue_sensor){

    if(internal_queue_console->size>0){
        return delete_node(internal_queue_console);
    }
    else if(internal_queue_sensor->size>0){
        return delete_node(internal_queue_sensor);
    }
    else{
        //TODO: wait ate que haja novas mensagens ??
    }
    Message m;
    return m;
}