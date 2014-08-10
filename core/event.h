#ifndef _PROXY_EVENT_H_
#define _PROXY_EVENT_H_

#include <stdint.h>

typedef uint32_t event_id;

// data0 is set by event_set_handler, data1 is set by evnet_post
typedef int (*event_handler)(event_id event, void* data0, void* data1);

void event_module_init();

void event_module_done();

event_id event_get_id();

void event_free_id(event_id eid);

int event_set_handler(event_id eid, event_handler handler, void* data0);

int event_post(event_id eid, void* data1);

int event_work();

#endif
