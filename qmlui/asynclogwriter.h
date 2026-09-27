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

    /** Calls shutdown(). Every message enqueued before this call is
     *  guaranteed to reach the sink before the destructor returns.
     *
     *  Note: destroying the writer does not make a pointer to it safe to use
     *  afterwards. A process-wide message handler that forwards to a writer
     *  must keep the writer alive for as long as the handler is installed -
     *  call shutdown() instead of deleting it (see qmlui/main.cpp). */
    ~AsyncLogWriter();

    AsyncLogWriter(const AsyncLogWriter &) = delete;
    AsyncLogWriter &operator=(const AsyncLogWriter &) = delete;

    /** Queue $msg for delivery to the sink. Safe to call from any thread, at
     *  any time before the destructor runs - including after, and
     *  concurrently with, shutdown(): once the worker thread has stopped,
     *  $msg is handed to the sink synchronously on the calling thread
     *  instead, so nothing logged late (e.g. from destructors during
     *  application teardown) is lost. */
    void enqueue(const QString &msg);

    /** Stops the background worker once its queue is drained, and joins it.
     *  Idempotent and safe to call from any thread; called from the worker
     *  itself (a sink that logs a fatal message) it only requests the stop
     *  and returns without joining.
     *  Messages enqueued while the worker is still draining are still
     *  delivered by it, in order; messages enqueued after it has stopped go
     *  straight to the sink (see enqueue()), so they always come after
     *  everything that was queued before. */
    void shutdown();

    /** True once the worker thread has drained its queue and exited, i.e.
     *  every further enqueue() is delivered synchronously. */
    bool isStopped();

private:
    void workerLoop();

    std::function<void(const QString &)> m_sink;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<QString> m_queue;
    /** Cleared by shutdown(): the worker exits once the queue is empty */
    bool m_running;
    /** Set by the worker, under m_mutex, as it exits with an empty queue */
    bool m_stopped;
    /** Serializes shutdown() callers around the join */
    std::mutex m_joinMutex;
    /** Serializes synchronous (post-shutdown) sink calls across threads */
    std::mutex m_syncSinkMutex;
    std::thread m_thread;
    /** m_thread's id, cached once: m_thread itself is modified by join() */
    std::thread::id m_workerId;
};

#endif // ASYNCLOGWRITER_H
