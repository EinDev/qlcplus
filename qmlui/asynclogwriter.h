/*
  Q Light Controller Plus
  asynclogwriter.h

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

#ifndef ASYNCLOGWRITER_H
#define ASYNCLOGWRITER_H

#include <QString>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

/** Queues messages on the calling thread and hands them, in enqueue order,
 *  to a sink function invoked from a single dedicated background thread.
 *
 *  Used to keep qInstallMessageHandler() (which is very often called from the
 *  UI/render thread via qDebug()/QML console.log()) from blocking on flushed
 *  file/stderr I/O for every single log line. The sink itself can be
 *  anything - production code hands it real file+stderr I/O, tests can hand
 *  it something that just records into a vector. */
class AsyncLogWriter
{
public:
    /** Starts the background worker thread immediately. $sink is invoked,
     *  from the worker thread only, once per enqueued message, in the order
     *  messages were enqueued. */
    explicit AsyncLogWriter(std::function<void(const QString &)> sink);

    /** Signals the worker to stop once its queue is drained, and joins it.
     *  Every message enqueued before this call is guaranteed to reach the
     *  sink before the destructor returns. */
    ~AsyncLogWriter();

    AsyncLogWriter(const AsyncLogWriter &) = delete;
    AsyncLogWriter &operator=(const AsyncLogWriter &) = delete;

    /** Queue $msg for delivery to the sink. Safe to call from any thread. */
    void enqueue(const QString &msg);

private:
    void workerLoop();

    std::function<void(const QString &)> m_sink;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<QString> m_queue;
    bool m_running;
    std::thread m_thread;
};

#endif // ASYNCLOGWRITER_H
