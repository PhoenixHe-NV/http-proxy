#ifndef _PROXY_EVENT_H_
#define _PROXY_EVENT_H_

#include <stdint.h>

typedef uint32_t event_id;

typedef int (*event_handler)(event_id event, void* data);

void event_module_init();

void event_module_done();

event_id event_get_id();

void event_free_id(event_id eid);

int event_post(event_id eid, void* data);

int proxy_event_work();

#endif
