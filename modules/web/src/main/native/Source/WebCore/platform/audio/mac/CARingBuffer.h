/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEB_AUDIO) && USE(MEDIATOOLBOX)

#include "AudioStreamDescription.h"
#include "CAAudioStreamDescription.h"
#include <runtime/ArrayBuffer.h>
#include <wtf/Lock.h>
#include <wtf/Vector.h>

typedef struct AudioBufferList AudioBufferList;

namespace WebCore {

class CARingBuffer {
public:
    WEBCORE_EXPORT CARingBuffer();
    WEBCORE_EXPORT ~CARingBuffer()
    {
        deallocate();
    }

    enum Error {
        Ok,
        TooMuch, // fetch start time is earlier than buffer start time and fetch end time is later than buffer end time
    };

    WEBCORE_EXPORT void allocate(const CAAudioStreamDescription&, size_t);
    WEBCORE_EXPORT void deallocate();

    WEBCORE_EXPORT Error store(const AudioBufferList*, size_t frameCount, uint64_t startFrame);

    enum FetchMode { Copy, Mix };
    WEBCORE_EXPORT Error fetch(AudioBufferList*, size_t frameCount, uint64_t startFrame, FetchMode mode = Copy);

    WEBCORE_EXPORT void flush();

    WEBCORE_EXPORT void getCurrentFrameBounds(uint64_t &startTime, uint64_t &endTime);

    uint32_t channelCount() const { return m_channelCount; }

private:
    void allocate(uint32_t m_channelCount, size_t bytesPerFrame, size_t frameCount);
    size_t frameOffset(uint64_t frameNumber) { return (frameNumber & m_frameCountMask) * m_bytesPerFrame; }

    void clipTimeBounds(uint64_t& startRead, uint64_t& endRead);

    uint64_t currentStartFrame() const;
    uint64_t currentEndFrame() const;
    void setCurrentFrameBounds(uint64_t startFrame, uint64_t endFrame);

    RefPtr<ArrayBuffer> m_buffers;
    uint32_t m_channelCount { 0 };
    size_t m_bytesPerFrame { 0 };
    uint32_t m_frameCount { 0 };
    uint32_t m_frameCountMask { 0 };
    size_t m_capacityBytes { 0 };

    struct TimeBounds {
        TimeBounds()
            : m_startFrame(0)
            , m_endFrame(0)
            , m_updateCounter(0)
        {
        }
        volatile uint64_t m_startFrame;
        volatile uint64_t m_endFrame;
        volatile uint32_t m_updateCounter;
    };

    CAAudioStreamDescription m_description;
    Vector<TimeBounds> m_timeBoundsQueue;
    Lock m_currentFrameBoundsLock;
    std::atomic<int32_t> m_timeBoundsQueuePtr { 0 };
};

}

#endif // ENABLE(WEB_AUDIO) && USE(MEDIATOOLBOX)
