/*
  Q Light Controller Plus
  asynclogwriter.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "asynclogwriter.h"

AsyncLogWriter::AsyncLogWriter(std::function<void(const QString &)> sink)
    : m_sink(std::move(sink))
    , m_running(true)
    , m_stopped(false)
    , m_thread(&AsyncLogWriter::workerLoop, this)
    , m_workerId(m_thread.get_id())
{
}

AsyncLogWriter::~AsyncLogWriter()
{
    shutdown();
}

void AsyncLogWriter::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_one();

    // Called from the worker itself (the sink logged a fatal message):
    // joining would deadlock. The worker still exits once its queue is empty.
    if (std::this_thread::get_id() == m_workerId)
        return;

    std::lock_guard<std::mutex> joinLock(m_joinMutex);
    if (m_thread.joinable())
        m_thread.join();
}

bool AsyncLogWriter::isStopped()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopped;
}

void AsyncLogWriter::enqueue(const QString &msg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped == false)
        {
            // The worker is alive (possibly already asked to stop, but it
            // only exits with an empty queue), so it will deliver this.
            m_queue.push(msg);
            m_cv.notify_one();
            return;
        }
    }

    // The worker has drained everything and exited: write synchronously.
    // Not under m_mutex, so a sink that itself logs cannot self-deadlock on
    // it; m_syncSinkMutex only keeps concurrent late writers from
    // interleaving inside the sink.
    std::lock_guard<std::mutex> sinkLock(m_syncSinkMutex);
    m_sink(msg);
}

void AsyncLogWriter::workerLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true)
    {
        m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });

        while (!m_queue.empty())
        {
            QString msg = m_queue.front();
            m_queue.pop();

            lock.unlock();
            m_sink(msg);
            lock.lock();
        }

        // Exit only with the lock held and the queue empty, and flag it in
        // the same critical section: from here on enqueue() writes
        // synchronously, so no message can be pushed that nobody drains.
        if (!m_running)
        {
            m_stopped = true;
            break;
        }
    }
}
