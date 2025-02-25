/**
 * @file coolbpf.h
 * @author Shuyi Cheng (chengshuyi@linux.alibaba.com)
 * @brief 
 * @version 0.1
 * @date 2022-12-22
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __COOLBPF_H
#define __COOLBPF_H

#ifdef __VMLINUX_H__

#include "coolbpf_common.h"

#else

#define COOLBPF_MAJOR_VERSION 0
#define COOLBPF_MINOR_VERSION 1

#include "coolbpf_common.h"


/**
 * @brief Parameters required to create perf threads
 * 
 */
struct perf_thread_arguments {
    int mapfd;                          /**< fd of perf event map */
    perf_buffer_sample_fn sample_cb;    /**< callback function */
    perf_buffer_lost_fn lost_cb;        /**< callback function when the event is lost */
    int pg_cnt;                         /**< perf buffer size in page, default is 128 page */
    int timeout_ms;                     /**< timeout of perf poll in ms, default is 100ms */
};

/**
 * @brief get coolbpf major verison
 * 
 * @return uint32_t 
 */
COOLBPF_API uint32_t coolbpf_major_version();

/**
 * @brief get coolbpf minor version
 * 
 * @return uint32_t 
 */
COOLBPF_API uint32_t coolbpf_minor_version();

/**
 * @brief get coolbpf version as string
 * 
 * @return const char* 
 */
COOLBPF_API const char *coolbpf_version_string(void);

/**
 * @brief Create a perf thread to receive perf events
 * 
 * @param mapfd fd of map of type BPF_MAP_TYPE_PERF_EVENT_ARRAY
 * @return pthread_t thread id, you can use it later to destroy the thread
 */
COOLBPF_API pthread_t initial_perf_thread(struct perf_thread_arguments *args);

/**
 * @brief Destroy perf thread
 * 
 * @param thread perf thread id
 * @return int 
 */
COOLBPF_API int kill_perf_thread(pthread_t thread);

#endif

#endif
