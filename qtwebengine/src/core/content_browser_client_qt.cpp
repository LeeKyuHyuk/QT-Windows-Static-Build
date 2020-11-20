/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "content_browser_client_qt.h"

#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/strings/utf_string_conversions.h"
#include "base/message_loop/message_loop.h"
#include "base/task/post_task.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry_factory.h"
#if QT_CONFIG(webengine_spellchecker)
#include "chrome/browser/spellchecker/spell_check_host_chrome_impl.h"
#endif
#include "components/guest_view/browser/guest_view_base.h"
#include "components/network_hints/browser/network_hints_message_filter.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/common/url_schemes.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/client_certificate_delegate.h"
#include "content/public/browser/media_observer.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/resource_dispatcher_host_delegate.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/service_names.mojom.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/user_agent.h"
#include "media/media_buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "printing/buildflags/buildflags.h"
#include "qtwebengine/browser/qtwebengine_content_browser_overlay_manifest.h"
#include "qtwebengine/browser/qtwebengine_content_renderer_overlay_manifest.h"
#include "qtwebengine/browser/qtwebengine_packaged_service_manifest.h"
#include "qtwebengine/browser/qtwebengine_renderer_manifest.h"
#include "net/ssl/client_cert_identity.h"
#include "net/ssl/client_cert_store.h"
#include "services/service_manager/public/cpp/connector.h"
#include "services/service_manager/public/cpp/service.h"
#include "services/service_manager/sandbox/switches.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/mojom/insecure_input/insecure_input_service.mojom.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gpu_timing.h"
#include "url/url_util_qt.h"

#include "qtwebengine/common/renderer_configuration.mojom.h"
#include "qtwebengine/grit/qt_webengine_resources.h"

#include "profile_adapter.h"
#include "browser_main_parts_qt.h"
#include "browser_message_filter_qt.h"
#include "certificate_error_controller.h"
#include "certificate_error_controller_p.h"
#include "client_cert_select_controller.h"
#include "devtools_manager_delegate_qt.h"
#include "login_delegate_qt.h"
#include "media_capture_devices_dispatcher.h"
#include "net/network_delegate_qt.h"
#include "net/url_request_context_getter_qt.h"
#include "platform_notification_service_qt.h"
#if QT_CONFIG(webengine_printing_and_pdf)
#include "printing/printing_message_filter_qt.h"
#endif
#include "profile_qt.h"
#include "profile_io_data_qt.h"
#include "quota_permission_context_qt.h"
#include "renderer_host/user_resource_controller_host.h"
#include "service/service_qt.h"
#include "type_conversion.h"
#include "web_contents_delegate_qt.h"
#include "web_engine_context.h"
#include "web_engine_library_info.h"
#include "api/qwebenginecookiestore.h"
#include "api/qwebenginecookiestore_p.h"

#if defined(Q_OS_LINUX)
#include "global_descriptors_qt.h"
#include "ui/base/resource/resource_bundle.h"
#endif

#if QT_CONFIG(webengine_pepper_plugins)
#include "content/public/browser/browser_ppapi_host.h"
#include "ppapi/host/ppapi_host.h"
#include "renderer_host/pepper/pepper_host_factory_qt.h"
#endif

#if QT_CONFIG(webengine_geolocation)
#include "location_provider_qt.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/extensions_browser_client_qt.h"
#include "extensions/browser/extension_message_filter.h"
#include "extensions/browser/guest_view/extensions_guest_view_message_filter.h"
#include "extensions/browser/io_thread_extension_message_filter.h"
#include "extensions/common/constants.h"
#include "common/extensions/extensions_client_qt.h"
#include "renderer_host/resource_dispatcher_host_delegate_qt.h"
#endif

#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
#include "media/mojo/interfaces/constants.mojom.h"
#include "media/mojo/services/media_service_factory.h"
#endif

#include <QGuiApplication>
#include <QLocale>
#if QT_CONFIG(opengl)
# include <QOpenGLContext>
# include <QOpenGLExtraFunctions>
#endif
#include <qpa/qplatformnativeinterface.h>

QT_BEGIN_NAMESPACE
Q_GUI_EXPORT QOpenGLContext *qt_gl_global_share_context();
QT_END_NAMESPACE

