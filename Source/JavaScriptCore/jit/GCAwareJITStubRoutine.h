/*
 * Copyright (C) 2012-2021 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#if ENABLE(JIT)

#include "DFGCodeOriginPool.h"
#include "JITStubRoutine.h"
#include "JSObject.h"
#include "WriteBarrier.h"
#include <wtf/FixedVector.h>
#include <wtf/Hasher.h>
#include <wtf/Vector.h>

namespace JSC {

class AccessCase;
class CallLinkInfo;
class JITStubRoutineSet;
class OptimizingCallLinkInfo;

// Use this stub routine if you know that your code might be on stack when
// either GC or other kinds of stub deletion happen. Basicaly, if your stub
// routine makes calls (either to JS code or to C++ code) then you should
// assume that it's possible for that JS or C++ code to do something that
// causes the system to try to delete your routine. Using this routine type
// ensures that the actual deletion is delayed until the GC proves that the
// routine is no longer running. You can also subclass this routine if you
// want to mark additional objects during GC in those cases where the
// routine is known to be executing, or if you want to force this routine to
// keep other routines alive (for example due to the use of a slow-path
// list which does not get reclaimed all at once).
class GCAwareJITStubRoutine : public JITStubRoutine {
public:
    friend class JITStubRoutine;
    GCAwareJITStubRoutine(Type, const MacroAssemblerCodeRef<JITStubRoutinePtrTag>&, JSCell* owner);

    static Ref<JITStubRoutine> create(VM& vm, const MacroAssemblerCodeRef<JITStubRoutinePtrTag>& code, JSCell* owner)
    {
        auto stub = adoptRef(*new GCAwareJITStubRoutine(Type::GCAwareJITStubRoutineType, code, owner));
        stub->makeGCAware(vm);
        return stub;
    }

    void deleteFromGC();

    void makeGCAware(VM&);

    JSCell* owner() const { return m_owner; }
    
protected:
    void observeZeroRefCountImpl();

private:
    friend class JITStubRoutineSet;

    JSCell* m_owner { nullptr };
    bool m_mayBeExecuting : 1 { false };
    bool m_isJettisoned : 1 { false };
    bool m_ownerIsDead : 1 { false };
    bool m_isGCAware : 1 { false };
};

class PolymorphicAccessJITStubRoutine : public GCAwareJITStubRoutine {
public:
    using Base = GCAwareJITStubRoutine;
    friend class JITStubRoutine;

    PolymorphicAccessJITStubRoutine(Type, const MacroAssemblerCodeRef<JITStubRoutinePtrTag>&, VM&, FixedVector<RefPtr<AccessCase>>&&, FixedVector<StructureID>&&, JSCell* owner);

    const FixedVector<RefPtr<AccessCase>>& cases() const { return m_cases; }
    const FixedVector<StructureID>& weakStructures() const { return m_weakStructures; }

    unsigned hash() const
    {
        if (!m_hash)
            m_hash = computeHash(m_cases, m_weakStructures);
        return m_hash;
    }

    static unsigned computeHash(const FixedVector<RefPtr<AccessCase>>&, const FixedVector<StructureID>&);

protected:
    void observeZeroRefCountImpl();

private:
    VM& m_vm;
    FixedVector<RefPtr<AccessCase>> m_cases;
    FixedVector<StructureID> m_weakStructures;
};

// Use this if you want to mark one additional object during GC if your stub
// routine is known to be executing.
class MarkingGCAwareJITStubRoutine : public PolymorphicAccessJITStubRoutine {
public:
    using Base = PolymorphicAccessJITStubRoutine;
    friend class JITStubRoutine;

    MarkingGCAwareJITStubRoutine(Type, const MacroAssemblerCodeRef<JITStubRoutinePtrTag>&, VM&, FixedVector<RefPtr<AccessCase>>&&, FixedVector<StructureID>&&, JSCell* owner, const Vector<JSCell*>&, Bag<OptimizingCallLinkInfo>&&);

protected:
    template<typename Visitor> void markRequiredObjectsInternalImpl(Visitor&);
    void markRequiredObjectsImpl(AbstractSlotVisitor&);
    void markRequiredObjectsImpl(SlotVisitor&);

private:
    FixedVector<WriteBarrier<JSCell>> m_cells;
    Bag<OptimizingCallLinkInfo> m_callLinkInfos;
};


// The stub has exception handlers in it. So it clears itself from exception
// handling table when it dies. It also frees space in CodeOrigin table
// for new exception handlers to use the same DisposableCallSiteIndex.
class GCAwareJITStubRoutineWithExceptionHandler final : public MarkingGCAwareJITStubRoutine {
public:
    using Base = MarkingGCAwareJITStubRoutine;
    friend class JITStubRoutine;

    GCAwareJITStubRoutineWithExceptionHandler(const MacroAssemblerCodeRef<JITStubRoutinePtrTag>&, VM&, FixedVector<RefPtr<AccessCase>>&&, FixedVector<StructureID>&&, JSCell* owner, const Vector<JSCell*>&, Bag<OptimizingCallLinkInfo>&&, CodeBlock*, DisposableCallSiteIndex);
    ~GCAwareJITStubRoutineWithExceptionHandler();


private:
    void aboutToDieImpl()
    {
        m_codeBlockWithExceptionHandler = nullptr;
#if ENABLE(DFG_JIT)
        m_codeOriginPool = nullptr;
#endif
    }

    void observeZeroRefCountImpl();

    CodeBlock* m_codeBlockWithExceptionHandler;
#if ENABLE(DFG_JIT)
    RefPtr<DFG::CodeOriginPool> m_codeOriginPool;
#endif
    DisposableCallSiteIndex m_exceptionHandlerCallSiteIndex;
};

// Helper for easily creating a GC-aware JIT stub routine. For the varargs,
// pass zero or more JSCell*'s. This will either create a JITStubRoutine, a
// GCAwareJITStubRoutine, or an ObjectMarkingGCAwareJITStubRoutine as
// appropriate. Generally you only need to pass pointers that will be used
// after the first call to C++ or JS.
// 
// Ref<PolymorphicAccessJITStubRoutine> createICJITStubRoutine(
//    const MacroAssemblerCodeRef<JITStubRoutinePtrTag>& code,
//    VM& vm,
//    FixedVector<RefPtr<AccessCase>>&& cases,
//    JSCell* owner,
//    bool makesCalls,
//    ...);
//
// Note that we don't actually use C-style varargs because that leads to
// strange type-related problems. For example it would preclude us from using
// our custom of passing '0' as NULL pointer. Besides, when I did try to write
// this function using varargs, I ended up with more code than this simple
// way.

Ref<PolymorphicAccessJITStubRoutine> createICJITStubRoutine(
    const MacroAssemblerCodeRef<JITStubRoutinePtrTag>&, FixedVector<RefPtr<AccessCase>>&& cases, FixedVector<StructureID>&& weakStructures, VM&, JSCell* owner, bool makesCalls,
    const Vector<JSCell*>&, Bag<OptimizingCallLinkInfo>&& callLinkInfos,
    CodeBlock* codeBlockForExceptionHandlers, DisposableCallSiteIndex exceptionHandlingCallSiteIndex);

} // namespace JSC

#endif // ENABLE(JIT)
