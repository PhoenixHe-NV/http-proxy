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

static UT_icd event_icd = {sizeof(event_id), NULL, NULL, NULL};

static UT_array id_freelist;

void event_module_init() {
    utarray_init(&id_freelist, &event_icd);
}

void event_module_done() {
    utarray_done(&id_freelist);
}

event_id event_get_id() {
    if (utarray_len(&id_freelist) == 0)
        return id_max++;
    event_id ret = *utarray_back(&id_freelist);
    utarray_pop_back(&id_freelist);
    return ret;
}

void event_free_id(event_id eid) {
    utarray_push_back(&id_freelist, &eid);
    struct handler_table* h;
    HASH_FIND(hh, handlers, &eid, sizeof(event_id), h);
    if (h) {
        HASH_DEL(handlers, h);
        mem_free(h);
    }
}

int event_set_handler(event_id eid, event_handler handler) {
    struct handler_table* h;
    HASH_FIND(hh, handlers, &eid, sizeof(event_id), h);
    if (h == NULL) {
        h = mem_alloc(sizeof(handler_table));
        h->eid = eid;
        HASH_ADD(hh, handlers, eid, sizeof(event_id), h);
    }
    h->handler = handler;
    return 0;
}

int event_post(event_id eid, void* data) {
    struct event* ev = mem_alloc(sizeof(struct event));
    ev->eid = eid;
    ev->data = data;
    ev->next = NULL;
    if (tail == NULL)
        head = tail = ev;
    else
        tail->next = ev;
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
            h->handler(head->data);
        }

        struct event_t* next = head->next;
        mem_free(head);
        head = next;
    }
    tail = NULL;
    return 0;
}
