/*
 * Copyright (C) 2008-2016 Apple Inc. All rights reserved.
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
#include "GradientImage.h"

#include "GraphicsContext.h"
#include "ImageBuffer.h"

namespace WebCore {

GradientImage::GradientImage(Gradient& generator, const FloatSize& size)
    : m_gradient(generator)
{
    setContainerSize(size);
}

GradientImage::~GradientImage()
{
}

void GradientImage::draw(GraphicsContext& destContext, const FloatRect& destRect, const FloatRect& srcRect, CompositeOperator compositeOp, BlendMode blendMode, ImageOrientationDescription)
{
    GraphicsContextStateSaver stateSaver(destContext);
    destContext.setCompositeOperation(compositeOp, blendMode);
    destContext.clip(destRect);
    destContext.translate(destRect.x(), destRect.y());
    if (destRect.size() != srcRect.size())
        destContext.scale(FloatSize(destRect.width() / srcRect.width(), destRect.height() / srcRect.height()));
    destContext.translate(-srcRect.x(), -srcRect.y());
    destContext.fillRect(FloatRect(FloatPoint(), size()), m_gradient.get());
}

void GradientImage::drawPattern(GraphicsContext& destContext, const FloatRect& destRect, const FloatRect& srcRect, const AffineTransform& patternTransform,
    const FloatPoint& phase, const FloatSize& spacing, CompositeOperator compositeOp, BlendMode blendMode)
{
    // Allow the generator to provide visually-equivalent tiling parameters for better performance.
    FloatSize adjustedSize = size();
    FloatRect adjustedSrcRect = srcRect;
    m_gradient->adjustParametersForTiledDrawing(adjustedSize, adjustedSrcRect, spacing);

    // Factor in the destination context's scale to generate at the best resolution
    AffineTransform destContextCTM = destContext.getCTM(GraphicsContext::DefinitelyIncludeDeviceScale);
    double xScale = fabs(destContextCTM.xScale());
    double yScale = fabs(destContextCTM.yScale());
    AffineTransform adjustedPatternCTM = patternTransform;
    adjustedPatternCTM.scale(1.0 / xScale, 1.0 / yScale);
    adjustedSrcRect.scale(xScale, yScale);

    unsigned generatorHash = m_gradient->hash();

    if (!m_cachedImageBuffer || m_cachedGeneratorHash != generatorHash || m_cachedAdjustedSize != adjustedSize || !m_cachedImageBuffer->isCompatibleWithContext(destContext)) {
        m_cachedImageBuffer = ImageBuffer::createCompatibleBuffer(adjustedSize, ColorSpaceSRGB, destContext);
        if (!m_cachedImageBuffer)
            return;

        // Fill with the generated image.
        m_cachedImageBuffer->context().fillRect(FloatRect(FloatPoint(), adjustedSize), m_gradient.get());

        m_cachedGeneratorHash = generatorHash;
        m_cachedAdjustedSize = adjustedSize;

        if (destContext.drawLuminanceMask())
            m_cachedImageBuffer->convertToLuminanceMask();
    }

    destContext.setDrawLuminanceMask(false);

    // Tile the image buffer into the context.
    m_cachedImageBuffer->drawPattern(destContext, destRect, adjustedSrcRect, adjustedPatternCTM, phase, spacing, compositeOp, blendMode);
}

void GradientImage::dump(TextStream& ts) const
{
    GeneratedImage::dump(ts);
    // FIXME: dump the gradient.
}

}
