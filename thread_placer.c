#define _GNU_SOURCE
#include "thread_placer.h"
#include <ctype.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <stdbool.h>
#include <string.h>

const char* parse_number(const char* p, uint16_t* number)
{
    uint16_t result = 0;
    while (isdigit(*p))
    {
        result = result * 10 + *p-'0';
        p++;
    }
    *number = result;
    return p;
}

const char* parse_number_range(const char* p, uint16_t *head, uint16_t* tail)
{
    if (isdigit(*p))
    {
        p = parse_number(p, head);
        *tail = *head;
        if (*p == '-')
        {
            p++;
            p = parse_number(p, tail);
        }
    }
    return p;
}

/**
 * Parse a list text into an array.
 * Example text:
 *
 *   1,2,3,4
 *   1-4
 *   1-2,3-4
 *
 * All the above texts generate the same array [1,2,3,4]
 */
uint16_t parse_list_text(const char* text, uint16_t* array, uint16_t array_size)
{
    const char* p = text;
    uint16_t n = 0;

    while (*p != 0)
    {
        if (isdigit(*p))
        {
            uint16_t head, tail;
            p = parse_number_range(p, &head, &tail);
            while (head <= tail && n < array_size)
            {
                array[n] = head;
                n++;
                head++;
            }
            if (*p == ',')
                p++;
            else if (isspace(*p) || !*p)
                break;
            else
            {
                printf("%lu: Expected ',' not met\n", p-text);
                break;
            }
        }
        else if (isspace(*p))
            break;
        else
        {
            printf("%lu: Expected number not met\n", p-text);
            break;
        }
    }
    return n;
}

#define MAX_SOCKET_COUNT 16
#define MAX_THREAD_COUNT 256
#define LINE_BUF_SIZE 256

size_t read_file(const char* filename, char* buffer, int buflen)
{
    FILE* file = fopen(filename, "r");
    size_t result = fread(buffer, 1, buflen-1, file);
    fclose(file);
    buffer[result] = 0; // add trailing 0
    return result;
}

/**
 * Probe the CPU topology.
 */
void probe_cpu_topology(cpu_topology_t* topo)
{
    uint16_t sockets[MAX_SOCKET_COUNT];
    uint16_t threads[MAX_THREAD_COUNT];
    char line[LINE_BUF_SIZE];
    uint16_t socket_count;
    uint16_t thread_count;
    uint16_t i, j;

    // init topo
    topo->socket_count = 0;
    topo->thread_count = 0;
    for (i = 0; i < MAX_CPU_THREADS; i++)
    {
        topo->thread_sockets[i] = 0;
    }
    // get node list at /sys/devices/system/node/online
    read_file("/sys/devices/system/node/online", line, LINE_BUF_SIZE);
    socket_count = parse_list_text(line, sockets, MAX_SOCKET_COUNT);
    topo->socket_count = socket_count;

    // get all cpus in the nodes from /sys/devices/system/node/node${id}/cpulist
    for (i = 0; i < socket_count; i++)
    {
        snprintf(line, LINE_BUF_SIZE, "/sys/devices/system/node/node%d/cpulist", (int)i);
        read_file(line, line, LINE_BUF_SIZE);
        thread_count = parse_list_text(line, threads, MAX_THREAD_COUNT);
        topo->thread_count += thread_count;
        for (j = 0; j < thread_count; j++)
        {
            topo->thread_sockets[threads[j]] = i;
        }
    }
}

void thread_placer_init(thread_placer_t* placer, uint16_t preferred_socket, uint16_t shared_thread_id)
{
    memset(placer, 0, sizeof(*placer));
    probe_cpu_topology(&placer->topo);
    pthread_mutex_init(&placer->mutex, 0);
    if (preferred_socket < placer->topo.socket_count)
        placer->preferred_socket = preferred_socket;
    if (shared_thread_id < placer->topo.thread_count)
        placer->shared_thread_id = shared_thread_id;
}

void thread_placer_skip(thread_placer_t* placer, uint32_t cpu_id)
{
    if (cpu_id < placer->topo.thread_count)
    {
        placer->cpu_used_count[cpu_id] = 10000; // too high, so never use
    }
}
#define MAX_THREAD_NAME 16

void thread_placer_bind(thread_placer_t* placer, const char* name, thread_bind_flag_t flag)
{
    int i;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    prctl(PR_SET_NAME, (unsigned long)name);
    pthread_t self = pthread_self();

    if (flag == THREAD_BIND_ALL)
    {
        // bind the thread to all possible cores
        
        for (i = 0 ;i < placer->topo.thread_count; i++)
        {
            CPU_SET(i, &cpuset);
        }
    }
    else if (flag == THREAD_BIND_SOCKET)
    {
        for (i = 0; i < placer->topo.thread_count; i++)
        {
            if (placer->topo.thread_sockets[i] == placer->preferred_socket)
            {
                CPU_SET(i, &cpuset);
            }
        }
    }
    else if (flag == THREAD_BIND_SHARED)
    {
        pthread_mutex_lock(&placer->mutex);

        CPU_SET(placer->shared_thread_id, &cpuset);
        placer->cpu_used_count[placer->shared_thread_id]++;

        pthread_mutex_unlock(&placer->mutex);
    }
    else if (flag == THREAD_BIND_BEST_EFFORT)
    {
        uint16_t target = thread_placer_alloc(placer);
        printf("Binding thread %s to cpu %d\n", name, target);
        CPU_SET(target, &cpuset);
    }

    pthread_setaffinity_np(self, sizeof(cpuset), &cpuset);
}

uint16_t thread_placer_alloc(thread_placer_t* placer)
{
   // first, try to find a core in the current socket
    bool found = false;
    uint16_t target = 0;
    int i;

    pthread_mutex_lock(&placer->mutex);
    for (i = 0; i < placer->topo.thread_count; i++)
    {
        if (placer->cpu_used_count[i] == 0 && 
            placer->topo.thread_sockets[i] == placer->preferred_socket)
        {
            target = i;
            found = true;
            break;
        }
    }
    if (!found)
    {
        // 2nd round, find any free cpu
        for (i = 0; i < placer->topo.thread_count; i++)
        {
            if (placer->cpu_used_count[i] == 0)
            {
                target = i;
                found = true;
                break;
            }
        }
    }
    if (!found)
    {
        // 3rd round, find the cpu with least threads
        int count = placer->cpu_used_count[0];
        target = 0;
        for (i = 0; i < placer->topo.thread_count; i++)
        {
            if (placer->cpu_used_count[i] < count)
            {
                target = i;
                found = true;
                count = placer->cpu_used_count[i];
            }
        }
    }
    placer->cpu_used_count[target]++;
    pthread_mutex_unlock(&placer->mutex);
    return target;
}