namespace QtWebEngineCore {

class QtShareGLContext : public gl::GLContext {
public:
    QtShareGLContext(QOpenGLContext *qtContext)
        : gl::GLContext(0)
        , m_handle(0)
    {
        QString platform = qApp->platformName().toLower();
        QPlatformNativeInterface *pni = QGuiApplication::platformNativeInterface();
        if (platform == QLatin1String("xcb") || platform == QLatin1String("offscreen")) {
            if (gl::GetGLImplementation() == gl::kGLImplementationEGLGLES2)
                m_handle = pni->nativeResourceForContext(QByteArrayLiteral("eglcontext"), qtContext);
            else
                m_handle = pni->nativeResourceForContext(QByteArrayLiteral("glxcontext"), qtContext);
        } else if (platform == QLatin1String("cocoa"))
            m_handle = pni->nativeResourceForContext(QByteArrayLiteral("cglcontextobj"), qtContext);
        else if (platform == QLatin1String("qnx"))
            m_handle = pni->nativeResourceForContext(QByteArrayLiteral("eglcontext"), qtContext);
        else if (platform == QLatin1String("eglfs") || platform == QLatin1String("wayland")
                 || platform == QLatin1String("wayland-egl"))
            m_handle = pni->nativeResourceForContext(QByteArrayLiteral("eglcontext"), qtContext);
        else if (platform == QLatin1String("windows")) {
            if (gl::GetGLImplementation() == gl::kGLImplementationEGLGLES2)
                m_handle = pni->nativeResourceForContext(QByteArrayLiteral("eglContext"), qtContext);
            else
                m_handle = pni->nativeResourceForContext(QByteArrayLiteral("renderingcontext"), qtContext);
        } else {
            qFatal("%s platform not yet supported", platform.toLatin1().constData());
            // Add missing platforms once they work.
            Q_UNREACHABLE();
        }
    }

    void* GetHandle() override { return m_handle; }
    unsigned int CheckStickyGraphicsResetStatus() override
    {
#if QT_CONFIG(opengl)
        if (QOpenGLContext *context = qt_gl_global_share_context()) {
            if (context->format().testOption(QSurfaceFormat::ResetNotification))
                return context->extraFunctions()->glGetGraphicsResetStatus();
        }
#endif
        return 0 /*GL_NO_ERROR*/;
    }

