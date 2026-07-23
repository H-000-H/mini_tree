/*
 * FreeRTOS Kernel V11.3.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 */

#ifndef WAIT_FOR_EVENT_H
#define WAIT_FOR_EVENT_H

#include <stdbool.h>
#include <time.h>
#include <pthread.h>

struct event;

struct event * event_create( void );
void event_delete( struct event * ev );
bool event_wait( struct event * ev );
bool event_wait_timed( struct event * ev, time_t ms );
void event_signal( struct event * ev );

#endif /* WAIT_FOR_EVENT_H */
