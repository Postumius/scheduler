#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;
  
  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;
  
  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  int is_alive;
  
  //   b. If the task is sleeping, when should it wake up?
  size_t wake_time;
  
  //   c. If the task is waiting for another task, which task is it waiting for?
  int waiting_for;
  
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
} task_info_t;

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
int num_alive;
task_info_t tasks[MAX_TASKS]; //< Information for every task
ucontext_t main_ctx;

int is_ready(task_info_t task) {
  return task.is_alive &&
    (task.waiting_for == -1 ||
     !tasks[task.waiting_for].is_alive) &&
    task.wake_time <= time_ms();
}

//loops through the tasks and schedules the next available one
void schedule_next() {
  int i = current_task + 1;
  while(true) {
    if (is_ready(tasks[i])) {      
      int old_task = current_task;
      current_task = i;
      tasks[current_task].waiting_for = -1;
      swapcontext(&tasks[old_task].context,
                  &tasks[current_task].context);
      break;
    } else {
      i++;
      if (i >= num_tasks)
        i = 0;
    }
  }
}

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functiosn in this file.
 */
void scheduler_init() {
  current_task = 0;
  num_tasks = 1;
  num_alive = 1;

  tasks[0].is_alive = 1;

  tasks[0].waiting_for = -1;  
  
  getcontext(&tasks[0].context);
 
}


/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  num_alive--;
  tasks[current_task].is_alive = 0;
  
  free(tasks[current_task].context.uc_stack.ss_sp);
  tasks[current_task].context.uc_stack.ss_size = 0;
  
  free(tasks[current_task].exit_context.uc_stack.ss_sp);
  tasks[current_task].exit_context.uc_stack.ss_size = 0;
  
  schedule_next();
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;
  num_alive++;
  
  // Set the task handle to this index, since task_t is just an int
  *handle = index;

  ///Mark the task as alive
  tasks[index].is_alive = 1;

  //Task is waiting for no one
  tasks[index].waiting_for = -1;
 
  // We're going to make two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second
  
  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);
  
  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;
  
  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);
  
  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);
  
  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;
  
  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;
  
  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  tasks[current_task].waiting_for = handle;
  schedule_next();
}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 * 
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  tasks[current_task].wake_time = time_ms() + ms;
  schedule_next();
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  while (true) {
    int input = getch();
    if (input == ERR) {
      schedule_next();
    }
    else return input;
  }
}