    // We don't care about the rest, this context shouldn't be used except for its handle.
    bool Initialize(gl::GLSurface *, const gl::GLContextAttribs &) override { Q_UNREACHABLE(); return false; }
    bool MakeCurrent(gl::GLSurface *) override { Q_UNREACHABLE(); return false; }
    void ReleaseCurrent(gl::GLSurface *) override { Q_UNREACHABLE(); }
    bool IsCurrent(gl::GLSurface *) override { Q_UNREACHABLE(); return false; }
    scoped_refptr<gl::GPUTimingClient> CreateGPUTimingClient() override
    {
        return nullptr;
    }
    const gfx::ExtensionSet& GetExtensions() override
    {
        static const gfx::ExtensionSet s_emptySet;
        return s_emptySet;
    }
    void ResetExtensions() override
    {
    }

private:
    void *m_handle;
};

class ShareGroupQtQuick : public gl::GLShareGroup {
public:
    gl::GLContext* GetContext() override { return m_shareContextQtQuick.get(); }
    void AboutToAddFirstContext() override;

private:
    scoped_refptr<QtShareGLContext> m_shareContextQtQuick;
};

void ShareGroupQtQuick::AboutToAddFirstContext()
{
#ifndef QT_NO_OPENGL
    // This currently has to be setup by ::main in all applications using QQuickWebEngineView with delegated rendering.
    QOpenGLContext *shareContext = qt_gl_global_share_context();
    if (!shareContext) {
        qFatal("QWebEngine: OpenGL resource sharing is not set up in QtQuick. Please make sure to call QtWebEngine::initialize() in your main() function before QCoreApplication is created.");
    }
    m_shareContextQtQuick = new QtShareGLContext(shareContext);
#endif
}

ContentBrowserClientQt::ContentBrowserClientQt()
{
}

ContentBrowserClientQt::~ContentBrowserClientQt()
{
}

std::unique_ptr<content::BrowserMainParts> ContentBrowserClientQt::CreateBrowserMainParts(const content::MainFunctionParams&)
{
    return std::make_unique<BrowserMainPartsQt>();
}

void ContentBrowserClientQt::RenderProcessWillLaunch(content::RenderProcessHost* host,
                                                     service_manager::mojom::ServiceRequest *service_request)
{
    const int id = host->GetID();
    Profile *profile = Profile::FromBrowserContext(host->GetBrowserContext());
    base::PostTaskWithTraitsAndReplyWithResult(
            FROM_HERE, {content::BrowserThread::IO},
            base::BindOnce(&net::URLRequestContextGetter::GetURLRequestContext, base::Unretained(profile->GetRequestContext())),
            base::BindOnce(&ContentBrowserClientQt::AddNetworkHintsMessageFilter, base::Unretained(this), id));

    // FIXME: Add a settings variable to enable/disable the file scheme.
    content::ChildProcessSecurityPolicy::GetInstance()->GrantRequestScheme(id, url::kFileScheme);
    static_cast<ProfileQt*>(host->GetBrowserContext())->m_profileAdapter->userResourceController()->renderProcessStartedWithHost(host);
    host->AddFilter(new BrowserMessageFilterQt(id, profile));
#if QT_CONFIG(webengine_printing_and_pdf)
    host->AddFilter(new PrintingMessageFilterQt(host->GetID()));
#endif
#if BUILDFLAG(ENABLE_EXTENSIONS)
    host->AddFilter(new extensions::ExtensionMessageFilter(host->GetID(), host->GetBrowserContext()));
    host->AddFilter(new extensions::IOThreadExtensionMessageFilter(host->GetID(), host->GetBrowserContext()));
    host->AddFilter(new extensions::ExtensionsGuestViewMessageFilter(host->GetID(), host->GetBrowserContext()));
#endif //ENABLE_EXTENSIONS

    bool is_incognito_process = profile->IsOffTheRecord();
    qtwebengine::mojom::RendererConfigurationAssociatedPtr renderer_configuration;
    host->GetChannel()->GetRemoteAssociatedInterface(&renderer_configuration);
    renderer_configuration->SetInitialConfiguration(is_incognito_process);

    mojo::PendingRemote<service_manager::mojom::Service> service;
    *service_request = service.InitWithNewPipeAndPassReceiver();
    service_manager::Identity renderer_identity = host->GetChildIdentity();
    mojo::Remote<service_manager::mojom::ProcessMetadata> metadata;
    ServiceQt::GetInstance()->connector()->RegisterServiceInstance(
                service_manager::Identity("qtwebengine_renderer",
                                          renderer_identity.instance_group(),
                                          renderer_identity.instance_id(),
                                          base::Token::CreateRandom()),
                std::move(service), metadata.BindNewPipeAndPassReceiver());
}

void ContentBrowserClientQt::ResourceDispatcherHostCreated()
{
#if BUILDFLAG(ENABLE_EXTENSIONS)
    m_resourceDispatcherHostDelegate.reset(new ResourceDispatcherHostDelegateQt);
#else
    m_resourceDispatcherHostDelegate.reset(new content::ResourceDispatcherHostDelegate);
#endif
    content::ResourceDispatcherHost::Get()->SetDelegate(m_resourceDispatcherHostDelegate.get());
}

gl::GLShareGroup *ContentBrowserClientQt::GetInProcessGpuShareGroup()
{
    if (!m_shareGroupQtQuick.get())
        m_shareGroupQtQuick = new ShareGroupQtQuick;
    return m_shareGroupQtQuick.get();
}

content::MediaObserver *ContentBrowserClientQt::GetMediaObserver()
{
    return MediaCaptureDevicesDispatcher::GetInstance();
}

void ContentBrowserClientQt::OverrideWebkitPrefs(content::RenderViewHost *rvh, content::WebPreferences *web_prefs)
{
    if (content::WebContents *webContents = rvh->GetDelegate()->GetAsWebContents()) {
#if BUILDFLAG(ENABLE_EXTENSIONS)
        if (guest_view::GuestViewBase::IsGuest(webContents))
            return;
#endif // BUILDFLAG(ENABLE_EXTENSIONS)
        WebContentsDelegateQt* delegate = static_cast<WebContentsDelegateQt*>(webContents->GetDelegate());
        if (delegate)
            delegate->overrideWebPreferences(webContents, web_prefs);
    }
}

scoped_refptr<content::QuotaPermissionContext> ContentBrowserClientQt::CreateQuotaPermissionContext()
{
    return new QuotaPermissionContextQt;
}

void ContentBrowserClientQt::GetQuotaSettings(content::BrowserContext* context,
                                              content::StoragePartition* partition,
                                              storage::OptionalQuotaSettingsCallback callback)
{
    storage::GetNominalDynamicSettings(partition->GetPath(), context->IsOffTheRecord(), storage::GetDefaultDiskInfoHelper(), std::move(callback));
}

// Copied from chrome/browser/ssl/ssl_error_handler.cc:
static int IsCertErrorFatal(int cert_error)
{
    switch (cert_error) {
    case net::ERR_CERT_COMMON_NAME_INVALID:
    case net::ERR_CERT_DATE_INVALID:
    case net::ERR_CERT_AUTHORITY_INVALID:
    case net::ERR_CERT_WEAK_SIGNATURE_ALGORITHM:
    case net::ERR_CERT_WEAK_KEY:
    case net::ERR_CERT_NAME_CONSTRAINT_VIOLATION:
    case net::ERR_CERT_VALIDITY_TOO_LONG:
    case net::ERR_CERTIFICATE_TRANSPARENCY_REQUIRED:
    case net::ERR_CERT_SYMANTEC_LEGACY:
        return false;
    case net::ERR_CERT_CONTAINS_ERRORS:
    case net::ERR_CERT_REVOKED:
    case net::ERR_CERT_INVALID:
    case net::ERR_SSL_WEAK_SERVER_EPHEMERAL_DH_KEY:
    case net::ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN:
        return true;
    default:
        NOTREACHED();
    }
    return true;
}

void ContentBrowserClientQt::AllowCertificateError(content::WebContents *webContents,
                                                   int cert_error,
                                                   const net::SSLInfo &ssl_info,
                                                   const GURL &request_url,
                                                   bool is_main_frame_request,
                                                   bool strict_enforcement,
                                                   bool expired_previous_decision,
                                                   const base::Callback<void(content::CertificateRequestResultType)> &callback)
{
    WebContentsDelegateQt* contentsDelegate = static_cast<WebContentsDelegateQt*>(webContents->GetDelegate());

    QSharedPointer<CertificateErrorController> errorController(
            new CertificateErrorController(
                    new CertificateErrorControllerPrivate(
                            cert_error,
                            ssl_info,
                            request_url,
                            is_main_frame_request,
                            IsCertErrorFatal(cert_error),
                            strict_enforcement,
                            callback)));
    contentsDelegate->allowCertificateError(errorController);
}


base::OnceClosure ContentBrowserClientQt::SelectClientCertificate(content::WebContents *webContents,
                                                                  net::SSLCertRequestInfo *certRequestInfo,
                                                                  net::ClientCertIdentityList clientCerts,
                                                                  std::unique_ptr<content::ClientCertificateDelegate> delegate)
{
    if (!clientCerts.empty()) {
        WebContentsDelegateQt* contentsDelegate = static_cast<WebContentsDelegateQt*>(webContents->GetDelegate());

        QSharedPointer<ClientCertSelectController> certSelectController(
                new ClientCertSelectController(certRequestInfo, std::move(clientCerts), std::move(delegate)));

        contentsDelegate->selectClientCert(certSelectController);
    } else {
        delegate->ContinueWithCertificate(nullptr, nullptr);
    }
    // This is consistent with AwContentBrowserClient and CastContentBrowserClient:
    return base::OnceClosure();
}

std::unique_ptr<net::ClientCertStore> ContentBrowserClientQt::CreateClientCertStore(content::ResourceContext *resource_context)
{
    if (!resource_context)
        return nullptr;

    return ProfileIODataQt::FromResourceContext(resource_context)->CreateClientCertStore();
}

std::string ContentBrowserClientQt::GetApplicationLocale()
{
    return WebEngineLibraryInfo::getApplicationLocale();
}

std::string ContentBrowserClientQt::GetAcceptLangs(content::BrowserContext *context)
{
    return static_cast<ProfileQt*>(context)->profileAdapter()->httpAcceptLanguage().toStdString();
}

void ContentBrowserClientQt::AppendExtraCommandLineSwitches(base::CommandLine* command_line, int child_process_id)
{
    Q_UNUSED(child_process_id);

    url::CustomScheme::SaveSchemes(command_line);

    std::string processType = command_line->GetSwitchValueASCII(switches::kProcessType);
    if (processType == service_manager::switches::kZygoteProcess)
        command_line->AppendSwitchASCII(switches::kLang, GetApplicationLocale());
}

void ContentBrowserClientQt::GetAdditionalWebUISchemes(std::vector<std::string>* additional_schemes)
{
    additional_schemes->push_back(content::kChromeDevToolsScheme);
}

void ContentBrowserClientQt::GetAdditionalViewSourceSchemes(std::vector<std::string>* additional_schemes)
{
   additional_schemes->push_back(content::kChromeDevToolsScheme);
}

#if defined(Q_OS_LINUX)
void ContentBrowserClientQt::GetAdditionalMappedFilesForChildProcess(const base::CommandLine& command_line, int child_process_id, content::PosixFileDescriptorInfo* mappings)
{
    const std::string &locale = GetApplicationLocale();
    const base::FilePath &locale_file_path = ui::ResourceBundle::GetSharedInstance().GetLocaleFilePath(locale, true);
    if (locale_file_path.empty())
        return;

    // Open pak file of the current locale in the Browser process and pass its file descriptor to the sandboxed
    // Renderer Process. FileDescriptorInfo is responsible for closing the file descriptor.
    int flags = base::File::FLAG_OPEN | base::File::FLAG_READ;
    base::File locale_file = base::File(locale_file_path, flags);
    mappings->Transfer(kWebEngineLocale, base::ScopedFD(locale_file.TakePlatformFile()));
}
#endif

#if QT_CONFIG(webengine_pepper_plugins)
void ContentBrowserClientQt::DidCreatePpapiPlugin(content::BrowserPpapiHost* browser_host)
{
    browser_host->GetPpapiHost()->AddHostFactoryFilter(
                std::make_unique<QtWebEngineCore::PepperHostFactoryQt>(browser_host));
}
#endif

content::DevToolsManagerDelegate* ContentBrowserClientQt::GetDevToolsManagerDelegate()
{
    return new DevToolsManagerDelegateQt;
}

content::PlatformNotificationService *ContentBrowserClientQt::GetPlatformNotificationService(content::BrowserContext *browser_context)
{
    ProfileQt *profile = static_cast<ProfileQt *>(browser_context);
    if (!profile)
        return nullptr;
    return profile->platformNotificationService();
}

// This is a really complicated way of doing absolutely nothing, but Mojo demands it:
class ServiceDriver
        : public blink::mojom::InsecureInputService
        , public content::WebContentsUserData<ServiceDriver>
{
public:
    static void CreateForRenderFrameHost(content::RenderFrameHost *renderFrameHost)
    {
        content::WebContents* web_contents = content::WebContents::FromRenderFrameHost(renderFrameHost);
        if (!web_contents)
            return;
        CreateForWebContents(web_contents);

    }
    static ServiceDriver* FromRenderFrameHost(content::RenderFrameHost *renderFrameHost)
    {
        content::WebContents* web_contents = content::WebContents::FromRenderFrameHost(renderFrameHost);
        if (!web_contents)
            return nullptr;
        return FromWebContents(web_contents);
    }
    static void BindInsecureInputService(blink::mojom::InsecureInputServiceRequest request, content::RenderFrameHost *render_frame_host)
    {
        CreateForRenderFrameHost(render_frame_host);
        ServiceDriver *driver = FromRenderFrameHost(render_frame_host);

        if (driver)
            driver->BindInsecureInputServiceRequest(std::move(request));
    }
    void BindInsecureInputServiceRequest(blink::mojom::InsecureInputServiceRequest request)
    {
        m_insecureInputServiceBindings.AddBinding(this, std::move(request));
    }

    // blink::mojom::InsecureInputService:
    void DidEditFieldInInsecureContext() override
    { }

private:
    WEB_CONTENTS_USER_DATA_KEY_DECL();
    explicit ServiceDriver(content::WebContents* /*web_contents*/) { }
    friend class content::WebContentsUserData<ServiceDriver>;
    mojo::BindingSet<blink::mojom::InsecureInputService> m_insecureInputServiceBindings;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(ServiceDriver)

void ContentBrowserClientQt::InitFrameInterfaces()
{
    m_frameInterfaces = std::make_unique<service_manager::BinderRegistry>();
    m_frameInterfacesParameterized = std::make_unique<service_manager::BinderRegistryWithArgs<content::RenderFrameHost*>>();
    m_frameInterfacesParameterized->AddInterface(base::BindRepeating(&ServiceDriver::BindInsecureInputService));
}

void ContentBrowserClientQt::BindInterfaceRequestFromFrame(content::RenderFrameHost* render_frame_host,
                                                           const std::string& interface_name,
                                                           mojo::ScopedMessagePipeHandle interface_pipe)
{
    if (!m_frameInterfaces.get() && !m_frameInterfacesParameterized.get())
        InitFrameInterfaces();

    if (!m_frameInterfacesParameterized->TryBindInterface(interface_name, &interface_pipe, render_frame_host))
        m_frameInterfaces->TryBindInterface(interface_name, &interface_pipe);
}

void ContentBrowserClientQt::RunServiceInstance(const service_manager::Identity &identity,
                                                mojo::PendingReceiver<service_manager::mojom::Service> *receiver)
{
#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
    if (identity.name() == media::mojom::kMediaServiceName) {
        service_manager::Service::RunAsyncUntilTermination(media::CreateMediaService(std::move(*receiver)));
        return;
    }
#endif

    content::ContentBrowserClient::RunServiceInstance(identity, receiver);
}

void ContentBrowserClientQt::RunServiceInstanceOnIOThread(const service_manager::Identity &identity,
                                                          mojo::PendingReceiver<service_manager::mojom::Service> *receiver)
{
    if (identity.name() == "qtwebengine") {
        ServiceQt::GetInstance()->CreateServiceQtRequestHandler().Run(std::move(*receiver));
        return;
    }

    content::ContentBrowserClient::RunServiceInstance(identity, receiver);
}

base::Optional<service_manager::Manifest> ContentBrowserClientQt::GetServiceManifestOverlay(base::StringPiece name)
{
    if (name == content::mojom::kBrowserServiceName)
        return GetQtWebEngineContentBrowserOverlayManifest();
    else if (name == content::mojom::kRendererServiceName)
        return GetQtWebEngineContentRendererOverlayManifest();

    return base::nullopt;
}

std::vector<service_manager::Manifest> ContentBrowserClientQt::GetExtraServiceManifests()
{
    auto manifests = GetQtWebEnginePackagedServiceManifests();
    manifests.push_back(GetQtWebEngineRendererManifest());
    return manifests;
}

bool ContentBrowserClientQt::CanCreateWindow(
        content::RenderFrameHost* opener,
        const GURL& opener_url,
        const GURL& opener_top_level_frame_url,
        const url::Origin& source_origin,
        content::mojom::WindowContainerType container_type,
        const GURL& target_url,
        const content::Referrer& referrer,
        const std::string& frame_name,
        WindowOpenDisposition disposition,
        const blink::mojom::WindowFeatures& features,
        bool user_gesture,
        bool opener_suppressed,
        bool* no_javascript_access)
{
    Q_UNUSED(opener_url);
    Q_UNUSED(opener_top_level_frame_url);
    Q_UNUSED(source_origin);
    Q_UNUSED(container_type);
    Q_UNUSED(target_url);
    Q_UNUSED(referrer);
    Q_UNUSED(frame_name);
    Q_UNUSED(disposition);
    Q_UNUSED(features);
    Q_UNUSED(opener_suppressed);

    if (no_javascript_access)
        *no_javascript_access = false;

    content::WebContents* webContents = content::WebContents::FromRenderFrameHost(opener);

    WebEngineSettings *settings = nullptr;
    if (webContents) {
        WebContentsDelegateQt* delegate =
                static_cast<WebContentsDelegateQt*>(webContents->GetDelegate());
        if (delegate)
            settings = delegate->webEngineSettings();
    }

    return (settings && settings->getJavaScriptCanOpenWindowsAutomatically()) || user_gesture;
}

#if QT_CONFIG(webengine_geolocation)
std::unique_ptr<device::LocationProvider> ContentBrowserClientQt::OverrideSystemLocationProvider()
{
    return base::WrapUnique(new LocationProviderQt());
}
#endif

void ContentBrowserClientQt::AddNetworkHintsMessageFilter(int render_process_id, net::URLRequestContext *context)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    content::RenderProcessHost* host = content::RenderProcessHost::FromID(render_process_id);
    if (!host)
        return;

    content::BrowserMessageFilter *network_hints_message_filter =
            new network_hints::NetworkHintsMessageFilter(render_process_id);
    host->AddFilter(network_hints_message_filter);
}

bool ContentBrowserClientQt::ShouldEnableStrictSiteIsolation()
{
    // mirroring AwContentBrowserClient, CastContentBrowserClient and
    // HeadlessContentBrowserClient
    return false;
}

bool ContentBrowserClientQt::WillCreateRestrictedCookieManager(network::mojom::RestrictedCookieManagerRole role,
                                                               content::BrowserContext *browser_context,
                                                               const url::Origin &origin,
                                                               bool is_service_worker,
                                                               int process_id,
                                                               int routing_id,
                                                               network::mojom::RestrictedCookieManagerRequest *request)
{
    base::PostTaskWithTraits(
        FROM_HERE, {content::BrowserThread::IO},
        base::BindOnce(&ProfileIODataQt::CreateRestrictedCookieManager,
                       ProfileIODataQt::FromBrowserContext(browser_context)->getWeakPtrOnUIThread(),
                       std::move(*request),
                       role, origin, is_service_worker, process_id, routing_id));
    return true;
}

bool ContentBrowserClientQt::AllowAppCacheOnIO(const GURL &manifest_url,
                                               const GURL &first_party,
                                               content::ResourceContext *context)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    return ProfileIODataQt::FromResourceContext(context)->canGetCookies(toQt(first_party), toQt(manifest_url));
}

