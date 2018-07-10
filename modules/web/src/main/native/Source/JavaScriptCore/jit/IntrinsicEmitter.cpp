/*
 * Copyright (C) 2015-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(JIT)

#include "CCallHelpers.h"
#include "CallFrame.h"
#include "CodeBlock.h"
#include "IntrinsicGetterAccessCase.h"
#include "JSArrayBufferView.h"
#include "JSCJSValueInlines.h"
#include "JSCellInlines.h"
#include "PolymorphicAccess.h"
#include "StructureStubInfo.h"

namespace JSC {

typedef CCallHelpers::TrustedImm32 TrustedImm32;
typedef CCallHelpers::Imm32 Imm32;
typedef CCallHelpers::TrustedImmPtr TrustedImmPtr;
typedef CCallHelpers::ImmPtr ImmPtr;
typedef CCallHelpers::TrustedImm64 TrustedImm64;
typedef CCallHelpers::Imm64 Imm64;

bool IntrinsicGetterAccessCase::canEmitIntrinsicGetter(JSFunction* getter, Structure* structure)
{

    switch (getter->intrinsic()) {
    case TypedArrayByteOffsetIntrinsic:
    case TypedArrayByteLengthIntrinsic:
    case TypedArrayLengthIntrinsic: {
        TypedArrayType type = structure->classInfo()->typedArrayStorageType;

        if (!isTypedView(type))
            return false;

        return true;
    }
    case UnderscoreProtoIntrinsic: {
        auto getPrototypeMethod = structure->classInfo()->methodTable.getPrototype;
        MethodTable::GetPrototypeFunctionPtr defaultGetPrototype = JSObject::getPrototype;
        return getPrototypeMethod == defaultGetPrototype;
    }
    default:
        return false;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

void IntrinsicGetterAccessCase::emitIntrinsicGetter(AccessGenerationState& state)
{
    CCallHelpers& jit = *state.jit;
    JSValueRegs valueRegs = state.valueRegs;
    GPRReg baseGPR = state.baseGPR;
    GPRReg valueGPR = valueRegs.payloadGPR();

    switch (intrinsic()) {
    case TypedArrayLengthIntrinsic: {
        jit.load32(MacroAssembler::Address(state.baseGPR, JSArrayBufferView::offsetOfLength()), valueGPR);
        jit.boxInt32(valueGPR, valueRegs);
        state.succeed();
        return;
    }

    case TypedArrayByteLengthIntrinsic: {
        TypedArrayType type = structure()->classInfo()->typedArrayStorageType;

        jit.load32(MacroAssembler::Address(state.baseGPR, JSArrayBufferView::offsetOfLength()), valueGPR);

        if (elementSize(type) > 1) {
            // We can use a bitshift here since we TypedArrays cannot have byteLength that overflows an int32.
            jit.lshift32(valueGPR, Imm32(logElementSize(type)), valueGPR);
        }

        jit.boxInt32(valueGPR, valueRegs);
        state.succeed();
        return;
    }

    case TypedArrayByteOffsetIntrinsic: {
        GPRReg scratchGPR = state.scratchGPR;

        CCallHelpers::Jump notEmptyByteOffset = jit.branch32(
            MacroAssembler::Equal,
            MacroAssembler::Address(baseGPR, JSArrayBufferView::offsetOfMode()),
            TrustedImm32(WastefulTypedArray));

        jit.move(TrustedImmPtr(nullptr), valueGPR);
        CCallHelpers::Jump done = jit.jump();

        notEmptyByteOffset.link(&jit);

        // We need to load the butterfly before the vector because baseGPR and valueGPR
        // can be the same register.
        jit.loadPtr(MacroAssembler::Address(baseGPR, JSObject::butterflyOffset()), scratchGPR);
        jit.loadPtr(MacroAssembler::Address(baseGPR, JSArrayBufferView::offsetOfPoisonedVector()), valueGPR);
        CCallHelpers::Jump nullVector = jit.branchTestPtr(MacroAssembler::Zero, valueGPR);

#if ENABLE(POISON)
        auto typedArrayType = structure()->classInfo()->typedArrayStorageType;
        jit.xorPtr(TrustedImmPtr(JSArrayBufferView::poisonFor(typedArrayType)), valueGPR);
#endif
        jit.loadPtr(MacroAssembler::Address(scratchGPR, Butterfly::offsetOfArrayBuffer()), scratchGPR);
        jit.loadPtr(MacroAssembler::Address(scratchGPR, ArrayBuffer::offsetOfData()), scratchGPR);
        jit.subPtr(scratchGPR, valueGPR);

        nullVector.link(&jit);
        done.link(&jit);

        jit.boxInt32(valueGPR, valueRegs);
        state.succeed();
        return;
    }

    case UnderscoreProtoIntrinsic: {
        if (structure()->hasPolyProto())
            jit.loadValue(CCallHelpers::Address(baseGPR, offsetRelativeToBase(knownPolyProtoOffset)), valueRegs);
        else
            jit.moveValue(structure()->storedPrototype(), valueRegs);
        state.succeed();
        return;
    }

    default:
        break;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

} // namespace JSC

#endif // ENABLE(JIT)
