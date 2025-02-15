/*
 * Copyright (C) 2021-2023 Apple Inc. All rights reserved.
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

#include "config.h"
#include "GPUBuffer.h"

#include "GPUDevice.h"
#include "JSDOMPromiseDeferred.h"
#include "JSGPUBufferMapState.h"

namespace WebCore {

GPUBuffer::~GPUBuffer()
{
    m_bufferSize = 0;
    m_backing->destroy();
    m_arrayBuffer = nullptr;
}

GPUBuffer::GPUBuffer(Ref<WebGPU::Buffer>&& backing, size_t bufferSize, GPUBufferUsageFlags usage, bool mappedAtCreation, GPUDevice& device)
    : m_backing(WTFMove(backing))
    , m_bufferSize(bufferSize)
    , m_usage(usage)
    , m_mapState(mappedAtCreation ? GPUBufferMapState::Mapped : GPUBufferMapState::Unmapped)
    , m_device(device)
{
}

String GPUBuffer::label() const
{
    return m_backing->label();
}

void GPUBuffer::setLabel(String&& label)
{
    m_backing->setLabel(WTFMove(label));
}

void GPUBuffer::mapAsync(GPUMapModeFlags mode, std::optional<GPUSize64> offset, std::optional<GPUSize64> size, MapAsyncPromise&& promise)
{
    if (!m_bufferSize || (size.has_value() && !size.value())) {
        promise.resolve(nullptr);
        return;
    }

    if (m_pendingMapPromise) {
        promise.reject(Exception { ExceptionCode::OperationError });
        return;
    }

    if (m_mapState == GPUBufferMapState::Unmapped)
        m_mapState = GPUBufferMapState::Pending;

    m_pendingMapPromise = promise;
    // FIXME: Should this capture a weak pointer to |this| instead?
    m_backing->mapAsync(convertMapModeFlagsToBacking(mode), offset.value_or(0), size, [promise = WTFMove(promise), protectedThis = Ref { *this }](bool success) mutable {
        if (!protectedThis->m_pendingMapPromise)
            return;

        protectedThis->m_pendingMapPromise = std::nullopt;
        if (success) {
            protectedThis->m_mapState = GPUBufferMapState::Mapped;
            promise.resolve(nullptr);
        } else {
            if (protectedThis->m_mapState == GPUBufferMapState::Pending)
                protectedThis->m_mapState = GPUBufferMapState::Unmapped;
            promise.reject(Exception { ExceptionCode::OperationError });
        }
    });
}

static auto makeArrayBuffer(auto source, auto byteLength, auto& cachedArrayBuffer, auto& device, auto& buffer)
{
    auto arrayBuffer = ArrayBuffer::create(source, byteLength);
    cachedArrayBuffer = arrayBuffer.ptr();
    cachedArrayBuffer->pin();
    device.addBufferToUnmap(buffer);
    return arrayBuffer;
}

ExceptionOr<Ref<JSC::ArrayBuffer>> GPUBuffer::getMappedRange(std::optional<GPUSize64> offset, std::optional<GPUSize64> size)
{
    if (!m_bufferSize || (size.has_value() && !size.value()))
        return makeArrayBuffer(static_cast<size_t>(0U), 1, m_arrayBuffer, m_device, *this);

    // size is <= the size of the buffer is validated in WebGPU.framework
    m_mappedRange = m_backing->getMappedRange(offset.value_or(0), size);
    if (!m_mappedRange.source) {
        m_arrayBuffer = nullptr;
        return Exception { ExceptionCode::OperationError };
    }

    return makeArrayBuffer(m_mappedRange.source, m_mappedRange.byteLength, m_arrayBuffer, m_device, *this);
}

void GPUBuffer::unmap(ScriptExecutionContext& scriptExecutionContext)
{
    internalUnmap(scriptExecutionContext);
    m_device.removeBufferToUnmap(*this);
}

void GPUBuffer::internalUnmap(ScriptExecutionContext& scriptExecutionContext)
{
    if (m_pendingMapPromise) {
        m_pendingMapPromise->reject(Exception { ExceptionCode::AbortError });
        m_pendingMapPromise = std::nullopt;
    }

    m_mapState = GPUBufferMapState::Unmapped;
    if (!m_bufferSize)
        return;

    if (m_arrayBuffer && m_arrayBuffer->data()) {
        memcpy(m_mappedRange.source, m_arrayBuffer->data(), m_mappedRange.byteLength);
        JSC::ArrayBufferContents emptyBuffer;
        m_arrayBuffer->unpin();
        m_arrayBuffer->transferTo(scriptExecutionContext.vm(), emptyBuffer);
    }

    m_arrayBuffer = nullptr;
    m_backing->unmap();
}

void GPUBuffer::destroy(ScriptExecutionContext& scriptExecutionContext)
{
    if (!m_bufferSize)
        return;

    internalUnmap(scriptExecutionContext);
    m_bufferSize = 0;
    m_backing->destroy();
}

}