bool ContentBrowserClientQt::AllowAppCache(const GURL &manifest_url,
                                           const GURL &first_party,
                                           content::BrowserContext *context)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    return static_cast<ProfileQt *>(context)->profileAdapter()->cookieStore()->d_func()->canAccessCookies(toQt(first_party), toQt(manifest_url));
}

bool ContentBrowserClientQt::AllowServiceWorker(const GURL &scope,
                                                const GURL &first_party,
                                                const GURL & /*script_url*/,
                                                content::ResourceContext *context,
                                                base::RepeatingCallback<content::WebContents*()> wc_getter)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    // FIXME: Chrome also checks if javascript is enabled here to check if has been disabled since the service worker
    // was started.
    return ProfileIODataQt::FromResourceContext(context)->canGetCookies(toQt(first_party), toQt(scope));
}

// We control worker access to FS and indexed-db using cookie permissions, this is mirroring Chromium's logic.
void ContentBrowserClientQt::AllowWorkerFileSystem(const GURL &url,
                                                   content::ResourceContext *context,
                                                   const std::vector<content::GlobalFrameRoutingId> &/*render_frames*/,
                                                   base::Callback<void(bool)> callback)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    callback.Run(ProfileIODataQt::FromResourceContext(context)->canSetCookie(toQt(url), QByteArray(), toQt(url)));
}


