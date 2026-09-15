/*
  Q Light Controller Plus
  spoutsender.h

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

#ifndef SPOUTSENDER_H
#define SPOUTSENDER_H

#include <QtGlobal>

// Spout is a Windows-only (DirectX 11) texture sharing framework. The
// vendored SDK subset in qmlui/spout/ only builds on WIN32 and qmlui's
// CMakeLists defines QLC_SPOUT there and nowhere else, so this whole class
// is compiled out on Linux/macOS/Android.
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>

class spoutDX;

/**
 * Thin RAII wrapper around the Spout SDK's DirectX 11 sender (spoutDX).
 *
 * A SpoutSender publishes a stream of BGRA frames under a name that any
 * Spout receiver on the same machine (OBS via the obs-spout2 plugin,
 * Resolume, TouchDesigner, ...) can pick up. It owns its own D3D11 device
 * (created by spoutDX::OpenDirectX11 with no external device), so it is
 * completely independent of Qt's OpenGL/RHI render thread and can be
 * driven from plain C++ with QImage frames.
 *
 * Pixel format / alpha
 * --------------------
 * Frames are sent as QImage::Format_ARGB32_Premultiplied. On a little-endian
 * machine that format is B,G,R,A byte order in memory, which is exactly the
 * DXGI_FORMAT_B8G8R8A8_UNORM texture spoutDX creates by default, so
 * spoutDX::SendImage uploads the QImage bits as-is (UpdateSubresource with
 * the QImage's own bytesPerLine as pitch) - there is no channel swizzle.
 * Any other QImage format passed to sendImage() is converted first.
 *
 * Because the buffer is premultiplied, the receiving side has to composite
 * it as premultiplied alpha. In OBS, on the Spout2 Capture source, set
 * "Composite Mode" to "Premultiplied Alpha" (the default "Opaque" ignores
 * alpha entirely and "Straight Alpha" would darken semi-transparent edges).
 * A frame sent with sendTransparent() (every byte zero) then renders as
 * fully transparent, and an opaque frame (alpha 255 everywhere) as-is.
 *
 * Threading
 * ---------
 * spoutDX's D3D11 immediate context is not thread-safe. Every method of a
 * given SpoutSender instance must be called from the same thread (the one
 * that called create()).
 *
 * Sender names
 * ------------
 * Spout sender names are global per machine. If a sender with the requested
 * name already exists, spoutDX silently registers this one as "name_1",
 * "name_2", ... - name() returns the name actually registered, which may
 * therefore differ from what was passed to create().
 */
class SpoutSender
{
public:
    SpoutSender();
    ~SpoutSender();

    SpoutSender(const SpoutSender &) = delete;
    SpoutSender &operator=(const SpoutSender &) = delete;

    /**
     * Create the D3D11 device and register a sender called $name of the
     * given size. Sends one fully transparent frame as part of creation:
     * that first frame is what actually allocates the shared texture and
     * publishes the sender, so receivers see it immediately (transparent)
     * rather than only after the first real frame.
     *
     * If this instance already has a sender, it is released first.
     *
     * @return true if the sender is registered and ready, false otherwise
     */
    bool create(const QString &name, int width, int height);

    /**
     * Send one frame. The image is converted to
     * QImage::Format_ARGB32_Premultiplied if it isn't already.
     *
     * The image is sent at its own size; if that differs from the current
     * sender size, spoutDX resizes the shared texture and the sender
     * (receivers re-initialize on such a change), and size() follows.
     * Does nothing if the sender was not created or the image is null.
     */
    void sendImage(const QImage &image);

    /** Send a fully transparent (all-zero BGRA) frame of the sender size */
    void sendTransparent();

    /** Unregister the sender and release the D3D11 device. Safe to call
     *  when nothing was created. */
    void release();

    /** true between a successful create() and release() */
    bool isCreated() const;

    /** The registered sender name (see class notes: may carry a _N suffix),
     *  empty when not created */
    QString name() const;

    /** The current sender size, invalid when not created */
    QSize size() const;

    /** Names of every Spout sender currently registered on this machine
     *  (from any process), for diagnostics */
    QStringList activeSenders() const;

private:
    std::unique_ptr<spoutDX> m_dx;
    QSize m_size;
    QImage m_transparent;
};

#endif // Q_OS_WIN && QLC_SPOUT

#endif // SPOUTSENDER_H
