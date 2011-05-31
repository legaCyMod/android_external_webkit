/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
#include "DrawingAreaImpl.h"

#include "DrawingAreaProxyMessages.h"
#include "LayerTreeContext.h"
#include "ShareableBitmap.h"
#include "UpdateInfo.h"
#include "WebPage.h"
#include "WebPageCreationParameters.h"
#include "WebProcess.h"
#include <WebCore/GraphicsContext.h>

#if !PLATFORM(MAC) && !PLATFORM(WIN)
#error "This drawing area is not ready for use by other ports yet."
#endif

using namespace WebCore;

namespace WebKit {

static uint64_t generateSequenceNumber()
{
    static uint64_t sequenceNumber;
    return ++sequenceNumber;
}

PassRefPtr<DrawingAreaImpl> DrawingAreaImpl::create(WebPage* webPage, const WebPageCreationParameters& parameters)
{
    return adoptRef(new DrawingAreaImpl(webPage, parameters));
}

DrawingAreaImpl::~DrawingAreaImpl()
{
    if (m_layerTreeHost)
        m_layerTreeHost->invalidate();
}

DrawingAreaImpl::DrawingAreaImpl(WebPage* webPage, const WebPageCreationParameters& parameters)
    : DrawingArea(DrawingAreaInfo::Impl, parameters.drawingAreaInfo.identifier, webPage)
    , m_inSetSize(false)
    , m_isWaitingForDidUpdate(false)
    , m_isPaintingSuspended(!parameters.isVisible)
    , m_displayTimer(WebProcess::shared().runLoop(), this, &DrawingAreaImpl::display)
    , m_exitCompositingTimer(WebProcess::shared().runLoop(), this, &DrawingAreaImpl::exitAcceleratedCompositingMode)
{
}

void DrawingAreaImpl::setNeedsDisplay(const IntRect& rect)
{
    IntRect dirtyRect = rect;
    dirtyRect.intersect(m_webPage->bounds());

    if (dirtyRect.isEmpty())
        return;

    if (m_layerTreeHost) {
        ASSERT(m_dirtyRegion.isEmpty());

        m_layerTreeHost->setNonCompositedContentsNeedDisplay(dirtyRect);
        return;
    }
    
    m_dirtyRegion.unite(dirtyRect);
    scheduleDisplay();
}

void DrawingAreaImpl::scroll(const IntRect& scrollRect, const IntSize& scrollOffset)
{
    if (m_layerTreeHost) {
        ASSERT(m_scrollRect.isEmpty());
        ASSERT(m_scrollOffset.isEmpty());
        ASSERT(m_dirtyRegion.isEmpty());

        m_layerTreeHost->scrollNonCompositedContents(scrollRect, scrollOffset);
        return;
    }

    if (!m_scrollRect.isEmpty() && scrollRect != m_scrollRect) {
        unsigned scrollArea = scrollRect.width() * scrollRect.height();
        unsigned currentScrollArea = m_scrollRect.width() * m_scrollRect.height();

        if (currentScrollArea >= scrollArea) {
            // The rect being scrolled is at least as large as the rect we'd like to scroll.
            // Go ahead and just invalidate the scroll rect.
            setNeedsDisplay(scrollRect);
            return;
        }

        // Just repaint the entire current scroll rect, we'll scroll the new rect instead.
        setNeedsDisplay(m_scrollRect);
        m_scrollRect = IntRect();
        m_scrollOffset = IntSize();
    }

    // Get the part of the dirty region that is in the scroll rect.
    Region dirtyRegionInScrollRect = intersect(scrollRect, m_dirtyRegion);
    if (!dirtyRegionInScrollRect.isEmpty()) {
        // There are parts of the dirty region that are inside the scroll rect.
        // We need to subtract them from the region, move them and re-add them.
        m_dirtyRegion.subtract(scrollRect);

        // Move the dirty parts.
        Region movedDirtyRegionInScrollRect = intersect(translate(dirtyRegionInScrollRect, scrollOffset), scrollRect);

        // And add them back.
        m_dirtyRegion.unite(movedDirtyRegionInScrollRect);
    } 
    
    // Compute the scroll repaint region.
    Region scrollRepaintRegion = subtract(scrollRect, translate(scrollRect, scrollOffset));

    m_dirtyRegion.unite(scrollRepaintRegion);

    m_scrollRect = scrollRect;
    m_scrollOffset += scrollOffset;
}

void DrawingAreaImpl::forceRepaint()
{
    m_isWaitingForDidUpdate = false;
    display();
}

void DrawingAreaImpl::didInstallPageOverlay()
{
    if (m_layerTreeHost)
        m_layerTreeHost->didInstallPageOverlay();
}

void DrawingAreaImpl::didUninstallPageOverlay()
{
    if (m_layerTreeHost)
        m_layerTreeHost->didUninstallPageOverlay();

    setNeedsDisplay(m_webPage->bounds());
}

void DrawingAreaImpl::setPageOverlayNeedsDisplay(const IntRect& rect)
{
    if (m_layerTreeHost) {
        m_layerTreeHost->setPageOverlayNeedsDisplay(rect);
        return;
    }

    setNeedsDisplay(rect);
}

void DrawingAreaImpl::attachCompositingContext()
{
}

void DrawingAreaImpl::detachCompositingContext()
{
}

void DrawingAreaImpl::setRootCompositingLayer(GraphicsLayer* graphicsLayer)
{
    if (graphicsLayer) {
        if (!m_layerTreeHost) {
            // We're actually entering accelerated compositing mode.
            enterAcceleratedCompositingMode(graphicsLayer);
        } else {
            m_exitCompositingTimer.stop();
            // We're already in accelerated compositing mode, but the root compositing layer changed.
            m_layerTreeHost->setRootCompositingLayer(graphicsLayer);
        }
    } else {
        if (m_layerTreeHost) {
            // We'll exit accelerated compositing mode on a timer, to avoid re-entering
            // compositing code via display() and layout.
            exitAcceleratedCompositingModeSoon();
        }
    }
}

void DrawingAreaImpl::scheduleCompositingLayerSync()
{
    if (!m_layerTreeHost)
        return;
    m_layerTreeHost->scheduleLayerFlush();
}

void DrawingAreaImpl::syncCompositingLayers()
{
}

void DrawingAreaImpl::didReceiveMessage(CoreIPC::Connection*, CoreIPC::MessageID, CoreIPC::ArgumentDecoder*)
{
}

void DrawingAreaImpl::setSize(const WebCore::IntSize& size, const WebCore::IntSize& scrollOffset)
{
    ASSERT(!m_inSetSize);
    m_inSetSize = true;

    // Set this to false since we're about to call display().
    m_isWaitingForDidUpdate = false;

    m_webPage->setSize(size);
    m_webPage->layoutIfNeeded();
    m_webPage->scrollMainFrameIfNotAtMaxScrollPosition(scrollOffset);

    UpdateInfo updateInfo;
    LayerTreeContext layerTreeContext;

    if (m_layerTreeHost) {
        m_layerTreeHost->sizeDidChange(size);
        layerTreeContext = m_layerTreeHost->layerTreeContext();
    }

    if (m_isPaintingSuspended || m_layerTreeHost)
        updateInfo.viewSize = m_webPage->size();
    else {
        m_dirtyRegion.unite(m_webPage->bounds());

        // The display here should not cause layout to happen, so we can't enter accelerated compositing mode here.
        display(updateInfo);
        ASSERT(!m_layerTreeHost);
    }

    m_webPage->send(Messages::DrawingAreaProxy::DidSetSize(generateSequenceNumber(), updateInfo, layerTreeContext));

    m_inSetSize = false;
}

void DrawingAreaImpl::didUpdate()
{
    // We might get didUpdate messages from the UI process even after we've
    // entered accelerated compositing mode. Ignore them.
    if (m_layerTreeHost)
        return;

    m_isWaitingForDidUpdate = false;

    // Display if needed.
    display();
}

void DrawingAreaImpl::suspendPainting()
{
    ASSERT(!m_isPaintingSuspended);

    m_isPaintingSuspended = true;
    m_displayTimer.stop();
}

void DrawingAreaImpl::resumePainting()
{
    ASSERT(m_isPaintingSuspended);

    m_isPaintingSuspended = false;

    // FIXME: We shouldn't always repaint everything here.
    setNeedsDisplay(m_webPage->bounds());
}

void DrawingAreaImpl::enterAcceleratedCompositingMode(GraphicsLayer* graphicsLayer)
{
    m_exitCompositingTimer.stop();

    ASSERT(!m_layerTreeHost);

    m_layerTreeHost = LayerTreeHost::create(m_webPage);
    m_layerTreeHost->setRootCompositingLayer(graphicsLayer);
    
    // Non-composited content will now be handled exclusively by the layer tree host.
    m_dirtyRegion = Region();
    m_scrollRect = IntRect();
    m_scrollOffset = IntSize();
    m_displayTimer.stop();
    m_isWaitingForDidUpdate = false;

    if (!m_inSetSize)
        m_webPage->send(Messages::DrawingAreaProxy::EnterAcceleratedCompositingMode(generateSequenceNumber(), m_layerTreeHost->layerTreeContext()));
}

void DrawingAreaImpl::exitAcceleratedCompositingMode()
{
    m_exitCompositingTimer.stop();

    ASSERT(m_layerTreeHost);

    m_layerTreeHost->invalidate();
    m_layerTreeHost = nullptr;

    if (m_inSetSize)
        return;

    UpdateInfo updateInfo;
    if (m_isPaintingSuspended)
        updateInfo.viewSize = m_webPage->size();
    else {
        m_dirtyRegion = m_webPage->bounds();
        display(updateInfo);
    }

    // Send along a complete update of the page so we can paint the contents right after we exit the
    // accelerated compositing mode, eliminiating flicker.
    if (!m_inSetSize)
        m_webPage->send(Messages::DrawingAreaProxy::ExitAcceleratedCompositingMode(generateSequenceNumber(), updateInfo));
}

void DrawingAreaImpl::exitAcceleratedCompositingModeSoon()
{
    if (m_exitCompositingTimer.isActive())
        return;

    m_exitCompositingTimer.startOneShot(0);
}

void DrawingAreaImpl::scheduleDisplay()
{
    if (m_isWaitingForDidUpdate)
        return;

    if (m_isPaintingSuspended)
        return;

    if (m_dirtyRegion.isEmpty())
        return;

    if (m_displayTimer.isActive())
        return;

    m_displayTimer.startOneShot(0);
}

void DrawingAreaImpl::display()
{
    ASSERT(!m_layerTreeHost);
    ASSERT(!m_isWaitingForDidUpdate);

    if (m_isPaintingSuspended)
        return;

    if (m_dirtyRegion.isEmpty())
        return;

    UpdateInfo updateInfo;
    display(updateInfo);

    if (m_layerTreeHost) {
        // The call to update caused layout which turned on accelerated compositing.
        // Don't send an Update message in this case.
        return;
    }

    m_webPage->send(Messages::DrawingAreaProxy::Update(generateSequenceNumber(), updateInfo));
    m_isWaitingForDidUpdate = true;
}

static bool shouldPaintBoundsRect(const IntRect& bounds, const Vector<IntRect>& rects)
{
    const size_t rectThreshold = 10;
    const float wastedSpaceThreshold = 0.75f;

    if (rects.size() <= 1 || rects.size() > rectThreshold)
        return true;

    // Attempt to guess whether or not we should use the region bounds rect or the individual rects.
    // We do this by computing the percentage of "wasted space" in the bounds.  If that wasted space
    // is too large, then we will do individual rect painting instead.
    unsigned boundsArea = bounds.width() * bounds.height();
    unsigned rectsArea = 0;
    for (size_t i = 0; i < rects.size(); ++i)
        rectsArea += rects[i].width() * rects[i].height();

    float wastedSpace = 1 - (rectsArea / boundsArea);

    return wastedSpace <= wastedSpaceThreshold;
}

void DrawingAreaImpl::display(UpdateInfo& updateInfo)
{
    ASSERT(!m_isPaintingSuspended);
    ASSERT(!m_layerTreeHost);
    ASSERT(!m_webPage->size().isEmpty());

    // FIXME: It would be better if we could avoid painting altogether when there is a custom representation.
    if (m_webPage->mainFrameHasCustomRepresentation())
        return;

    IntRect bounds = m_dirtyRegion.bounds();
    Vector<IntRect> rects = m_dirtyRegion.rects();

    if (shouldPaintBoundsRect(bounds, rects)) {
        rects.clear();
        rects.append(bounds);
    }

    updateInfo.scrollRect = m_scrollRect;
    updateInfo.scrollOffset = m_scrollOffset;

    m_dirtyRegion = Region();
    m_scrollRect = IntRect();
    m_scrollOffset = IntSize();
    
    RefPtr<ShareableBitmap> bitmap = ShareableBitmap::createShareable(bounds.size());
    if (!bitmap->createHandle(updateInfo.bitmapHandle))
        return;

    OwnPtr<GraphicsContext> graphicsContext = bitmap->createGraphicsContext();

    m_webPage->layoutIfNeeded();
    
    updateInfo.viewSize = m_webPage->size();
    updateInfo.updateRectBounds = bounds;

    graphicsContext->translate(-bounds.x(), -bounds.y());

    for (size_t i = 0; i < rects.size(); ++i) {
        m_webPage->drawRect(*graphicsContext, rects[i]);
        if (m_webPage->hasPageOverlay())
            m_webPage->drawPageOverlay(*graphicsContext, rects[i]);
        updateInfo.updateRects.append(rects[i]);
    }

    // Layout can trigger more calls to setNeedsDisplay and we don't want to process them
    // until the UI process has painted the update, so we stop the timer here.
    m_displayTimer.stop();
}


} // namespace WebKit