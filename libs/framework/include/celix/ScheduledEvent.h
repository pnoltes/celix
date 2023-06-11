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

#pragma once

#include "celix_bundle_context.h"

namespace celix {

/**
 * @brief A C++ abstraction for a scheduled event in Celix.
 *
 * A scheduled event is an event that is scheduled to be executed at a certain initial delay and/or interval.
 * A new scheduld event should be created using celix::BundleContext::createScheduledEvent.
 *
 * This class uses RAII to automatically remove the (non one-shot) scheduled event from the bundle context
 * when it is destroyed. For one-shot scheduled events, the destructor will not remove the scheduled event.
 */
class ScheduledEvent final {
  public:
    friend class ScheduledEventBuilder;

    /**
     * @brief Constructs a empty / not-active scheduled event.
     */
    ScheduledEvent() = default;

    /**
     * @brief Destroys the scheduled event by removes it from the bundle context if it is not a one-short event.
     */
    ~ScheduledEvent() noexcept {
        if (!isOneShot) {
            cancel();
        }
    }

    ScheduledEvent(const ScheduledEvent&) = delete;
    ScheduledEvent& operator=(const ScheduledEvent&) = delete;

    ScheduledEvent(ScheduledEvent&&) noexcept = default;
    ScheduledEvent& operator=(ScheduledEvent&&) noexcept = default;

    /**
     * @brief Cancels the scheduled event. Can be called multiple times. When this function returns, no more scheduled
     * event callbacks will be called and, if configured, the remove callback is called.
     *
     */
    void cancel() {
        if (ctx) {
            celix_bundleContext_tryRemoveScheduledEvent(ctx.get(), eventId);
        }
    }

    /**
     * @brief Wakes up the scheduled event and returns immediately, not waiting for the scheduled event callback to be
     * called.
     */
    void wakeup() { wakeup(std::chrono::duration<double>{0}); }

    /**
     * @brief Wakes up the scheduled event with an optional wait time.
     *
     * If `waitTime` is not zero, this function will block until the scheduled event callback is called or the
     * `waitTime` duration has elapsed. If `waitTime` is zero, this function will return immediately.
     *
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param[in] waitTime The wait time duration (default is zero).
     * @return true if the scheduled event was woken up, false if a timeout occurred.
     */
    template <typename Rep, typename Period>
    bool wakeup(std::chrono::duration<Rep, Period> waitTime) {
        double waitTimeInSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(waitTime).count();
        celix_status_t status = CELIX_SUCCESS;
        if (ctx) {
            status = celix_bundleContext_wakeupScheduledEvent(ctx.get(), eventId, waitTimeInSeconds);
        }
        return status == CELIX_SUCCESS;
    }

  private:
    struct Callbacks {
        std::function<void()> callback{};              /**< The callback for the scheduled event. */
        std::function<void()> removeCallback{};        /**< The remove callback for the scheduled event. */
    };

    /**
     * @brief Constructs a scheduled event using the given bundle context and options.
     *
     * @param[in] ctx The bundle context to use.
     * @param[in] options The options for the scheduled event.
     */
    ScheduledEvent(std::shared_ptr<celix_bundle_context_t> _cCtx,
                   const std::string& _name,
                   std::function<void()> _callback,
                   std::function<void()> _removeCallback,
                   celix_scheduled_event_options_t& options) {
        ctx = std::move(_cCtx);
        isOneShot = options.intervalInSeconds == 0;
        options.name = _name.c_str();
        auto* callbacks = new Callbacks{std::move(_callback), std::move(_removeCallback)};
        options.callbackData = callbacks;
        options.callback = [](void* data) {
            auto* callbacks = static_cast<Callbacks*>(data);
            (callbacks->callback)();
        };
        options.removeCallbackData = callbacks;
        options.removeCallback = [](void* data) {
            auto* callbacks = static_cast<Callbacks*>(data);
            if (callbacks->removeCallback) {
                (callbacks->removeCallback)();
            }
            delete callbacks;
        };

        eventId = celix_bundleContext_scheduleEvent(ctx.get(), &options);
    }

    std::shared_ptr<celix_bundle_context_t> ctx{}; /**< The bundle context for the scheduled event. */
    long eventId{-1};                              /**< The ID of the scheduled event. */
    bool isOneShot{false};                         /**< Whether the scheduled event is a one-shot event. */
};

} // end namespace celix