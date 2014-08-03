#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "log.h"
#include "mem.h"

#include "event.h"

#define MAX_EVENT_ID 1024

static event_handler_t handlers[MAX_EVENT_ID];

static int event_id_max = 0;

struct event_t {
    int event_id;
    void* data;
    struct event_t* next;
};

static struct event_t *head = NULL, *tail = NULL;

int proxy_event_set_handler(event_handler_t handler) {
    int id = event_id_max++;
    handlers[id] = handler;
    return id;
}

int proxy_event_post(int event_id, void* data) {
    struct event_t* ev = mem_alloc(sizeof(struct event_t));
    ev->event_id = event_id;
    ev->data = data;
    ev->next = NULL;
    if (tail == NULL)
        head = tail = ev;
    else
        tail->next = ev;
    return 0;
}

int proxy_event_work() {
    PLOGD("Entering event loop");
    while (head) {
        PLOGD("Dispatch event handler type: %d", head->event_id);
        handlers[head->event_id](head->event_id, head->data);
        struct event_t* next = head->next;
        mem_free(head);
        head = next;
    }
    tail = NULL;
    return 0;
}
