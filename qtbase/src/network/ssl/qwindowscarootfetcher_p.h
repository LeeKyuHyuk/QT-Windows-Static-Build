/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtNetwork module of the Qt Toolkit.
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

#ifndef QWINDOWSCAROOTFETCHER_P_H
#define QWINDOWSCAROOTFETCHER_P_H

#include <QtCore/QtGlobal>
#include <QtCore/QObject>

#include "qsslsocket_p.h"

#include "qsslsocket.h"
#include "qsslcertificate.h"

#include <memory>

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

QT_BEGIN_NAMESPACE

class QWindowsCaRootFetcher : public QObject
{
    Q_OBJECT
public:
    QWindowsCaRootFetcher(const QSslCertificate &certificate, QSslSocket::SslMode sslMode,
                          const QList<QSslCertificate> &caCertificates = {},
                          const QString &hostName = {});
    ~QWindowsCaRootFetcher();
public slots:
    void start();
signals:
    void finished(QSslCertificate brokenChain, QSslCertificate caroot);
private:
    QHCertStorePointer createAdditionalStore() const;

    QSslCertificate cert;
    QSslSocket::SslMode mode;
    // In case the application set CA certificates in the configuration,
    // in the past we did not load missing certs. But this disables
    // recoverable case when a certificate has Authority Information Access
    // extension. So we try to fetch in this scenario also, but in case
    // explicitly trusted root was not in a system store, we'll do
    // additional checks, thus we need 'peerVerifyName':
    QList<QSslCertificate> explicitlyTrustedCAs;
    QString peerVerifyName;
};

QT_END_NAMESPACE

#endif // QWINDOWSCAROOTFETCHER_P_H
