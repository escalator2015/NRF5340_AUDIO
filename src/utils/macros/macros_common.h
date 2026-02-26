/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _MACROS_H_
#define _MACROS_H_

#include <errno.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/kernel.h>

/* Error check. If != 0, print err code and call _SysFatalErrorHandler in main.
 * For debug mode all LEDs are turned on in case of an error.
 */

#define PRINT_AND_OOPS(code)                                                                       \
	do {                                                                                       \
		LOG_ERR("ERR_CHK Err_code: [%d] @ line: %d\t", code, __LINE__);                    \
		k_oops();                                                                          \
	} while (0)

#define ERR_CHK(err_code)                                                                          \
	do {                                                                                       \
		if (err_code) {                                                                    \
			PRINT_AND_OOPS(err_code);                                                  \
		}                                                                                  \
	} while (0)

#define ERR_CHK_MSG(err_code, msg)                                                                 \
	do {                                                                                       \
		if (err_code) {                                                                    \
			LOG_ERR("%s", msg);                                                        \
			PRINT_AND_OOPS(err_code);                                                  \
		}                                                                                  \
	} while (0)

#if (defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_ANALYZER))

#define STACK_USAGE_PRINT(thread_name, p_thread)                                                   \
	do {                                                                                       \
		static uint64_t thread_ts;                                                         \
		size_t unused_space_in_thread_bytes;                                               \
		if (k_uptime_get() - thread_ts > CONFIG_PRINT_STACK_USAGE_MS) {                    \
			k_thread_stack_space_get(p_thread, &unused_space_in_thread_bytes);         \
			thread_ts = k_uptime_get();                                                \
			LOG_DBG("Unused space in %s thread: %d bytes", thread_name,                \
				unused_space_in_thread_bytes);                                     \
		}                                                                                  \
	} while (0)
#else
#define STACK_USAGE_PRINT(thread_name, p_stack)
#endif /* (defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_ANALYZER)) */

#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)

/* Macro para imprimir estadísticas de un k_heap (heap de kernel) */
#define HEAP_USAGE_PRINT(heap_name, kheap_ptr)                                                     \
	do {                                                                                       \
		static uint64_t heap_ts;                                                           \
		struct sys_memory_stats stats;                                                     \
		if (k_uptime_get() - heap_ts > CONFIG_PRINT_HEAP_USAGE_MS) {                       \
			sys_heap_runtime_stats_get(&((kheap_ptr)->heap), &stats);                  \
			heap_ts = k_uptime_get();                                                  \
			LOG_DBG("Heap %s: free=%zu allocated=%zu max_alloc=%zu", heap_name,      \
				stats.free_bytes, stats.allocated_bytes, stats.max_allocated_bytes); \
		}                                                                                  \
	} while (0)

/* Macro para imprimir estadísticas del heap del sistema */
#define SYSTEM_HEAP_USAGE_PRINT()                                                                  \
	do {                                                                                       \
		static uint64_t sys_heap_ts;                                                       \
		struct sys_memory_stats stats;                                                     \
		extern struct k_heap _system_heap;                                                 \
		if (k_uptime_get() - sys_heap_ts > CONFIG_PRINT_HEAP_USAGE_MS) {                   \
			sys_heap_runtime_stats_get(&_system_heap.heap, &stats);                    \
			sys_heap_ts = k_uptime_get();                                              \
			LOG_DBG("System heap: free=%zu allocated=%zu max_alloc=%zu",               \
				stats.free_bytes, stats.allocated_bytes, stats.max_allocated_bytes); \
		}                                                                                  \
	} while (0)
#else
#define HEAP_USAGE_PRINT(heap_name, kheap_ptr)
#define SYSTEM_HEAP_USAGE_PRINT()
#endif /* defined(CONFIG_SYS_HEAP_RUNTIME_STATS) */

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif /* MIN */

#define COLOR_BLACK "\x1B[0;30m"
#define COLOR_RED "\x1B[0;31m"
#define COLOR_GREEN "\x1B[0;32m"
#define COLOR_YELLOW "\x1B[0;33m"
#define COLOR_BLUE "\x1B[0;34m"
#define COLOR_MAGENTA "\x1B[0;35m"
#define COLOR_CYAN "\x1B[0;36m"
#define COLOR_WHITE "\x1B[0;37m"

#define COLOR_RESET "\x1b[0m"

#define BIT_SET(REG, BIT) ((REG) |= (BIT))
#define BIT_CLEAR(REG, BIT) ((REG) &= ~(BIT))

#endif /* _MACROS_H_ */