bool ContentBrowserClientQt::AllowWorkerIndexedDB(const GURL &url,
                                                  content::ResourceContext *context,
                                                  const std::vector<content::GlobalFrameRoutingId> &/*render_frames*/)
{
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
    return ProfileIODataQt::FromResourceContext(context)->canSetCookie(toQt(url), QByteArray(), toQt(url));
}

static void LaunchURL(const GURL& url,
                      const content::ResourceRequestInfo::WebContentsGetter& web_contents_getter,
                      ui::PageTransition page_transition, bool is_main_frame, bool has_user_gesture)
{
    Q_ASSERT(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    content::WebContents* webContents = web_contents_getter.Run();
    if (!webContents)
        return;
    WebContentsDelegateQt *contentsDelegate = static_cast<WebContentsDelegateQt*>(webContents->GetDelegate());
    contentsDelegate->launchExternalURL(toQt(url), page_transition, is_main_frame, has_user_gesture);
}


bool ContentBrowserClientQt::HandleExternalProtocol(
        const GURL &url,
        content::ResourceRequestInfo::WebContentsGetter web_contents_getter,
        int child_id,
        content::NavigationUIData *navigation_data,
        bool is_main_frame,
        ui::PageTransition page_transition,
        bool has_user_gesture,
        network::mojom::URLLoaderFactoryPtr *out_factory)
{
    Q_ASSERT(content::BrowserThread::CurrentlyOn(content::BrowserThread::IO));
    Q_UNUSED(child_id);
    Q_UNUSED(navigation_data);
    Q_UNUSED(out_factory);

    base::PostTaskWithTraits(FROM_HERE, {content::BrowserThread::UI},
                             base::BindOnce(&LaunchURL,
                                            url,
                                            web_contents_getter,
                                            page_transition,
                                            is_main_frame,
                                            has_user_gesture));
    return true;
}

namespace {
// Copied from chrome/browser/chrome_content_browser_client.cc
template<class HandlerRegistry>
class ProtocolHandlerThrottle : public content::URLLoaderThrottle
{
public:
    explicit ProtocolHandlerThrottle(const HandlerRegistry &protocol_handler_registry)
        : protocol_handler_registry_(protocol_handler_registry)
    {
    }
    ~ProtocolHandlerThrottle() override = default;

    void WillStartRequest(network::ResourceRequest *request, bool *defer) override
    {
        TranslateUrl(&request->url);
    }

    void WillRedirectRequest(net::RedirectInfo *redirect_info,
                             const network::ResourceResponseHead &response_head, bool *defer,
                             std::vector<std::string> *to_be_removed_headers,
                             net::HttpRequestHeaders *modified_headers) override
    {
        TranslateUrl(&redirect_info->new_url);
    }

private:
    void TranslateUrl(GURL *url)
    {
        if (!protocol_handler_registry_->IsHandledProtocol(url->scheme()))
            return;
        GURL translated_url = protocol_handler_registry_->Translate(*url);
        if (!translated_url.is_empty())
            *url = translated_url;
    }

    HandlerRegistry protocol_handler_registry_;
};
} // namespace

std::vector<std::unique_ptr<content::URLLoaderThrottle>>
ContentBrowserClientQt::CreateURLLoaderThrottlesOnIO(
        const network::ResourceRequest & /*request*/, content::ResourceContext *resource_context,
        const base::RepeatingCallback<content::WebContents *()> & /*wc_getter*/,
        content::NavigationUIData * /*navigation_ui_data*/, int /*frame_tree_node_id*/)
{
    std::vector<std::unique_ptr<content::URLLoaderThrottle>> result;
    ProfileIODataQt *ioData = ProfileIODataQt::FromResourceContext(resource_context);
    result.push_back(std::make_unique<ProtocolHandlerThrottle<
                             scoped_refptr<ProtocolHandlerRegistry::IOThreadDelegate>>>(
            ioData->protocolHandlerRegistryIOThreadDelegate()));
    return result;
}

std::vector<std::unique_ptr<content::URLLoaderThrottle>>
ContentBrowserClientQt::CreateURLLoaderThrottles(
        const network::ResourceRequest &request, content::BrowserContext *browser_context,
        const base::RepeatingCallback<content::WebContents *()> &wc_getter,
        content::NavigationUIData *navigation_ui_data, int frame_tree_node_id)
{
    std::vector<std::unique_ptr<content::URLLoaderThrottle>> result;
    result.push_back(std::make_unique<ProtocolHandlerThrottle<ProtocolHandlerRegistry *>>(
            ProtocolHandlerRegistryFactory::GetForBrowserContext(browser_context)));
    return result;
}

std::unique_ptr<content::LoginDelegate> ContentBrowserClientQt::CreateLoginDelegate(
        const net::AuthChallengeInfo &authInfo,
        content::WebContents *web_contents,
        const content::GlobalRequestID & /*request_id*/,
        bool /*is_main_frame*/,
        const GURL &url,
        scoped_refptr<net::HttpResponseHeaders> /*response_headers*/,
        bool first_auth_attempt,
        LoginAuthRequiredCallback auth_required_callback)
{
    auto loginDelegate = std::make_unique<LoginDelegateQt>(authInfo, web_contents, url, first_auth_attempt, std::move(auth_required_callback));
    return loginDelegate;
}

bool ContentBrowserClientQt::ShouldIsolateErrorPage(bool in_main_frame)
{
    Q_UNUSED(in_main_frame);
    return false;
}

bool ContentBrowserClientQt::ShouldUseProcessPerSite(content::BrowserContext* browser_context, const GURL& effective_url)
{
#if BUILDFLAG(ENABLE_EXTENSIONS)
     if (effective_url.SchemeIs(extensions::kExtensionScheme))
        return true;
#endif
    return ContentBrowserClient::ShouldUseProcessPerSite(browser_context, effective_url);
}

std::string ContentBrowserClientQt::getUserAgent()
{
    // Mention the Chromium version we're based on to get passed stupid UA-string-based feature detection (several WebRTC demos need this)
    return content::BuildUserAgentFromProduct("QtWebEngine/" QTWEBENGINECORE_VERSION_STR " Chrome/" CHROMIUM_VERSION);
}

std::string ContentBrowserClientQt::GetProduct()
{
    QString productName(qApp->applicationName() % '/' % qApp->applicationVersion());
    return productName.toStdString();
}

} // namespace QtWebEngineCore
