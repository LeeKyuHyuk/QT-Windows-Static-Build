/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef PROFILE_IO_DATA_QT_H
#define PROFILE_IO_DATA_QT_H

#include "profile_adapter.h"
#include "content/public/browser/browsing_data_remover.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "extensions/buildflags/buildflags.h"
#include "services/proxy_resolver/public/mojom/proxy_resolver.mojom.h"

#include <QtCore/QString>
#include <QtCore/QPointer>
#include <QtCore/QMutex>

namespace net {
class ClientCertStore;
class DhcpPacFileFetcherFactory;
class HttpAuthPreferences;
class HttpNetworkSession;
class NetworkDelegate;
class ProxyConfigService;
class URLRequestContext;
class URLRequestContextStorage;
class URLRequestJobFactoryImpl;
class TransportSecurityPersister;
}

namespace extensions {
class ExtensionSystemQt;
}

namespace QtWebEngineCore {

struct ClientCertificateStoreData;
class ProfileIODataQt;
class ProfileQt;


class BrowsingDataRemoverObserverQt : public content::BrowsingDataRemover::Observer {
public:
    BrowsingDataRemoverObserverQt(ProfileIODataQt *profileIOData);

    void OnBrowsingDataRemoverDone() override;

private:
    ProfileIODataQt *m_profileIOData;
};


// ProfileIOData contains data that lives on the IOthread
// we still use shared memebers and use mutex which breaks
// idea for this object, but this is wip.

class ProfileIODataQt {

public:
    ProfileIODataQt(ProfileQt *profile); // runs on ui thread
    virtual ~ProfileIODataQt();

    QPointer<ProfileAdapter> profileAdapter();
    content::ResourceContext *resourceContext();
    net::URLRequestContext *urlRequestContext();
#if BUILDFLAG(ENABLE_EXTENSIONS)
    extensions::ExtensionSystemQt* GetExtensionSystem();
#endif // BUILDFLAG(ENABLE_EXTENSIONS)

    void initializeOnIOThread();
    void initializeOnUIThread(); // runs on ui thread
    void shutdownOnUIThread(); // runs on ui thread

    void cancelAllUrlRequests();
    void generateAllStorage();
    void generateStorage();
    void generateCookieStore();
    void generateHttpCache();
    void generateUserAgent();
    void generateJobFactory();
    void regenerateJobFactory();
    bool canSetCookie(const QUrl &firstPartyUrl, const QByteArray &cookieLine, const QUrl &url) const;
    bool canGetCookies(const QUrl &firstPartyUrl, const QUrl &url) const;
    void setGlobalCertificateVerification();

    // Used in NetworkDelegateQt::OnBeforeURLRequest.
    QWebEngineUrlRequestInterceptor *acquireInterceptor();
    void releaseInterceptor();
    QWebEngineUrlRequestInterceptor *requestInterceptor();

    void setRequestContextData(content::ProtocolHandlerMap *protocolHandlers,
                               content::URLRequestInterceptorScopedVector request_interceptors);
    void setFullConfiguration(); // runs on ui thread
    void updateStorageSettings(); // runs on ui thread
    void updateUserAgent(); // runs on ui thread
    void updateCookieStore(); // runs on ui thread
    void updateHttpCache(); // runs on ui thread
    void updateJobFactory(); // runs on ui thread
    void updateRequestInterceptor(); // runs on ui thread
    void requestStorageGeneration(); //runs on ui thread
    void createProxyConfig(); //runs on ui thread
    void updateUsedForGlobalCertificateVerification(); // runs on ui thread
    bool hasPageInterceptors();

#if QT_CONFIG(ssl)
    ClientCertificateStoreData *clientCertificateStoreData();
#endif
    std::unique_ptr<net::ClientCertStore> CreateClientCertStore();
    static ProfileIODataQt *FromResourceContext(content::ResourceContext *resource_context);
private:
    void removeBrowsingDataRemoverObserver();

    ProfileQt *m_profile;
    std::unique_ptr<net::URLRequestContextStorage> m_storage;
    std::unique_ptr<net::NetworkDelegate> m_networkDelegate;
    std::unique_ptr<content::ResourceContext> m_resourceContext;
    std::unique_ptr<net::URLRequestContext> m_urlRequestContext;
    std::unique_ptr<net::HttpNetworkSession> m_httpNetworkSession;
    std::unique_ptr<ProtocolHandlerRegistry::JobInterceptorFactory> m_protocolHandlerInterceptor;
    std::unique_ptr<net::DhcpPacFileFetcherFactory> m_dhcpPacFileFetcherFactory;
    std::unique_ptr<net::HttpAuthPreferences> m_httpAuthPreferences;
    std::unique_ptr<net::URLRequestJobFactory> m_jobFactory;
    std::unique_ptr<net::TransportSecurityPersister> m_transportSecurityPersister;
    base::WeakPtr<ProfileIODataQt> m_weakPtr;
    scoped_refptr<CookieMonsterDelegateQt> m_cookieDelegate;
    content::URLRequestInterceptorScopedVector m_requestInterceptors;
    content::ProtocolHandlerMap m_protocolHandlers;
    mojo::InterfacePtrInfo<proxy_resolver::mojom::ProxyResolverFactory> m_proxyResolverFactoryInterface;
    net::URLRequestJobFactoryImpl *m_baseJobFactory = nullptr;
    QAtomicPointer<net::ProxyConfigService> m_proxyConfigService;
    QPointer<ProfileAdapter> m_profileAdapter; // never dereferenced in IO thread and it is passed by qpointer
    ProfileAdapter::PersistentCookiesPolicy m_persistentCookiesPolicy;
#if QT_CONFIG(ssl)
    ClientCertificateStoreData *m_clientCertificateStoreData;
#endif
    QString m_cookiesPath;
    QString m_channelIdPath;
    QString m_httpAcceptLanguage;
    QString m_httpUserAgent;
    ProfileAdapter::HttpCacheType m_httpCacheType;
    QString m_httpCachePath;
    QList<QByteArray> m_customUrlSchemes;
    QList<QByteArray> m_installedCustomSchemes;
    QWebEngineUrlRequestInterceptor* m_requestInterceptor = nullptr;
    QMutex m_mutex;
    int m_httpCacheMaxSize = 0;
    bool m_initialized = false;
    bool m_updateAllStorage = false;
    bool m_updateJobFactory = false;
    bool m_ignoreCertificateErrors = false;
    bool m_useForGlobalCertificateVerification = false;
    bool m_hasPageInterceptors = false;
    BrowsingDataRemoverObserverQt m_removerObserver;
    base::WeakPtrFactory<ProfileIODataQt> m_weakPtrFactory; // this should be always the last member
    QString m_dataPath;
    bool m_pendingStorageRequestGeneration = false;
    DISALLOW_COPY_AND_ASSIGN(ProfileIODataQt);

    friend class BrowsingDataRemoverObserverQt;
};
} // namespace QtWebEngineCore

#endif // PROFILE_IO_DATA_QT_H
