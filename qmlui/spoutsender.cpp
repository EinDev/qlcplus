/*
  Q Light Controller Plus
  spoutsender.cpp

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

#include "spoutsender.h"

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)

#include <QDebug>

// Qt headers first: SpoutDX.h drags in <windows.h>, whose min/max/interface
// macros are best kept away from everything else. This is the only project
// translation unit that sees the SDK headers.
#include "SpoutDX.h"

SpoutSender::SpoutSender()
    : m_dx(new spoutDX())
{
}

SpoutSender::~SpoutSender()
{
    release();
}

bool SpoutSender::create(const QString &name, int width, int height)
{
    if (isCreated())
        release();

    if (name.isEmpty() || width <= 0 || height <= 0)
    {
        qWarning() << "[Spout] refusing to create sender with empty name or invalid size"
                   << name << width << height;
        return false;
    }

    // Spout sender names are plain 8-bit strings (max 256 bytes) shared via
    // memory-mapped files between processes.
    if (m_dx->SetSenderName(name.toLocal8Bit().constData()) == false)
    {
        qWarning() << "[Spout] SetSenderName failed for" << name;
        return false;
    }

    // No device argument: spoutDX creates and owns its own D3D11 device.
    if (m_dx->OpenDirectX11() == false)
    {
        qWarning() << "[Spout] OpenDirectX11 failed, no D3D11 device for sender" << name;
        return false;
    }

    m_size = QSize(width, height);
    m_transparent = QImage();

    // The shared texture and the sender registration are created lazily by
    // spoutDX on the first SendImage, so publish a transparent frame now.
    sendTransparent();

    if (m_dx->IsInitialized() == false)
    {
        qWarning() << "[Spout] sender" << name << "did not initialize after the first frame";
        m_dx->CloseDirectX11();
        m_size = QSize();
        return false;
    }

    return true;
}

void SpoutSender::sendImage(const QImage &image)
{
    if (m_dx->IsInitialized() == false && m_size.isValid() == false)
        return;

    if (image.isNull())
        return;

    // BGRA in memory, matching the DXGI_FORMAT_B8G8R8A8_UNORM shared texture
    // (see the header). convertToFormat is a no-op copy-on-write when the
    // format already matches.
    const QImage frame = image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (m_dx->SendImage(frame.constBits(),
                        (unsigned int)frame.width(),
                        (unsigned int)frame.height(),
                        (unsigned int)frame.bytesPerLine()) == false)
    {
        qWarning() << "[Spout] SendImage failed for sender" << name();
        return;
    }

    // spoutDX resizes the sender to whatever it was last given
    m_size = QSize((int)m_dx->GetWidth(), (int)m_dx->GetHeight());
}

void SpoutSender::sendTransparent()
{
    if (m_size.isValid() == false)
        return;

    if (m_transparent.size() != m_size)
    {
        m_transparent = QImage(m_size, QImage::Format_ARGB32_Premultiplied);
        m_transparent.fill(Qt::transparent); // all four bytes zero, premultiplied
    }

    sendImage(m_transparent);
}

void SpoutSender::release()
{
    if (m_dx->IsInitialized())
        m_dx->ReleaseSender();

    m_dx->CloseDirectX11();
    m_size = QSize();
    m_transparent = QImage();
}

bool SpoutSender::isCreated() const
{
    return m_dx->IsInitialized();
}

QString SpoutSender::name() const
{
    if (m_dx->IsInitialized() == false)
        return QString();

    return QString::fromLocal8Bit(m_dx->GetName());
}

QSize SpoutSender::size() const
{
    if (m_dx->IsInitialized() == false)
        return QSize();

    return m_size;
}

QStringList SpoutSender::activeSenders() const
{
    QStringList list;
    for (const std::string &s : m_dx->GetSenderList())
        list << QString::fromLocal8Bit(s.c_str());
    return list;
}

#endif // Q_OS_WIN && QLC_SPOUT
