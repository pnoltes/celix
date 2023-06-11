/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CELIX_CELIX_SCHEDULED_EVENT_H
#define CELIX_CELIX_SCHEDULED_EVENT_H

#include "celix_bundle_context.h"
#include "framework_private.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct celix_scheduled_event celix_scheduled_event_t;

#define CELIX_SCHEDULED_EVENT_TIMEOUT_WAIT_FOR_PROCESSING_IN_SECONDS 30.0

/**
 * @brief Create a scheduled event for the given bundle.
 *
 * The scheduled event will be created with a use count of 1.
 *
 * @param[in] bndId The bundle id for the bundle which the scheduled event is created.
 * @param[in] scheduledEventId The id of the scheduled event.
 * @param[in] eventName The name of the event. If NULL, CELIX_SCHEDULED_EVENT_DEFAULT_NAME is used.
 * @param[in] initialDelayInSeconds The initial delay in seconds.
 * @param[in] intervalInSeconds The interval in seconds.
 * @param[in] callbackData The event data.
 * @param[in] callback The event callback.
 * @param[in] removedCallbackData The removed callback data.
 * @param[in] removedCallback The removed callback.
 * @return A new scheduled event or NULL if failed.
 */
celix_scheduled_event_t* celix_scheduledEvent_create(celix_framework_logger_t* logger,
                                                     long bndId,
                                                     long scheduledEventId,
                                                     const char* providedEventName,
                                                     double initialDelayInSeconds,
                                                     double intervalInSeconds,
                                                     void* callbackData,
                                                     void (*callback)(void* callbackData),
                                                     void* removedCallbackData,
                                                     void (*removedCallback)(void* removedCallbackData));

/**
 * @brief Retain the scheduled event by increasing the use count.
 * Will silently ignore a NULL event.
 */
void celix_scheduledEvent_retain(celix_scheduled_event_t* event);

/**
 * @brief Release the scheduled event by decreasing the use count. If the use count is 0,
 * the scheduled event is destroyed. Will silently ignore a NULL event.
 */
void celix_scheduledEvent_release(celix_scheduled_event_t* event);

/**
 * @brief Returns the scheduled event id.
 */
const char* celix_scheduledEvent_getName(const celix_scheduled_event_t* event);

/**
 * @brief Returns the scheduled event ID.
 */
long celix_scheduledEvent_getId(const celix_scheduled_event_t* event);

/**
 * @brief Returns the interval of the scheduled event in seconds.
 */
double celix_scheduledEvent_getIntervalInSeconds(const celix_scheduled_event_t* event);

/**
 * @brief Returns the bundle id of the bundle which created the scheduled event.
 */
long celix_scheduledEvent_getBundleId(const celix_scheduled_event_t* event);

/**
 * @brief Returns whether the event deadline is reached and the event should be processed.
 * @param[in] event The event to check.
 * @param[in] currentTime The current time.
 * @param[out] nextProcessTimeInSeconds The time in seconds until the next event should be processed.
 *                                      if the deadline is reached, this is the next interval.
 * @return true if the event deadline is reached and the event should be processed.
 */
bool celix_scheduledEvent_deadlineReached(celix_scheduled_event_t* event,
                                          const struct timespec* currentTime,
                                          double* nextProcessTimeInSeconds);

/**
 * @brief Process the event by calling the event callback.
 *
 * Must be called on the Celix event thread.
 *
 * @param[in] event The event to process.
 * @param[in] currentTime The current time.
 * @return The time in seconds until the next event should be processed.
 */
void celix_scheduledEvent_process(celix_scheduled_event_t* event, const struct timespec* currentTime);

/**
 * @brief Returns true if the event is a one-shot event and is done.
 * @param[in] event The event to check.
 * @return  true if the event is a one-shot event and is done.
 */
bool celix_scheduleEvent_isSingleShotDone(celix_scheduled_event_t* event);

/**
 * @brief Configure a scheduled event for a wakeup, so celix_scheduledEvent_deadlineReached will return true until
 * the event is processed.
 * 
 * @param[in] event The event to configure for wakeup.
 * @return The future call count of the event after the next processing is done.
 */
size_t celix_scheduledEvent_configureWakeup(celix_scheduled_event_t* event);

/**
 * @brief Wait for a scheduled event to reach at least the provided call count.
 * Will directly (non blocking) return if the call count is already reached or waitTimeInSeconds is <= 0
 * @param[in] event The event to wait for.
 * @param[in] callCount The call count to wait for.
 * @param[in] timeout The max time to wait in seconds.
 * @return CELIX_SUCCESS if the scheduled event reached the call count, CELIX_TIMEOUT if the scheduled event
 */
celix_status_t celix_scheduledEvent_waitForAtLeastCallCount(celix_scheduled_event_t* event,
                                                            size_t targetCallCount,
                                                            double waitTimeInSeconds);


/**
 * @brief Wait for a scheduled event to be done with processing.
 * @param[in] event The event to wait for.
 * @param[in] waitTimeInSeconds The max time to wait in seconds. Must be > 0.
 * @return CELIX_SUCCESS if the scheduled event is done with processing, CELIX_TIMEOUT if the scheduled event
 *         is not done with processing within the waitTimeInSeconds.
 */
celix_status_t celix_scheduledEvent_waitForProcessing(celix_scheduled_event_t* event);

#ifdef __cplusplus
};
#endif

#endif // CELIX_CELIX_SCHEDULED_EVENT_H