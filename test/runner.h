#ifndef _SIMPLE_TILES_RUNNER_H
#define _SIMPLE_TILES_RUNNER_H

#include "test.h"

#define TASK_ENTRY(name) \
  { &run_task_##name, #name },

typedef struct {
  void (*call)();
  const char *name;
} task_wrap_t;

task_wrap_t tasks[] = {
  TASK_ENTRY(list)
  TASK_ENTRY(layer)
  TASK_ENTRY(filter)
  TASK_ENTRY(style)
  TASK_ENTRY(map)
  TASK_ENTRY(integration)
  TASK_ENTRY(pool)
  { NULL, NULL }
};

#endif
