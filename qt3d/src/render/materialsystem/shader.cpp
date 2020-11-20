/****************************************************************************
**
** Copyright (C) 2014 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
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

#include "shader_p.h"
#include "renderlogging_p.h"

#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QMutexLocker>
#include <qshaderprogram.h>
#include <Qt3DRender/private/attachmentpack_p.h>
#include <Qt3DRender/private/graphicscontext_p.h>
#include <Qt3DRender/private/qshaderprogram_p.h>
#include <Qt3DRender/private/stringtoint_p.h>
#include <Qt3DCore/qpropertyupdatedchange.h>

QT_BEGIN_NAMESPACE

using namespace Qt3DCore;

namespace Qt3DRender {
namespace Render {
const int Shader::modelMatrixNameId = StringToInt::lookupId(QLatin1String("modelMatrix"));
const int Shader::viewMatrixNameId = StringToInt::lookupId(QLatin1String("viewMatrix"));
const int Shader::projectionMatrixNameId = StringToInt::lookupId(QLatin1String("projectionMatrix"));
const int Shader::modelViewMatrixNameId = StringToInt::lookupId(QLatin1String("modelView"));
const int Shader::viewProjectionMatrixNameId = StringToInt::lookupId(QLatin1String("viewProjectionMatrix"));
const int Shader::modelViewProjectionNameId = StringToInt::lookupId(QLatin1String("modelViewProjection"));
const int Shader::mvpNameId = StringToInt::lookupId(QLatin1String("mvp"));
const int Shader::inverseModelMatrixNameId = StringToInt::lookupId(QLatin1String("inverseModelMatrix"));
const int Shader::inverseViewMatrixNameId = StringToInt::lookupId(QLatin1String("inverseViewMatrix"));
const int Shader::inverseProjectionMatrixNameId = StringToInt::lookupId(QLatin1String("inverseProjectionMatrix"));
const int Shader::inverseModelViewNameId = StringToInt::lookupId(QLatin1String("inverseModelView"));
const int Shader::inverseViewProjectionMatrixNameId = StringToInt::lookupId(QLatin1String("inverseViewProjectionMatrix"));
const int Shader::inverseModelViewProjectionNameId = StringToInt::lookupId(QLatin1String("inverseModelViewProjection"));
const int Shader::modelNormalMatrixNameId = StringToInt::lookupId(QLatin1String("modelNormalMatrix"));
const int Shader::modelViewNormalNameId = StringToInt::lookupId(QLatin1String("modelViewNormal"));
const int Shader::viewportMatrixNameId = StringToInt::lookupId(QLatin1String("viewportMatrix"));
const int Shader::inverseViewportMatrixNameId = StringToInt::lookupId(QLatin1String("inverseViewportMatrix"));
const int Shader::aspectRatioNameId = StringToInt::lookupId(QLatin1String("aspectRatio"));
const int Shader::exposureNameId = StringToInt::lookupId(QLatin1String("exposure"));
const int Shader::gammaNameId = StringToInt::lookupId(QLatin1String("gamma"));
const int Shader::timeNameId = StringToInt::lookupId(QLatin1String("time"));
const int Shader::eyePositionNameId = StringToInt::lookupId(QLatin1String("eyePosition"));
const int Shader::skinningPaletteNameId = StringToInt::lookupId(QLatin1String("skinningPalette[0]"));

Shader::Shader()
    : BackendNode(ReadWrite)
    , m_isLoaded(false)
    , m_dna(0)
    , m_oldDna(0)
    , m_graphicsContext(nullptr)
    , m_status(QShaderProgram::NotReady)
    , m_requiresFrontendSync(false)
{
    m_shaderCode.resize(static_cast<int>(QShaderProgram::Compute) + 1);
}

Shader::~Shader()
{
    // TO DO: ShaderProgram is leaked as of now
    // Fix that taking care that they may be shared given a same dna
    if (m_graphicsContext)
        QObject::disconnect(m_contextConnection);
}

void Shader::cleanup()
{
    // Remove this shader from the hash in the graphics context so
    // nothing tries to use it after it has been recycled
    {
        QMutexLocker lock(&m_mutex);
        if (m_graphicsContext)
            m_graphicsContext->removeShaderProgramReference(this);
        m_graphicsContext = nullptr;
        QObject::disconnect(m_contextConnection);
    }

    QBackendNode::setEnabled(false);
    m_isLoaded = false;
    m_dna = 0;
    m_oldDna = 0;
    m_uniformsNames.clear();
    m_attributesNames.clear();
    m_uniformBlockNames.clear();
    m_uniforms.clear();
    m_attributes.clear();
    m_uniformBlocks.clear();
    m_status = QShaderProgram::NotReady;
}

void Shader::syncFromFrontEnd(const QNode *frontEnd, bool firstTime)
{
    const QShaderProgram *node = qobject_cast<const QShaderProgram *>(frontEnd);
    if (!node)
        return;

    BackendNode::syncFromFrontEnd(frontEnd, firstTime);

    if (firstTime)
        for (int i = QShaderProgram::Vertex; i <= QShaderProgram::Compute; ++i)
            m_shaderCode[i].clear();

    for (int i = QShaderProgram::Vertex; i <= QShaderProgram::Compute; ++i) {
        const QShaderProgram::ShaderType shaderType = static_cast<QShaderProgram::ShaderType>(i);
        const QByteArray code = node->shaderCode(shaderType);
        if (code != m_shaderCode.value(shaderType))
            setShaderCode(shaderType, code);
    }
}

void Shader::setGraphicsContext(GraphicsContext *context)
{
    QMutexLocker lock(&m_mutex);
    m_graphicsContext = context;
    if (m_graphicsContext) {
        m_contextConnection = QObject::connect(m_graphicsContext->openGLContext(),
                                               &QOpenGLContext::aboutToBeDestroyed,
                                               [this] { setGraphicsContext(nullptr); });
    }
}

GraphicsContext *Shader::graphicsContext()
{
    QMutexLocker lock(&m_mutex);
    return m_graphicsContext;
}

QVector<QString> Shader::uniformsNames() const
{
    return m_uniformsNames;
}

QVector<QString> Shader::attributesNames() const
{
    return m_attributesNames;
}

QVector<QString> Shader::uniformBlockNames() const
{
    return m_uniformBlockNames;
}

QVector<QString> Shader::storageBlockNames() const
{
    return m_shaderStorageBlockNames;
}

QVector<QByteArray> Shader::shaderCode() const
{
    return m_shaderCode;
}

void Shader::setShaderCode(QShaderProgram::ShaderType type, const QByteArray &code)
{
    if (code == m_shaderCode[type])
        return;

    m_shaderCode[type] = code;
    m_isLoaded = false;
    m_status = QShaderProgram::NotReady;
    updateDNA();
    m_requiresFrontendSync = true;
    markDirty(AbstractRenderer::ShadersDirty);
}

QHash<QString, ShaderUniform> Shader::activeUniformsForUniformBlock(int blockIndex) const
{
    return m_uniformBlockIndexToShaderUniforms.value(blockIndex);
}

ShaderUniformBlock Shader::uniformBlockForBlockIndex(int blockIndex)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_index == blockIndex) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderUniformBlock Shader::uniformBlockForBlockNameId(int blockNameId)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_nameId == blockNameId) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderUniformBlock Shader::uniformBlockForBlockName(const QString &blockName)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_name == blockName) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockIndex(int blockIndex)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_index == blockIndex)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockNameId(int blockNameId)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_nameId == blockNameId)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockName(const QString &blockName)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_name == blockName)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

void Shader::prepareUniforms(ShaderParameterPack &pack)
{
    const PackUniformHash &values = pack.uniforms();

    auto it = values.keys.cbegin();
    const auto end = values.keys.cend();

    while (it != end) {
        // Find if there's a uniform with the same name id
        for (const ShaderUniform &uniform : qAsConst(m_uniforms)) {
            if (uniform.m_nameId == *it) {
                pack.setSubmissionUniform(uniform);
                break;
            }
        }
        ++it;
    }
}

void Shader::setFragOutputs(const QHash<QString, int> &fragOutputs)
{
    {
        QMutexLocker lock(&m_mutex);
        m_fragOutputs = fragOutputs;
    }
    updateDNA();
}

const QHash<QString, int> Shader::fragOutputs() const
{
    QMutexLocker lock(&m_mutex);
    return m_fragOutputs;
}

void Shader::updateDNA()
{
    m_oldDna = m_dna;
    uint codeHash = qHash(m_shaderCode[QShaderProgram::Vertex]
            + m_shaderCode[QShaderProgram::TessellationControl]
            + m_shaderCode[QShaderProgram::TessellationEvaluation]
            + m_shaderCode[QShaderProgram::Geometry]
            + m_shaderCode[QShaderProgram::Fragment]
            + m_shaderCode[QShaderProgram::Compute]);

    QMutexLocker locker(&m_mutex);
    uint attachmentHash = 0;
    QHash<QString, int>::const_iterator it = m_fragOutputs.cbegin();
    QHash<QString, int>::const_iterator end = m_fragOutputs.cend();
    while (it != end) {
        attachmentHash += ::qHash(it.value()) + ::qHash(it.key());
        ++it;
    }
    const ProgramDNA newDNA = codeHash + attachmentHash;

    // Remove reference to shader based on DNA in the ShaderCache
    // In turn this will allow to purge the shader program if no other
    // Shader backend node references it
    // Note: the purge is actually happening occasionally in GraphicsContext::beginDrawing
    if (m_graphicsContext && newDNA != m_oldDna)
        m_graphicsContext->removeShaderProgramReference(this);

    m_dna = newDNA;
}

void Shader::initializeUniforms(const QVector<ShaderUniform> &uniformsDescription)
{
    m_uniforms = uniformsDescription;
    m_uniformsNames.resize(uniformsDescription.size());
    m_uniformsNamesIds.reserve(uniformsDescription.size());
    m_standardUniformNamesIds.reserve(5);
    QHash<QString, ShaderUniform> activeUniformsInDefaultBlock;

    static const QVector<int> standardUniformNameIds = {
        modelMatrixNameId,
        viewMatrixNameId,
        projectionMatrixNameId,
        modelViewMatrixNameId,
        viewProjectionMatrixNameId,
        modelViewProjectionNameId,
        mvpNameId,
        inverseModelMatrixNameId,
        inverseViewMatrixNameId,
        inverseProjectionMatrixNameId,
        inverseModelViewNameId,
        inverseViewProjectionMatrixNameId,
        inverseModelViewProjectionNameId,
        modelNormalMatrixNameId,
        modelViewNormalNameId,
        viewportMatrixNameId,
        inverseViewportMatrixNameId,
        aspectRatioNameId,
        exposureNameId,
        gammaNameId,
        timeNameId,
        eyePositionNameId,
        skinningPaletteNameId,
    };

    for (int i = 0, m = uniformsDescription.size(); i < m; i++) {
        m_uniformsNames[i] = m_uniforms[i].m_name;
        const int nameId = StringToInt::lookupId(m_uniformsNames[i]);
        m_uniforms[i].m_nameId = nameId;

        // Is the uniform a Qt3D "Standard" uniform or a user defined one?
        if (standardUniformNameIds.contains(nameId))
            m_standardUniformNamesIds.push_back(nameId);
        else
            m_uniformsNamesIds.push_back(nameId);

        if (uniformsDescription[i].m_blockIndex == -1) { // Uniform is in default block
            qCDebug(Shaders) << "Active Uniform in Default Block " << uniformsDescription[i].m_name << uniformsDescription[i].m_blockIndex;
            activeUniformsInDefaultBlock.insert(uniformsDescription[i].m_name, uniformsDescription[i]);
        }
    }
    m_uniformBlockIndexToShaderUniforms.insert(-1, activeUniformsInDefaultBlock);
}

void Shader::initializeAttributes(const QVector<ShaderAttribute> &attributesDescription)
{
    m_attributes = attributesDescription;
    m_attributesNames.resize(attributesDescription.size());
    m_attributeNamesIds.resize(attributesDescription.size());
    for (int i = 0, m = attributesDescription.size(); i < m; i++) {
        m_attributesNames[i] = attributesDescription[i].m_name;
        m_attributes[i].m_nameId = StringToInt::lookupId(m_attributesNames[i]);
        m_attributeNamesIds[i] = m_attributes[i].m_nameId;
        qCDebug(Shaders) << "Active Attribute " << attributesDescription[i].m_name;
    }
}

void Shader::initializeUniformBlocks(const QVector<ShaderUniformBlock> &uniformBlockDescription)
{
    m_uniformBlocks = uniformBlockDescription;
    m_uniformBlockNames.resize(uniformBlockDescription.size());
    m_uniformBlockNamesIds.resize(uniformBlockDescription.size());
    for (int i = 0, m = uniformBlockDescription.size(); i < m; ++i) {
        m_uniformBlockNames[i] = m_uniformBlocks[i].m_name;
        m_uniformBlockNamesIds[i] = StringToInt::lookupId(m_uniformBlockNames[i]);
        m_uniformBlocks[i].m_nameId = m_uniformBlockNamesIds[i];
        qCDebug(Shaders) << "Initializing Uniform Block {" << m_uniformBlockNames[i] << "}";

        // Find all active uniforms for the shader block
        QVector<ShaderUniform>::const_iterator uniformsIt = m_uniforms.cbegin();
        const QVector<ShaderUniform>::const_iterator uniformsEnd = m_uniforms.cend();

        QVector<QString>::const_iterator uniformNamesIt = m_uniformsNames.cbegin();
        const QVector<QString>::const_iterator uniformNamesEnd = m_attributesNames.cend();

        QHash<QString, ShaderUniform> activeUniformsInBlock;

        while (uniformsIt != uniformsEnd && uniformNamesIt != uniformNamesEnd) {
            if (uniformsIt->m_blockIndex == uniformBlockDescription[i].m_index) {
                QString uniformName = *uniformNamesIt;
                if (!m_uniformBlockNames[i].isEmpty() && !uniformName.startsWith(m_uniformBlockNames[i]))
                    uniformName = m_uniformBlockNames[i] + QLatin1Char('.') + *uniformNamesIt;
                activeUniformsInBlock.insert(uniformName, *uniformsIt);
                qCDebug(Shaders) << "Active Uniform Block " << uniformName << " in block " << m_uniformBlockNames[i] << " at index " << uniformsIt->m_blockIndex;
            }
            ++uniformsIt;
            ++uniformNamesIt;
        }
        m_uniformBlockIndexToShaderUniforms.insert(uniformBlockDescription[i].m_index, activeUniformsInBlock);
    }
}

void Shader::initializeShaderStorageBlocks(const QVector<ShaderStorageBlock> &shaderStorageBlockDescription)
{
    m_shaderStorageBlocks = shaderStorageBlockDescription;
    m_shaderStorageBlockNames.resize(shaderStorageBlockDescription.size());
    m_shaderStorageBlockNamesIds.resize(shaderStorageBlockDescription.size());

    for (int i = 0, m = shaderStorageBlockDescription.size(); i < m; ++i) {
        m_shaderStorageBlockNames[i] = m_shaderStorageBlocks[i].m_name;
        m_shaderStorageBlockNamesIds[i] = StringToInt::lookupId(m_shaderStorageBlockNames[i]);
        m_shaderStorageBlocks[i].m_nameId =m_shaderStorageBlockNamesIds[i];
        qCDebug(Shaders) << "Initializing Shader Storage Block {" << m_shaderStorageBlockNames[i] << "}";
    }
}

/*!
   \internal
   Initializes this Shader's state relating to attributes, global block uniforms and
   and named uniform blocks by copying these details from \a other.
*/
void Shader::initializeFromReference(const Shader &other)
{
    Q_ASSERT(m_dna == other.m_dna);
    m_uniformsNamesIds = other.m_uniformsNamesIds;
    m_standardUniformNamesIds = other.m_standardUniformNamesIds;
    m_uniformsNames = other.m_uniformsNames;
    m_uniforms = other.m_uniforms;
    m_attributesNames = other.m_attributesNames;
    m_attributeNamesIds = other.m_attributeNamesIds;
    m_attributes = other.m_attributes;
    m_uniformBlockNamesIds = other.m_uniformBlockNamesIds;
    m_uniformBlockNames = other.m_uniformBlockNames;
    m_uniformBlocks = other.m_uniformBlocks;
    m_uniformBlockIndexToShaderUniforms = other.m_uniformBlockIndexToShaderUniforms;
    m_fragOutputs = other.m_fragOutputs;
    m_shaderStorageBlockNamesIds = other.m_shaderStorageBlockNamesIds;
    m_shaderStorageBlockNames = other.m_shaderStorageBlockNames;
    m_shaderStorageBlocks = other.m_shaderStorageBlocks;
    m_isLoaded = other.m_isLoaded;
    m_status = other.m_status;
    m_log = other.m_log;
    m_requiresFrontendSync = true;
}

void Shader::setLog(const QString &log)
{
    m_log = log;
    m_requiresFrontendSync = true;
}

void Shader::setStatus(QShaderProgram::Status status)
{
    m_status = status;
    m_requiresFrontendSync = true;
}

} // namespace Render
} // namespace Qt3DRender

QT_END_NAMESPACE
