/*
  Q Light Controller Plus - Unit test
  asynclogwriter_test.cpp

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

#include <QtTest/QtTest>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "asynclogwriter.h"
#include "asynclogwriter_test.h"

namespace {

/** Thread-safe recording sink: records every message handed to it (from
 *  AsyncLogWriter's single worker thread) and lets the test thread block
 *  until a target count has been recorded, instead of guessing with a fixed
 *  sleep - the wait returns as soon as the message actually arrives. */
class RecordingSink
{
public:
    void operator()(const QString &msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(msg);
        m_cv.notify_all();
    }

    /** Blocks until at least $count messages have been recorded, or
     *  $timeoutMs elapses. Returns the current snapshot either way. */
    std::vector<QString> waitForCount(size_t count, int timeoutMs = 5000)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                      [&] { return m_messages.size() >= count; });
        return m_messages;
    }

    std::vector<QString> snapshot()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_messages;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<QString> m_messages;
};

} // namespace

void AsyncLogWriter_Test::deliversInOrder()
{
    RecordingSink sink;
    AsyncLogWriter writer(std::ref(sink));

    QStringList expected;
    for (int i = 0; i < 50; i++)
        expected << QString("line-%1").arg(i);

    for (const QString &line : std::as_const(expected))
        writer.enqueue(line);

    std::vector<QString> received = sink.waitForCount(expected.size());

    QCOMPARE(int(received.size()), expected.size());
    for (int i = 0; i < expected.size(); i++)
        QCOMPARE(received[i], expected.at(i));
}

void AsyncLogWriter_Test::concurrentEnqueueLosesNothing()
{
    RecordingSink sink;
    AsyncLogWriter writer(std::ref(sink));

    const int threadCount = 8;
    const int perThread = 100;
    const int total = threadCount * perThread;

    QVector<QStringList> expectedPerThread(threadCount);
    std::vector<std::thread> threads;

    for (int t = 0; t < threadCount; t++)
    {
        QStringList lines;
        for (int i = 0; i < perThread; i++)
            lines << QString("t%1-%2").arg(t).arg(i);
        expectedPerThread[t] = lines;
    }

    for (int t = 0; t < threadCount; t++)
    {
        threads.emplace_back([&writer, &expectedPerThread, t] {
            for (const QString &line : std::as_const(expectedPerThread[t]))
                writer.enqueue(line);
        });
    }

    for (std::thread &th : threads)
        th.join();

    std::vector<QString> received = sink.waitForCount(total);
    QCOMPARE(int(received.size()), total);

    QStringList expectedAll;
    for (int t = 0; t < threadCount; t++)
        expectedAll << expectedPerThread[t];

    QStringList receivedAll;
    for (const QString &msg : received)
        receivedAll << msg;

    std::sort(expectedAll.begin(), expectedAll.end());
    std::sort(receivedAll.begin(), receivedAll.end());

    // Cross-thread ordering isn't guaranteed, but nothing should be dropped
    // or duplicated: as sorted multisets, both lists must match exactly.
    QCOMPARE(receivedAll, expectedAll);
}

void AsyncLogWriter_Test::destructorDrainsQueueBeforeReturning()
{
    RecordingSink sink;

    QStringList expected;
    for (int i = 0; i < 200; i++)
        expected << QString("shutdown-%1").arg(i);

    {
        AsyncLogWriter writer(std::ref(sink));
        for (const QString &line : std::as_const(expected))
            writer.enqueue(line);
        // Destructor runs here: must signal stop, drain whatever is still
        // queued, and join - all before this scope actually exits.
    }

    std::vector<QString> received = sink.snapshot();
    QCOMPARE(int(received.size()), expected.size());
    for (int i = 0; i < expected.size(); i++)
        QCOMPARE(received[i], expected.at(i));
}

QTEST_APPLESS_MAIN(AsyncLogWriter_Test)
