/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
#include "CryptoAlgorithmSHA256.h"

#if ENABLE(SUBTLE_CRYPTO)

#include "CryptoDigest.h"

namespace WebCore {

const char* const CryptoAlgorithmSHA256::s_name = "SHA-256";

CryptoAlgorithmSHA256::CryptoAlgorithmSHA256()
{
}

CryptoAlgorithmSHA256::~CryptoAlgorithmSHA256()
{
}

std::unique_ptr<CryptoAlgorithm> CryptoAlgorithmSHA256::create()
{
    return std::unique_ptr<CryptoAlgorithm>(new CryptoAlgorithmSHA256);
}

CryptoAlgorithmIdentifier CryptoAlgorithmSHA256::identifier() const
{
    return s_identifier;
}

void CryptoAlgorithmSHA256::digest(const CryptoAlgorithmParameters&, const CryptoOperationData& data, VectorCallback callback, VoidCallback failureCallback, ExceptionCode&)
{
    std::unique_ptr<CryptoDigest> digest = CryptoDigest::create(CryptoAlgorithmIdentifier::SHA_256);
    if (!digest) {
        failureCallback();
        return;
    }

    digest->addBytes(data.first, data.second);

    callback(digest->computeHash());
}

}

#endif // ENABLE(SUBTLE_CRYPTO)
