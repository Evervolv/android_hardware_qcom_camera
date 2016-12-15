/*
 * Copyright 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "cam_semaphore_tests"
#include <utils/Log.h>

#include <gtest/gtest.h>

#include "cam_semaphore.h"

#define NS_PER_S 1000000000

static inline void timespec_add_ms(timespec& ts, size_t ms) {
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000;
    if (ts.tv_nsec >= NS_PER_S) {
        ts.tv_sec++;
        ts.tv_nsec -= NS_PER_S;
    }
}

// Test cam_semaphore_timedwait
TEST(cam_semaphore_tests, cam_semaphore_timedwait) {

    cam_semaphore_t sem;
    cam_sem_init(&sem, 0);

    // Test timeout
    timespec ts;
    ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &ts));
    timespec_add_ms(ts, 100);

    errno = 0;
    ASSERT_EQ(-1, cam_sem_timedwait(&sem, &ts));
    ASSERT_EQ(ETIMEDOUT, errno);

    // Test successful wait
    ASSERT_EQ(0, clock_gettime(CLOCK_MONOTONIC, &ts));
    timespec_add_ms(ts, 100);

    errno = 0;
    cam_sem_post(&sem);
    ASSERT_EQ(0, cam_sem_timedwait(&sem, &ts));
    ASSERT_EQ(0, errno);
}
