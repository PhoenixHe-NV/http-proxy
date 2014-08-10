#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include "log.h"
#include "mem.h"
#include "utlib.h"

#include "event.h"

struct handler_table {
    event_id eid;
    event_handler handler;
    void* data;
    UT_hash_handle hh;
};

static struct handler_table* handlers = NULL;

static event_id id_max = 0;

struct event {
    event_id eid;
    void* data;
    struct event* next;
};

static struct event *head = NULL, *tail = NULL;

void event_module_init() {
}

void event_module_done() {
    event_work();
    struct handler_table *h, *tmp;
    HASH_ITER(hh, handlers, h, tmp) {
        HASH_DEL(handlers, h);
        mem_free(h);
    }
}

event_id event_get_id() {
    return id_max++;
}

void event_free_id(event_id eid) {
    struct handler_table* h;
    HASH_FIND(hh, handlers, &eid, sizeof(event_id), h);
    if (h) {
        HASH_DEL(handlers, h);
        mem_free(h);
    }
}

int event_set_handler(event_id eid, event_handler handler, void* data) {
    struct handler_table* h;
    HASH_FIND(hh, handlers, &eid, sizeof(event_id), h);
    if (h == NULL) {
        h = mem_alloc(sizeof(struct handler_table));
        h->eid = eid;
        HASH_ADD(hh, handlers, eid, sizeof(event_id), h);
    }
    h->handler = handler;
    h->data = data;
    return 0;
}

int event_post(event_id eid, void* data) {
    struct event* ev = mem_alloc(sizeof(struct event));
    ev->eid = eid;
    ev->data = data;
    ev->next = NULL;
    if (tail == NULL)
        head = tail = ev;
    else {
        tail->next = ev;
        tail = ev;
    }
    return 0;
}

int event_work() {
    PLOGD("Entering event loop");
    while (head) {
        event_id eid = head->eid;
        PLOGD("Dispatch event id: %d", eid);

        struct handler_table* h;
        HASH_FIND(hh, handlers, &eid, sizeof(event_id), h);
        if (h == NULL) {
            PLOGD("NO HANDLER FOUND!");
        } else {
            h->handler(eid, h->data, head->data);
        }

        struct event* next = head->next;
        mem_free(head);
        head = next;
    }
    tail = NULL;
    return 0;
}
