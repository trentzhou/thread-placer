#ifndef _THREAD_PLACER_T
#define _THREAD_PLACER_T

#include <stdint.h>
#include <pthread.h>

#define MAX_CPU_THREADS 256

struct cpu_topology_s
{
    // socket count
    uint16_t socket_count;
    
    // count as displayed in /proc/cpuinfo
    uint16_t thread_count;
    
    // map between cpu thread and numa socket
    uint16_t thread_sockets[MAX_CPU_THREADS];
};

typedef struct cpu_topology_s cpu_topology_t;

/**
 * Probe cpu topology.
 */
void probe_cpu_topology(cpu_topology_t* topo);


struct thread_placer_s
{
    cpu_topology_t topo;
    uint16_t cpu_used_count[MAX_CPU_THREADS];
    pthread_mutex_t mutex;
    uint16_t preferred_socket;
    uint16_t shared_thread_id;
};

typedef struct thread_placer_s thread_placer_t;

enum thread_bind_flag_e
{ 
    THREAD_BIND_ALL,        // bind to all possible cpu cores
    THREAD_BIND_SOCKET,     // bind to all possible cpu cores on preferred numa socket
    THREAD_BIND_BEST_EFFORT,// use one core in the preferred numa socket.
    THREAD_BIND_SHARED,     // shared on the preferred numa socket
};

typedef enum thread_bind_flag_e thread_bind_flag_t;

/**
 * Initialize the thread placer.
 */
void thread_placer_init(thread_placer_t* placer, uint16_t preferred_socket, uint16_t shared_thread_id);

/**
 * Skip a cpu core, so threads are not placed on it.
 */
void thread_placer_skip(thread_placer_t* placer, uint32_t cpu_id);

/**
 * Bind the running thread on a cpu core.
 */
void thread_placer_bind(thread_placer_t* placer, const char* name, thread_bind_flag_t flag);

/**
 * allocate a cpu core for 1 thread. returns the core id
 */
uint16_t thread_placer_alloc(thread_placer_t* placer);

#endif

