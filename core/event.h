#ifndef _PROXY_EVENT_H_
#define _PROXY_EVENT_H_

typedef int (*event_handler_t)(int event, void* data);

// Return: event id
int proxy_event_set_handler(event_handler_t event);

int proxy_event_post(int event_id, void* data);

int proxy_event_work();

#endif
