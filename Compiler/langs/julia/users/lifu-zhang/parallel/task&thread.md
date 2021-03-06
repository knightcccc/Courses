# task & thread

## task

在 `julia.h` 中定义了 task 的结构：

```c
typedef struct _jl_task_t {
    JL_DATA_TYPE
    jl_value_t *tls;
    jl_sym_t *state;
    jl_value_t *donenotify;
    jl_value_t *result;
    jl_value_t *exception;
    jl_value_t *backtrace;
    jl_value_t *logstate;
    jl_function_t *start;

// hidden state:
    jl_ucontext_t ctx; // saved thread state
    void *stkbuf; // malloc'd memory (either copybuf or stack)
    size_t bufsz; // actual sizeof stkbuf
    unsigned int copy_stack:31; // sizeof stack for copybuf
    unsigned int started:1;

    // current exception handler
    jl_handler_t *eh;
    // saved gc stack top for context switches
    jl_gcframe_t *gcstack;
    // saved exception stack
    jl_excstack_t *excstack;
    // current world age
    size_t world_age;

    // id of owning thread
    // does not need to be defined until the task runs
    int16_t tid;
#ifdef JULIA_ENABLE_THREADING
    // This is statically initialized when the task is not holding any locks
    arraylist_t locks;
#endif
    jl_timing_block_t *timing_stack;
} jl_task_t;
```

task 的相关操作在 `src/task.c` 中。

## threads

Julia 中对于 thread 的定义主要在 `threading.h` 和 `threadgroup.h` 中，对于 thread 的操作主要在 `threading.c` 中。

首先 Julia 是根据当前操作系统去判断使用的 thread 操作的 API，对于 Windows，Julia 采用的主要是微软自家的线程操作，调用的是头文件 `Windows.h` ；对于非 Windows 系统，Julia 采用的是 pthreads 库进行线程的操作。

### Thread-local storage (TLS)

在 `threading.c` 中定义了一系列的 thread 操作，如初始化 thread，执行分配到 thread 上的函数等操作，Julia 在对线程进行这些操作时会用到 tls_states buffer (Thread-local storage，Julia 的优化定义在 `src/tls.h` 中)，值得注意的是 Julia 对 tls 的使用也做了一些优化：

首先是因为 Mac 和 Windows 不是用 ELF，所以采用运行时的 API 来创建 TLS，据他们说这种方式要比直接使用 `__thread` 关键字的效率更高一些。

- 对于 Mac：因为没有静态 TLS 模型，所以采用的是调用 pthread 库中的 API 来进行创建；
- 对于 Windows：则是使用微软自家的 TLSAlloc 实现；
- 对于 Linux，FreeBSD：使用 `__thread` 关键字进行实现；

Julia 中具体的 TLS 结构在 `src/julia_threads.h` 中定义为 `_jl_tls_states_t` 。

### Thread init

对于 master thread 的初始化，Julia 同样根据操作系统进行了区分，对于 Windows 采用了 `DuplicateHandle` 进行处理，而其他的操作系统，则由 `ti_initthread(0)` 进行。

对于其他线程，均是由 `static void ti_initthread(int16_t tid)` 进行初始化，在该函数里面对目标线程的 tls_states 进行了各项初始化：

```c
static void ti_initthread(int16_t tid)
{
    jl_ptls_t ptls = jl_get_ptls_states();
#ifndef _OS_WINDOWS_
    ptls->system_id = pthread_self();
#endif
    assert(ptls->world_age == 0);
    ptls->world_age = 1; // OK to run Julia code on this thread
    ptls->tid = tid;
    ptls->pgcstack = NULL;
    ptls->gc_state = 0; // GC unsafe
    // Conditionally initialize the safepoint address. See comment in
    // `safepoint.c`
    if (tid == 0) {
        ptls->safepoint = (size_t*)(jl_safepoint_pages + jl_page_size);
    }
    else {
        ptls->safepoint = (size_t*)(jl_safepoint_pages + jl_page_size * 2 +
                                    sizeof(size_t));
    }
    ptls->defer_signal = 0;
    void *bt_data = malloc(sizeof(uintptr_t) * (JL_MAX_BT_SIZE + 1));
    if (bt_data == NULL) {
        jl_printf(JL_STDERR, "could not allocate backtrace buffer\n");
        gc_debug_critical_error();
        abort();
    }
    memset(bt_data, 0, sizeof(uintptr_t) * (JL_MAX_BT_SIZE + 1));
    ptls->bt_data = (uintptr_t*)bt_data;
    ptls->sig_exception = NULL;
    ptls->previous_exception = NULL;
#ifdef _OS_WINDOWS_
    ptls->needs_resetstkoflw = 0;
#endif
    jl_init_thread_heap(ptls);
    jl_install_thread_signal_handler(ptls);

    jl_all_tls_states[tid] = ptls;
}
```

### Thread function

在 `threading.c` 中还定义了处理分配到 thread 上函数的操作，主要定义在函数 `void ti_threadfun(void *arg)` 中，这里会首先创建一个新的 thread，然后对该线程进行一个栈空间的初始化，这个栈空间的初始化同样根据不同的操作系统采用的不同的栈分配 API（非 Windows 采用的 pthread），之后则是在分配好的栈空间上创建一个根任务，然后将该 thread 添加到 threadgroup 中初始化，从 threadgroup 的结构体中可以看到采用 libuv 库中的一些接口：

```c
typedef struct {
    int16_t *tid_map, num_threads, added_threads;
    uint8_t num_sockets, num_cores, num_threads_per_core;

    // fork/join/barrier
    uint8_t group_sense; // Written only by master thread
    ti_thread_sense_t **thread_sense;
    void              *envelope;

    // to let threads sleep
    uv_mutex_t alarm_lock;
    uv_cond_t  alarm;
    uint64_t   sleep_threshold;
} ti_threadgroup_t;
```

之后则会进入一个 `for(::)` 循环来执行调用的任务，是对分配给当前线程上的任务进行处理，判断结构体 `ti_threadwork_t work` 是否为空，根据 work 中的成员进行相关函数的的调用，以及根据成员状态判断该任务是否完成执行跳出循环。

```c
typedef struct {
    uint8_t command;
    jl_method_instance_t *mfunc;
    jl_callptr_t fptr;
    jl_value_t **args;
    uint32_t nargs;
    jl_value_t *ret;
    size_t world_age;
} ti_threadwork_t;
```



