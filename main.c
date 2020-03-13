#include "thread_placer.h"
#include <stdio.h>
#include <unistd.h>

thread_placer_t placer;

void* thread_entry(void* arg)
{
    char threadname[16];
    uint64_t id = (uint64_t)arg;
    sprintf(threadname, "thread_%lu", id);
    thread_placer_bind(&placer, threadname, THREAD_BIND_BEST_EFFORT);
    while (1)
    {
        sleep(1);
    }
    return 0;
}

int main()
{
    thread_placer_init(&placer, 0, 0);

    thread_placer_bind(&placer, "play_cpu_main", THREAD_BIND_SHARED);
    uint64_t i;
    for (i = 0; i < 240; i++)
    {
        pthread_t th;
        pthread_create(&th, 0, thread_entry, (void*)i);
        sleep(1);
    }
    while (1)
    {
        sleep(1);
    }
    return 0;
}

