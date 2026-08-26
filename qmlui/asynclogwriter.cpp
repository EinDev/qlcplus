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
    , m_thread(&AsyncLogWriter::workerLoop, this)
{
}

AsyncLogWriter::~AsyncLogWriter()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

void AsyncLogWriter::enqueue(const QString &msg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(msg);
    }
    m_cv.notify_one();
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

        if (!m_running)
            break;
    }
}
