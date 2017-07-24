/*
 * Copyright (C) 2008-2017 Apple Inc. All Rights Reserved.
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

#include "config.h"
#include "ApplicationCacheGroup.h"

#include "ApplicationCache.h"
#include "ApplicationCacheHost.h"
#include "ApplicationCacheResource.h"
#include "ApplicationCacheStorage.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "DOMApplicationCache.h"
#include "DocumentLoader.h"
#include "EventNames.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "HTTPHeaderNames.h"
#include "InspectorInstrumentation.h"
#include "ManifestParser.h"
#include "Page.h"
#include "ProgressTracker.h"
#include "ResourceHandle.h"
#include "SecurityOrigin.h"
#include "Settings.h"
#include <wtf/HashMap.h>
#include <wtf/MainThread.h>

namespace WebCore {

ApplicationCacheGroup::ApplicationCacheGroup(Ref<ApplicationCacheStorage>&& storage, const URL& manifestURL)
    : m_storage(WTFMove(storage))
    , m_manifestURL(manifestURL)
    , m_origin(SecurityOrigin::create(manifestURL))
    , m_availableSpaceInQuota(ApplicationCacheStorage::unknownQuota())
{
}

ApplicationCacheGroup::~ApplicationCacheGroup()
{
    ASSERT(!m_newestCache);
    ASSERT(m_caches.isEmpty());

    stopLoading();

    m_storage->cacheGroupDestroyed(*this);
}

ApplicationCache* ApplicationCacheGroup::cacheForMainRequest(const ResourceRequest& request, DocumentLoader* documentLoader)
{
    if (!ApplicationCache::requestIsHTTPOrHTTPSGet(request))
        return nullptr;

    URL url(request.url());
    url.removeFragmentIdentifier();

    auto* page = documentLoader->frame() ? documentLoader->frame()->page() : nullptr;
    if (!page || page->usesEphemeralSession())
        return nullptr;

    auto* group = page->applicationCacheStorage().cacheGroupForURL(url);
    if (!group)
        return nullptr;

    ASSERT(group->newestCache());
    ASSERT(!group->isObsolete());

    return group->newestCache();
}

ApplicationCache* ApplicationCacheGroup::fallbackCacheForMainRequest(const ResourceRequest& request, DocumentLoader* documentLoader)
{
    if (!ApplicationCache::requestIsHTTPOrHTTPSGet(request))
        return nullptr;

    auto* frame = documentLoader->frame();
    if (!frame)
        return nullptr;

    auto* page = frame->page();
    if (!page)
        return nullptr;

    URL url(request.url());
    url.removeFragmentIdentifier();

    auto* group = page->applicationCacheStorage().fallbackCacheGroupForURL(url);
    if (!group)
        return nullptr;

    ASSERT(group->newestCache());
    ASSERT(!group->isObsolete());

    return group->newestCache();
}

void ApplicationCacheGroup::selectCache(Frame& frame, const URL& passedManifestURL)
{
    ASSERT(frame.document());
    ASSERT(frame.page());
    ASSERT(frame.loader().documentLoader());

    if (!frame.settings().offlineWebApplicationCacheEnabled())
        return;

    auto& documentLoader = *frame.loader().documentLoader();
    ASSERT(!documentLoader.applicationCacheHost().applicationCache());

    if (passedManifestURL.isNull()) {
        selectCacheWithoutManifestURL(frame);
        return;
    }

    // Don't access anything on disk if private browsing is enabled.
    if (frame.page()->usesEphemeralSession() || !frame.document()->securityOrigin().canAccessApplicationCache(frame.tree().top().document()->securityOrigin())) {
        postListenerTask(eventNames().checkingEvent, documentLoader);
        postListenerTask(eventNames().errorEvent, documentLoader);
        return;
    }

    URL manifestURL(passedManifestURL);
    manifestURL.removeFragmentIdentifier();

    auto* mainResourceCache = documentLoader.applicationCacheHost().mainResourceApplicationCache();

    if (mainResourceCache) {
        ASSERT(mainResourceCache->group());
        if (manifestURL == mainResourceCache->group()->m_manifestURL) {
            // The cache may have gotten obsoleted after we've loaded from it, but before we parsed the document and saw cache manifest.
            if (mainResourceCache->group()->isObsolete())
                return;
            mainResourceCache->group()->associateDocumentLoaderWithCache(&documentLoader, mainResourceCache);
            mainResourceCache->group()->update(frame, ApplicationCacheUpdateWithBrowsingContext);
        } else {
            // The main resource was loaded from cache, so the cache must have an entry for it. Mark it as foreign.
            URL resourceURL { documentLoader.responseURL() };
            resourceURL.removeFragmentIdentifier();

            ASSERT(mainResourceCache->resourceForURL(resourceURL));
            auto& resource = *mainResourceCache->resourceForURL(resourceURL);

            bool inStorage = resource.storageID();
            resource.addType(ApplicationCacheResource::Foreign);
            if (inStorage)
                frame.page()->applicationCacheStorage().storeUpdatedType(&resource, mainResourceCache);

            // Restart the current navigation from the top of the navigation algorithm, undoing any changes that were made
            // as part of the initial load.
            // The navigation will not result in the same resource being loaded, because "foreign" entries are never picked during navigation.
            frame.navigationScheduler().scheduleLocationChange(*frame.document(), frame.document()->securityOrigin(), documentLoader.url(), frame.loader().referrer());
        }
        return;
    }

    // The resource was loaded from the network, check if it is a HTTP/HTTPS GET.
    auto& request = frame.loader().activeDocumentLoader()->request();

    if (!ApplicationCache::requestIsHTTPOrHTTPSGet(request))
        return;

    // Check that the resource URL has the same scheme/host/port as the manifest URL.
    if (!protocolHostAndPortAreEqual(manifestURL, request.url()))
        return;

    auto& group = *frame.page()->applicationCacheStorage().findOrCreateCacheGroup(manifestURL);

    documentLoader.applicationCacheHost().setCandidateApplicationCacheGroup(&group);
    group.m_pendingMasterResourceLoaders.add(&documentLoader);
    group.m_downloadingPendingMasterResourceLoadersCount++;

    ASSERT(!group.m_cacheBeingUpdated || group.m_updateStatus != Idle);
    group.update(frame, ApplicationCacheUpdateWithBrowsingContext);
}

void ApplicationCacheGroup::selectCacheWithoutManifestURL(Frame& frame)
{
    if (!frame.settings().offlineWebApplicationCacheEnabled())
        return;

    ASSERT(frame.document());
    ASSERT(frame.page());
    ASSERT(frame.loader().documentLoader());
    auto& documentLoader = *frame.loader().documentLoader();
    ASSERT(!documentLoader.applicationCacheHost().applicationCache());

    // Don't access anything on disk if private browsing is enabled.
    if (frame.page()->usesEphemeralSession() || !frame.document()->securityOrigin().canAccessApplicationCache(frame.tree().top().document()->securityOrigin())) {
        postListenerTask(eventNames().checkingEvent, documentLoader);
        postListenerTask(eventNames().errorEvent, documentLoader);
        return;
    }

    if (auto* mainResourceCache = documentLoader.applicationCacheHost().mainResourceApplicationCache()) {
        ASSERT(mainResourceCache->group());
        auto& group = *mainResourceCache->group();
        group.associateDocumentLoaderWithCache(&documentLoader, mainResourceCache);
        group.update(frame, ApplicationCacheUpdateWithBrowsingContext);
    }
}

void ApplicationCacheGroup::finishedLoadingMainResource(DocumentLoader& loader)
{
    ASSERT(m_pendingMasterResourceLoaders.contains(&loader));
    ASSERT(m_completionType == None || m_pendingEntries.isEmpty());
    URL url = loader.url();
    url.removeFragmentIdentifier();

    switch (m_completionType) {
    case None:
        // The main resource finished loading before the manifest was ready. It will be handled via dispatchMainResources() later.
        return;
    case NoUpdate:
        ASSERT(!m_cacheBeingUpdated);
        associateDocumentLoaderWithCache(&loader, m_newestCache.get());
        if (auto* resource = m_newestCache->resourceForURL(url)) {
            if (!(resource->type() & ApplicationCacheResource::Master)) {
                resource->addType(ApplicationCacheResource::Master);
                ASSERT(!resource->storageID());
            }
        } else
            m_newestCache->addResource(ApplicationCacheResource::create(url, loader.response(), ApplicationCacheResource::Master, loader.mainResourceData()));
        break;
    case Failure:
        // Cache update has been a failure, so there is no reason to keep the document associated with the incomplete cache
        // (its main resource was not cached yet, so it is likely that the application changed significantly server-side).
        ASSERT(!m_cacheBeingUpdated); // Already cleared out by stopLoading().
        loader.applicationCacheHost().setApplicationCache(nullptr); // Will unset candidate, too.
        m_associatedDocumentLoaders.remove(&loader);
        postListenerTask(eventNames().errorEvent, loader);
        break;
    case Completed:
        ASSERT(m_associatedDocumentLoaders.contains(&loader));
        if (auto* resource = m_cacheBeingUpdated->resourceForURL(url)) {
            if (!(resource->type() & ApplicationCacheResource::Master)) {
                resource->addType(ApplicationCacheResource::Master);
                ASSERT(!resource->storageID());
            }
        } else
            m_cacheBeingUpdated->addResource(ApplicationCacheResource::create(url, loader.response(), ApplicationCacheResource::Master, loader.mainResourceData()));
        // The "cached" event will be posted to all associated documents once update is complete.
        break;
    }

    ASSERT(m_downloadingPendingMasterResourceLoadersCount > 0);
    m_downloadingPendingMasterResourceLoadersCount--;
    checkIfLoadIsComplete();
}

void ApplicationCacheGroup::failedLoadingMainResource(DocumentLoader& loader)
{
    ASSERT(m_pendingMasterResourceLoaders.contains(&loader));
    ASSERT(m_completionType == None || m_pendingEntries.isEmpty());

    switch (m_completionType) {
    case None:
        // The main resource finished loading before the manifest was ready. It will be handled via dispatchMainResources() later.
        return;
    case NoUpdate:
        ASSERT(!m_cacheBeingUpdated);
        // The manifest didn't change, and we have a relevant cache - but the main resource download failed mid-way, so it cannot be stored to the cache,
        // and the loader does not get associated to it. If there are other main resources being downloaded for this cache group, they may still succeed.
        postListenerTask(eventNames().errorEvent, loader);
        break;
    case Failure:
        // Cache update failed, too.
        ASSERT(!m_cacheBeingUpdated); // Already cleared out by stopLoading().
        ASSERT(!loader.applicationCacheHost().applicationCache() || loader.applicationCacheHost().applicationCache()->group() == this);
        loader.applicationCacheHost().setApplicationCache(nullptr); // Will unset candidate, too.
        m_associatedDocumentLoaders.remove(&loader);
        postListenerTask(eventNames().errorEvent, loader);
        break;
    case Completed:
        // The cache manifest didn't list this main resource, and all cache entries were already updated successfully - but the main resource failed to load,
        // so it cannot be stored to the cache. If there are other main resources being downloaded for this cache group, they may still succeed.
        ASSERT(m_associatedDocumentLoaders.contains(&loader));
        ASSERT(loader.applicationCacheHost().applicationCache() == m_cacheBeingUpdated);
        ASSERT(!loader.applicationCacheHost().candidateApplicationCacheGroup());
        m_associatedDocumentLoaders.remove(&loader);
        loader.applicationCacheHost().setApplicationCache(nullptr);
        postListenerTask(eventNames().errorEvent, loader);
        break;
    }

    ASSERT(m_downloadingPendingMasterResourceLoadersCount > 0);
    m_downloadingPendingMasterResourceLoadersCount--;
    checkIfLoadIsComplete();
}

void ApplicationCacheGroup::stopLoading()
{
    if (m_manifestHandle) {
        ASSERT(!m_currentHandle);

        ASSERT(m_manifestHandle->client() == this);
        m_manifestHandle->clearClient();

        m_manifestHandle->cancel();
        m_manifestHandle = nullptr;
    }

    if (m_currentHandle) {
        ASSERT(!m_manifestHandle);
        ASSERT(m_cacheBeingUpdated);

        ASSERT(m_currentHandle->client() == this);
        m_currentHandle->clearClient();

        m_currentHandle->cancel();
        m_currentHandle = nullptr;
    }

    // FIXME: Resetting just a tiny part of the state in this function is confusing. Callers have to take care of a lot more.
    m_cacheBeingUpdated = nullptr;
    m_pendingEntries.clear();
}

void ApplicationCacheGroup::disassociateDocumentLoader(DocumentLoader& loader)
{
    m_associatedDocumentLoaders.remove(&loader);
    m_pendingMasterResourceLoaders.remove(&loader);

    if (auto* host = loader.applicationCacheHostUnlessBeingDestroyed())
        host->setApplicationCache(nullptr); // Will set candidate group to null, too.

    if (!m_associatedDocumentLoaders.isEmpty() || !m_pendingMasterResourceLoaders.isEmpty())
        return;

    if (m_caches.isEmpty()) {
        // There is an initial cache attempt in progress.
        ASSERT(!m_newestCache);
        // Delete ourselves, causing the cache attempt to be stopped.
        delete this;
        return;
    }

    ASSERT(m_caches.contains(m_newestCache.get()));

    // Release our reference to the newest cache. This could cause us to be deleted.
    // Any ongoing updates will be stopped from destructor.
    m_newestCache = nullptr;
}

void ApplicationCacheGroup::cacheDestroyed(ApplicationCache& cache)
{
    if (m_caches.remove(&cache) && m_caches.isEmpty()) {
        ASSERT(m_associatedDocumentLoaders.isEmpty());
        ASSERT(m_pendingMasterResourceLoaders.isEmpty());
        delete this;
    }
}

void ApplicationCacheGroup::stopLoadingInFrame(Frame& frame)
{
    if (&frame != m_frame)
        return;

    cacheUpdateFailed();
}

void ApplicationCacheGroup::setNewestCache(Ref<ApplicationCache>&& newestCache)
{
    m_newestCache = WTFMove(newestCache);

    m_caches.add(m_newestCache.get());
    m_newestCache->setGroup(this);
}

void ApplicationCacheGroup::makeObsolete()
{
    if (isObsolete())
        return;

    m_isObsolete = true;
    m_storage->cacheGroupMadeObsolete(*this);
    ASSERT(!m_storageID);
}

void ApplicationCacheGroup::update(Frame& frame, ApplicationCacheUpdateOption updateOption)
{
    ASSERT(frame.loader().documentLoader());
    auto& documentLoader = *frame.loader().documentLoader();

    if (m_updateStatus == Checking || m_updateStatus == Downloading) {
        if (updateOption == ApplicationCacheUpdateWithBrowsingContext) {
            postListenerTask(eventNames().checkingEvent, documentLoader);
            if (m_updateStatus == Downloading)
                postListenerTask(eventNames().downloadingEvent, documentLoader);
        }
        return;
    }

    // Don't access anything on disk if private browsing is enabled.
    if (frame.page()->usesEphemeralSession() || !frame.document()->securityOrigin().canAccessApplicationCache(frame.tree().top().document()->securityOrigin())) {
        ASSERT(m_pendingMasterResourceLoaders.isEmpty());
        ASSERT(m_pendingEntries.isEmpty());
        ASSERT(!m_cacheBeingUpdated);
        postListenerTask(eventNames().checkingEvent, documentLoader);
        postListenerTask(eventNames().errorEvent, documentLoader);
        return;
    }

    ASSERT(!m_frame);
    m_frame = &frame;

    setUpdateStatus(Checking);

    postListenerTask(eventNames().checkingEvent, m_associatedDocumentLoaders);
    if (!m_newestCache) {
        ASSERT(updateOption == ApplicationCacheUpdateWithBrowsingContext);
        postListenerTask(eventNames().checkingEvent, documentLoader);
    }

    ASSERT(!m_manifestHandle);
    ASSERT(!m_manifestResource);
    ASSERT(!m_currentHandle);
    ASSERT(!m_currentResource);
    ASSERT(m_completionType == None);

    // FIXME: Handle defer loading
    m_manifestHandle = createResourceHandle(m_manifestURL, m_newestCache ? m_newestCache->manifestResource() : 0);
}

void ApplicationCacheGroup::abort(Frame& frame)
{
    if (m_updateStatus == Idle)
        return;
    ASSERT(m_updateStatus == Checking || (m_updateStatus == Downloading && m_cacheBeingUpdated));

    if (m_completionType != None)
        return;

    frame.document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Debug, ASCIILiteral("Application Cache download process was aborted."));
    cacheUpdateFailed();
}

RefPtr<ResourceHandle> ApplicationCacheGroup::createResourceHandle(const URL& url, ApplicationCacheResource* newestCachedResource)
{
    ResourceRequest request(url);
    m_frame->loader().applyUserAgent(request);
    request.setHTTPHeaderField(HTTPHeaderName::CacheControl, "max-age=0");

    if (newestCachedResource) {
        const String& lastModified = newestCachedResource->response().httpHeaderField(HTTPHeaderName::LastModified);
        const String& eTag = newestCachedResource->response().httpHeaderField(HTTPHeaderName::ETag);
        if (!lastModified.isEmpty() || !eTag.isEmpty()) {
            if (!lastModified.isEmpty())
                request.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, lastModified);
            if (!eTag.isEmpty())
                request.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, eTag);
        }
    }

    RefPtr<ResourceHandle> handle = ResourceHandle::create(m_frame->loader().networkingContext(), request, this, false, true);

    // Because willSendRequest only gets called during redirects, we initialize
    // the identifier and the first willSendRequest here.
    m_currentResourceIdentifier = m_frame->page()->progress().createUniqueIdentifier();
    ResourceResponse redirectResponse = ResourceResponse();
    InspectorInstrumentation::willSendRequest(m_frame, m_currentResourceIdentifier, m_frame->loader().documentLoader(), request, redirectResponse);
    return handle;
}

void ApplicationCacheGroup::didReceiveResponse(ResourceHandle* handle, ResourceResponse&& response)
{
    ASSERT(m_frame);
    InspectorInstrumentation::didReceiveResourceResponse(*m_frame, m_currentResourceIdentifier, m_frame->loader().documentLoader(), response, nullptr);

    if (handle == m_manifestHandle) {
        didReceiveManifestResponse(response);
        return;
    }

    ASSERT(handle == m_currentHandle);

    URL url(handle->firstRequest().url());
    url.removeFragmentIdentifier();

    ASSERT(!m_currentResource);
    ASSERT(m_pendingEntries.contains(url));

    unsigned type = m_pendingEntries.get(url);

    // If this is an initial cache attempt, we should not get master resources delivered here.
    if (!m_newestCache)
        ASSERT(!(type & ApplicationCacheResource::Master));

    if (m_newestCache && response.httpStatusCode() == 304) { // Not modified.
        ApplicationCacheResource* newestCachedResource = m_newestCache->resourceForURL(url);
        if (newestCachedResource) {
            m_cacheBeingUpdated->addResource(ApplicationCacheResource::create(url, newestCachedResource->response(), type, &newestCachedResource->data(), newestCachedResource->path()));
            m_pendingEntries.remove(m_currentHandle->firstRequest().url());
            m_currentHandle->cancel();
            m_currentHandle = nullptr;
            // Load the next resource, if any.
            startLoadingEntry();
            return;
        }
        // The server could return 304 for an unconditional request - in this case, we handle the response as a normal error.
    }

    if (response.httpStatusCode() / 100 != 2 || response.url() != m_currentHandle->firstRequest().url()) {
        if ((type & ApplicationCacheResource::Explicit) || (type & ApplicationCacheResource::Fallback)) {
            m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, "Application Cache update failed, because " + m_currentHandle->firstRequest().url().stringCenterEllipsizedToLength() +
                ((response.httpStatusCode() / 100 != 2) ? " could not be fetched." : " was redirected."));
            // Note that cacheUpdateFailed() can cause the cache group to be deleted.
            cacheUpdateFailed();
        } else if (response.httpStatusCode() == 404 || response.httpStatusCode() == 410) {
            // Skip this resource. It is dropped from the cache.
            m_currentHandle->cancel();
            m_currentHandle = nullptr;
            m_pendingEntries.remove(url);
            // Load the next resource, if any.
            startLoadingEntry();
        } else {
            // Copy the resource and its metadata from the newest application cache in cache group whose completeness flag is complete, and act
            // as if that was the fetched resource, ignoring the resource obtained from the network.
            ASSERT(m_newestCache);
            ApplicationCacheResource* newestCachedResource = m_newestCache->resourceForURL(handle->firstRequest().url());
            ASSERT(newestCachedResource);
            m_cacheBeingUpdated->addResource(ApplicationCacheResource::create(url, newestCachedResource->response(), type, &newestCachedResource->data(), newestCachedResource->path()));
            m_pendingEntries.remove(m_currentHandle->firstRequest().url());
            m_currentHandle->cancel();
            m_currentHandle = nullptr;
            // Load the next resource, if any.
            startLoadingEntry();
        }
        return;
    }

    m_currentResource = ApplicationCacheResource::create(url, response, type);
}

void ApplicationCacheGroup::didReceiveData(ResourceHandle* handle, const char* data, unsigned length, int encodedDataLength)
{
    UNUSED_PARAM(encodedDataLength);

    InspectorInstrumentation::didReceiveData(m_frame, m_currentResourceIdentifier, 0, length, 0);

    if (handle == m_manifestHandle) {
        didReceiveManifestData(data, length);
        return;
    }

    ASSERT(handle == m_currentHandle);

    ASSERT(m_currentResource);
    m_currentResource->data().append(data, length);
}

void ApplicationCacheGroup::didFinishLoading(ResourceHandle* handle, double finishTime)
{
    InspectorInstrumentation::didFinishLoading(m_frame, m_frame->loader().documentLoader(), m_currentResourceIdentifier, finishTime);

    if (handle == m_manifestHandle) {
        didFinishLoadingManifest();
        return;
    }

    ASSERT(m_currentHandle == handle);
    ASSERT(m_pendingEntries.contains(handle->firstRequest().url()));

    m_pendingEntries.remove(handle->firstRequest().url());

    ASSERT(m_cacheBeingUpdated);

    m_cacheBeingUpdated->addResource(m_currentResource.releaseNonNull());
    m_currentHandle = nullptr;

    // While downloading check to see if we have exceeded the available quota.
    // We can stop immediately if we have already previously failed
    // due to an earlier quota restriction. The client was already notified
    // of the quota being reached and decided not to increase it then.
    // FIXME: Should we break earlier and prevent redownloading on later page loads?
    if (m_originQuotaExceededPreviously && m_availableSpaceInQuota < m_cacheBeingUpdated->estimatedSizeInStorage()) {
        m_currentResource = nullptr;
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, ASCIILiteral("Application Cache update failed, because size quota was exceeded."));
        cacheUpdateFailed();
        return;
    }

    // Load the next resource, if any.
    startLoadingEntry();
}

void ApplicationCacheGroup::didFail(ResourceHandle* handle, const ResourceError& error)
{
    InspectorInstrumentation::didFailLoading(m_frame, m_frame->loader().documentLoader(), m_currentResourceIdentifier, error);

    if (handle == m_manifestHandle) {
        // A network error is logged elsewhere, no need to log again. Also, it's normal for manifest fetching to fail when working offline.
        cacheUpdateFailed();
        return;
    }

    ASSERT(handle == m_currentHandle);

    unsigned type = m_currentResource ? m_currentResource->type() : m_pendingEntries.get(handle->firstRequest().url());
    URL url(handle->firstRequest().url());
    url.removeFragmentIdentifier();

    ASSERT(!m_currentResource || !m_pendingEntries.contains(url));
    m_currentResource = nullptr;
    m_pendingEntries.remove(url);

    if ((type & ApplicationCacheResource::Explicit) || (type & ApplicationCacheResource::Fallback)) {
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, "Application Cache update failed, because " + url.stringCenterEllipsizedToLength() + " could not be fetched.");
        // Note that cacheUpdateFailed() can cause the cache group to be deleted.
        cacheUpdateFailed();
    } else {
        // Copy the resource and its metadata from the newest application cache in cache group whose completeness flag is complete, and act
        // as if that was the fetched resource, ignoring the resource obtained from the network.
        ASSERT(m_newestCache);
        ApplicationCacheResource* newestCachedResource = m_newestCache->resourceForURL(url);
        ASSERT(newestCachedResource);
        m_cacheBeingUpdated->addResource(ApplicationCacheResource::create(url, newestCachedResource->response(), type, &newestCachedResource->data(), newestCachedResource->path()));
        // Load the next resource, if any.
        startLoadingEntry();
    }
}

void ApplicationCacheGroup::didReceiveManifestResponse(const ResourceResponse& response)
{
    ASSERT(!m_manifestResource);
    ASSERT(m_manifestHandle);

    if (response.httpStatusCode() == 404 || response.httpStatusCode() == 410) {
        InspectorInstrumentation::didFailLoading(m_frame, m_frame->loader().documentLoader(), m_currentResourceIdentifier, m_frame->loader().cancelledError(m_manifestHandle->firstRequest()));
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, makeString("Application Cache manifest could not be fetched, because the manifest had a ", String::number(response.httpStatusCode()), " response."));
        manifestNotFound();
        return;
    }

    if (response.httpStatusCode() == 304)
        return;

    if (response.httpStatusCode() / 100 != 2) {
        InspectorInstrumentation::didFailLoading(m_frame, m_frame->loader().documentLoader(), m_currentResourceIdentifier, m_frame->loader().cancelledError(m_manifestHandle->firstRequest()));
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, makeString("Application Cache manifest could not be fetched, because the manifest had a ", String::number(response.httpStatusCode()), " response."));
        cacheUpdateFailed();
        return;
    }

    if (response.url() != m_manifestHandle->firstRequest().url()) {
        InspectorInstrumentation::didFailLoading(m_frame, m_frame->loader().documentLoader(), m_currentResourceIdentifier, m_frame->loader().cancelledError(m_manifestHandle->firstRequest()));
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, ASCIILiteral("Application Cache manifest could not be fetched, because a redirection was attempted."));
        cacheUpdateFailed();
        return;
    }

    m_manifestResource = ApplicationCacheResource::create(m_manifestHandle->firstRequest().url(), response, ApplicationCacheResource::Manifest);
}

void ApplicationCacheGroup::didReceiveManifestData(const char* data, int length)
{
    if (m_manifestResource)
        m_manifestResource->data().append(data, length);
}

void ApplicationCacheGroup::didFinishLoadingManifest()
{
    bool isUpgradeAttempt = m_newestCache;

    if (!isUpgradeAttempt && !m_manifestResource) {
        // The server returned 304 Not Modified even though we didn't send a conditional request.
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, ASCIILiteral("Application Cache manifest could not be fetched because of an unexpected 304 Not Modified server response."));
        cacheUpdateFailed();
        return;
    }

    m_manifestHandle = nullptr;

    // Check if the manifest was not modified.
    if (isUpgradeAttempt) {
        ApplicationCacheResource* newestManifest = m_newestCache->manifestResource();
        ASSERT(newestManifest);

        if (!m_manifestResource || // The resource will be null if HTTP response was 304 Not Modified.
            (newestManifest->data().size() == m_manifestResource->data().size() && !memcmp(newestManifest->data().data(), m_manifestResource->data().data(), newestManifest->data().size()))) {

            m_completionType = NoUpdate;
            m_manifestResource = nullptr;
            deliverDelayedMainResources();

            return;
        }
    }

    Manifest manifest;
    if (!parseManifest(m_manifestURL, m_manifestResource->data().data(), m_manifestResource->data().size(), manifest)) {
        // At the time of this writing, lack of "CACHE MANIFEST" signature is the only reason for parseManifest to fail.
        m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, ASCIILiteral("Application Cache manifest could not be parsed. Does it start with CACHE MANIFEST?"));
        cacheUpdateFailed();
        return;
    }

    ASSERT(!m_cacheBeingUpdated);
    m_cacheBeingUpdated = ApplicationCache::create();
    m_cacheBeingUpdated->setGroup(this);

    for (auto& loader : m_pendingMasterResourceLoaders)
        associateDocumentLoaderWithCache(loader, m_cacheBeingUpdated.get());

    // We have the manifest, now download the resources.
    setUpdateStatus(Downloading);

    postListenerTask(eventNames().downloadingEvent, m_associatedDocumentLoaders);

    ASSERT(m_pendingEntries.isEmpty());

    if (isUpgradeAttempt) {
        for (const auto& urlAndResource : m_newestCache->resources()) {
            unsigned type = urlAndResource.value->type();
            if (type & ApplicationCacheResource::Master)
                addEntry(urlAndResource.key, type);
        }
    }

    for (const auto& explicitURL : manifest.explicitURLs)
        addEntry(explicitURL, ApplicationCacheResource::Explicit);

    for (auto& fallbackURL : manifest.fallbackURLs)
        addEntry(fallbackURL.second, ApplicationCacheResource::Fallback);

    m_cacheBeingUpdated->setOnlineWhitelist(manifest.onlineWhitelistedURLs);
    m_cacheBeingUpdated->setFallbackURLs(manifest.fallbackURLs);
    m_cacheBeingUpdated->setAllowsAllNetworkRequests(manifest.allowAllNetworkRequests);

    m_progressTotal = m_pendingEntries.size();
    m_progressDone = 0;

    recalculateAvailableSpaceInQuota();

    startLoadingEntry();
}

void ApplicationCacheGroup::didReachMaxAppCacheSize()
{
    ASSERT(m_frame);
    ASSERT(m_cacheBeingUpdated);
    m_frame->page()->chrome().client().reachedMaxAppCacheSize(m_frame->page()->applicationCacheStorage().spaceNeeded(m_cacheBeingUpdated->estimatedSizeInStorage()));
    m_calledReachedMaxAppCacheSize = true;
    checkIfLoadIsComplete();
}

void ApplicationCacheGroup::didReachOriginQuota(int64_t totalSpaceNeeded)
{
    // Inform the client the origin quota has been reached, they may decide to increase the quota.
    // We expect quota to be increased synchronously while waiting for the call to return.
    m_frame->page()->chrome().client().reachedApplicationCacheOriginQuota(m_origin.get(), totalSpaceNeeded);
}

void ApplicationCacheGroup::cacheUpdateFailed()
{
    stopLoading();
    m_manifestResource = nullptr;

    // Wait for master resource loads to finish.
    m_completionType = Failure;
    deliverDelayedMainResources();
}

void ApplicationCacheGroup::recalculateAvailableSpaceInQuota()
{
    if (!m_frame->page()->applicationCacheStorage().calculateRemainingSizeForOriginExcludingCache(m_origin, m_newestCache.get(), m_availableSpaceInQuota)) {
        // Failed to determine what is left in the quota. Fallback to allowing anything.
        m_availableSpaceInQuota = ApplicationCacheStorage::noQuota();
    }
}

void ApplicationCacheGroup::manifestNotFound()
{
    makeObsolete();

    postListenerTask(eventNames().obsoleteEvent, m_associatedDocumentLoaders);
    postListenerTask(eventNames().errorEvent, m_pendingMasterResourceLoaders);

    stopLoading();

    ASSERT(m_pendingEntries.isEmpty());
    m_manifestResource = nullptr;

    while (!m_pendingMasterResourceLoaders.isEmpty()) {
        HashSet<DocumentLoader*>::iterator it = m_pendingMasterResourceLoaders.begin();

        ASSERT((*it)->applicationCacheHost().candidateApplicationCacheGroup() == this);
        ASSERT(!(*it)->applicationCacheHost().applicationCache());
        (*it)->applicationCacheHost().setCandidateApplicationCacheGroup(nullptr);
        m_pendingMasterResourceLoaders.remove(it);
    }

    m_downloadingPendingMasterResourceLoadersCount = 0;
    setUpdateStatus(Idle);
    m_frame = nullptr;

    if (m_caches.isEmpty()) {
        ASSERT(m_associatedDocumentLoaders.isEmpty());
        ASSERT(!m_cacheBeingUpdated);
        delete this;
    }
}

void ApplicationCacheGroup::checkIfLoadIsComplete()
{
    if (m_manifestHandle || !m_pendingEntries.isEmpty() || m_downloadingPendingMasterResourceLoadersCount)
        return;

    // We're done, all resources have finished downloading (successfully or not).

    bool isUpgradeAttempt = m_newestCache;

    switch (m_completionType) {
    case None:
        ASSERT_NOT_REACHED();
        return;
    case NoUpdate:
        ASSERT(isUpgradeAttempt);
        ASSERT(!m_cacheBeingUpdated);

        // The storage could have been manually emptied by the user.
        if (!m_storageID)
            m_storage->storeNewestCache(*this);

        postListenerTask(eventNames().noupdateEvent, m_associatedDocumentLoaders);
        break;
    case Failure:
        ASSERT(!m_cacheBeingUpdated);
        postListenerTask(eventNames().errorEvent, m_associatedDocumentLoaders);
        if (m_caches.isEmpty()) {
            ASSERT(m_associatedDocumentLoaders.isEmpty());
            delete this;
            return;
        }
        break;
    case Completed: {
        // FIXME: Fetch the resource from manifest URL again, and check whether it is identical to the one used for update (in case the application was upgraded server-side in the meanwhile). (<rdar://problem/6467625>)

        ASSERT(m_cacheBeingUpdated);
        if (m_manifestResource)
            m_cacheBeingUpdated->setManifestResource(m_manifestResource.releaseNonNull());
        else {
            // We can get here as a result of retrying the Complete step, following
            // a failure of the cache storage to save the newest cache due to hitting
            // the maximum size. In such a case, m_manifestResource may be 0, as
            // the manifest was already set on the newest cache object.
            ASSERT(m_cacheBeingUpdated->manifestResource());
            ASSERT(m_storage->isMaximumSizeReached());
            ASSERT(m_calledReachedMaxAppCacheSize);
        }

        RefPtr<ApplicationCache> oldNewestCache = (m_newestCache == m_cacheBeingUpdated) ? RefPtr<ApplicationCache>() : m_newestCache;

        // If we exceeded the origin quota while downloading we can request a quota
        // increase now, before we attempt to store the cache.
        int64_t totalSpaceNeeded;
        if (!m_storage->checkOriginQuota(this, oldNewestCache.get(), m_cacheBeingUpdated.get(), totalSpaceNeeded))
            didReachOriginQuota(totalSpaceNeeded);

        ApplicationCacheStorage::FailureReason failureReason;
        setNewestCache(m_cacheBeingUpdated.releaseNonNull());
        if (m_storage->storeNewestCache(*this, oldNewestCache.get(), failureReason)) {
            // New cache stored, now remove the old cache.
            if (oldNewestCache)
                m_storage->remove(oldNewestCache.get());

            // Fire the final progress event.
            ASSERT(m_progressDone == m_progressTotal);
            postListenerTask(eventNames().progressEvent, m_progressTotal, m_progressDone, m_associatedDocumentLoaders);

            // Fire the success event.
            postListenerTask(isUpgradeAttempt ? eventNames().updatereadyEvent : eventNames().cachedEvent, m_associatedDocumentLoaders);
            // It is clear that the origin quota was not reached, so clear the flag if it was set.
            m_originQuotaExceededPreviously = false;
        } else {
            if (failureReason == ApplicationCacheStorage::OriginQuotaReached) {
                // We ran out of space for this origin. Fall down to the normal error handling
                // after recording this state.
                m_originQuotaExceededPreviously = true;
                m_frame->document()->addConsoleMessage(MessageSource::AppCache, MessageLevel::Error, ASCIILiteral("Application Cache update failed, because size quota was exceeded."));
            }

            if (failureReason == ApplicationCacheStorage::TotalQuotaReached && !m_calledReachedMaxAppCacheSize) {
                // FIXME: Should this be handled more like Origin Quotas? Does this fail properly?

                // We ran out of space. All the changes in the cache storage have
                // been rolled back. We roll back to the previous state in here,
                // as well, call the chrome client asynchronously and retry to
                // save the new cache.

                // Save a reference to the new cache.
                m_cacheBeingUpdated = WTFMove(m_newestCache);
                if (oldNewestCache)
                    setNewestCache(oldNewestCache.releaseNonNull());
                scheduleReachedMaxAppCacheSizeCallback();
                return;
            }

            // Run the "cache failure steps"
            // Fire the error events to all pending master entries, as well any other cache hosts
            // currently associated with a cache in this group.
            postListenerTask(eventNames().errorEvent, m_associatedDocumentLoaders);
            // Disassociate the pending master entries from the failed new cache. Note that
            // all other loaders in the m_associatedDocumentLoaders are still associated with
            // some other cache in this group. They are not associated with the failed new cache.

            // Need to copy loaders, because the cache group may be destroyed at the end of iteration.
            Vector<DocumentLoader*> loaders;
            copyToVector(m_pendingMasterResourceLoaders, loaders);
            for (auto& loader : loaders)
                disassociateDocumentLoader(*loader); // This can delete this group.

            // Reinstate the oldNewestCache, if there was one.
            if (oldNewestCache) {
                // This will discard the failed new cache.
                setNewestCache(oldNewestCache.releaseNonNull());
            } else {
                // We must have been deleted by the last call to disassociateDocumentLoader().
                return;
            }
        }
        break;
    }
    }

    // Empty cache group's list of pending master entries.
    m_pendingMasterResourceLoaders.clear();
    m_completionType = None;
    setUpdateStatus(Idle);
    m_frame = nullptr;
    m_availableSpaceInQuota = ApplicationCacheStorage::unknownQuota();
    m_calledReachedMaxAppCacheSize = false;
}

void ApplicationCacheGroup::startLoadingEntry()
{
    ASSERT(m_cacheBeingUpdated);

    if (m_pendingEntries.isEmpty()) {
        m_completionType = Completed;
        deliverDelayedMainResources();
        return;
    }

    auto firstPendingEntryURL = m_pendingEntries.begin()->key;

    postListenerTask(eventNames().progressEvent, m_progressTotal, m_progressDone, m_associatedDocumentLoaders);
    m_progressDone++;

    ASSERT(!m_currentHandle);
    m_currentHandle = createResourceHandle(URL(ParsedURLString, firstPendingEntryURL), m_newestCache ? m_newestCache->resourceForURL(firstPendingEntryURL) : 0);
}

void ApplicationCacheGroup::deliverDelayedMainResources()
{
    // Need to copy loaders, because the cache group may be destroyed at the end of iteration.
    Vector<DocumentLoader*> loaders;
    copyToVector(m_pendingMasterResourceLoaders, loaders);
    for (auto* loader : loaders) {
        if (loader->isLoadingMainResource())
            continue;
        if (loader->mainDocumentError().isNull())
            finishedLoadingMainResource(*loader);
        else
            failedLoadingMainResource(*loader);
    }
    if (loaders.isEmpty())
        checkIfLoadIsComplete();
}

void ApplicationCacheGroup::addEntry(const String& url, unsigned type)
{
    ASSERT(m_cacheBeingUpdated);
    ASSERT(!URL(ParsedURLString, url).hasFragmentIdentifier());

    // Don't add the URL if we already have an master resource in the cache
    // (i.e., the main resource finished loading before the manifest).
    if (auto* resource = m_cacheBeingUpdated->resourceForURL(url)) {
        ASSERT(resource->type() & ApplicationCacheResource::Master);
        ASSERT(!m_frame->loader().documentLoader()->isLoadingMainResource());
        resource->addType(type);
        return;
    }

    // Don't add the URL if it's the same as the manifest URL.
    ASSERT(m_manifestResource);
    if (m_manifestResource->url() == url) {
        m_manifestResource->addType(type);
        return;
    }

    m_pendingEntries.add(url, type).iterator->value |= type;
}

void ApplicationCacheGroup::associateDocumentLoaderWithCache(DocumentLoader* loader, ApplicationCache* cache)
{
    // If teardown started already, revive the group.
    if (!m_newestCache && !m_cacheBeingUpdated)
        m_newestCache = cache;

    ASSERT(!m_isObsolete);

    loader->applicationCacheHost().setApplicationCache(cache);

    ASSERT(!m_associatedDocumentLoaders.contains(loader));
    m_associatedDocumentLoaders.add(loader);
}

class ChromeClientCallbackTimer final : public TimerBase {
public:
    ChromeClientCallbackTimer(ApplicationCacheGroup& group)
        : m_group(group)
    {
    }

private:
    void fired() final
    {
        m_group.didReachMaxAppCacheSize();
        delete this;
    }

    // Note that there is no need to use a Ref here. The ApplicationCacheGroup instance is guaranteed
    // to be alive when the timer fires since invoking the callback is part of its normal
    // update machinery and nothing can yet cause it to get deleted.
    ApplicationCacheGroup& m_group;
};

void ApplicationCacheGroup::scheduleReachedMaxAppCacheSizeCallback()
{
    ASSERT(isMainThread());
    auto* timer = new ChromeClientCallbackTimer(*this);
    timer->startOneShot(0);
    // The timer will delete itself once it fires.
}

void ApplicationCacheGroup::postListenerTask(const AtomicString& eventType, int progressTotal, int progressDone, const HashSet<DocumentLoader*>& loaderSet)
{
    for (auto& loader : loaderSet)
        postListenerTask(eventType, progressTotal, progressDone, *loader);
}

void ApplicationCacheGroup::postListenerTask(const AtomicString& eventType, int progressTotal, int progressDone, DocumentLoader& loader)
{
    auto* frame = loader.frame();
    if (!frame)
        return;

    ASSERT(frame->loader().documentLoader() == &loader);

    RefPtr<DocumentLoader> protectedLoader(&loader);
    frame->document()->postTask([protectedLoader, &eventType, progressTotal, progressDone] (ScriptExecutionContext& context) {
        ASSERT_UNUSED(context, context.isDocument());
        auto* frame = protectedLoader->frame();
        if (!frame)
            return;

        ASSERT(frame->loader().documentLoader() == protectedLoader);
        protectedLoader->applicationCacheHost().notifyDOMApplicationCache(eventType, progressTotal, progressDone);
    });
}

void ApplicationCacheGroup::setUpdateStatus(UpdateStatus status)
{
    m_updateStatus = status;
}

void ApplicationCacheGroup::clearStorageID()
{
    m_storageID = 0;
    for (auto& cache : m_caches)
        cache->clearStorageID();
}

}
