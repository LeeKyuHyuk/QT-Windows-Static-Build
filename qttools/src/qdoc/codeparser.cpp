/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

/*
  codeparser.cpp
*/

#include "codeparser.h"
#include "node.h"
#include "tree.h"
#include "config.h"
#include "generator.h"
#include "qdocdatabase.h"
#include <qdebug.h>

QT_BEGIN_NAMESPACE

QList<CodeParser *> CodeParser::parsers;
bool CodeParser::showInternal_ = false;
bool CodeParser::singleExec_ = false;

/*!
  The constructor adds this code parser to the static
  list of code parsers.
 */
CodeParser::CodeParser()
{
    qdb_ = QDocDatabase::qdocDB();
    parsers.prepend(this);
}

/*!
  The destructor removes this code parser from the static
  list of code parsers.
 */
CodeParser::~CodeParser()
{
    parsers.removeAll(this);
}

/*!
  Initialize the code parser base class.
 */
void CodeParser::initializeParser(const Config& config)
{
    showInternal_ = config.getBool(CONFIG_SHOWINTERNAL);
    singleExec_ = config.getBool(CONFIG_SINGLEEXEC);
}

/*!
  Terminating a code parser is trivial.
 */
void CodeParser::terminateParser()
{
    // nothing.
}

QStringList CodeParser::headerFileNameFilter()
{
    return sourceFileNameFilter();
}

void CodeParser::parseHeaderFile(const Location& location, const QString& filePath)
{
    parseSourceFile(location, filePath);
}

/*!
  All the code parsers in the static list are initialized here,
  after the qdoc configuration variables have been set.
 */
void CodeParser::initialize(const Config& config)
{
    QList<CodeParser *>::ConstIterator p = parsers.constBegin();
    while (p != parsers.constEnd()) {
        (*p)->initializeParser(config);
        ++p;
    }
}

/*!
  All the code parsers in the static list are terminated here.
 */
void CodeParser::terminate()
{
    QList<CodeParser *>::ConstIterator p = parsers.constBegin();
    while (p != parsers.constEnd()) {
        (*p)->terminateParser();
        ++p;
    }
}

CodeParser *CodeParser::parserForLanguage(const QString& language)
{
    QList<CodeParser *>::ConstIterator p = parsers.constBegin();
    while (p != parsers.constEnd()) {
        if ((*p)->language() == language)
            return *p;
        ++p;
    }
    return nullptr;
}

CodeParser *CodeParser::parserForHeaderFile(const QString &filePath)
{
    QString fileName = QFileInfo(filePath).fileName();

    QList<CodeParser *>::ConstIterator p = parsers.constBegin();
    while (p != parsers.constEnd()) {

        QStringList headerPatterns = (*p)->headerFileNameFilter();
        foreach (const QString &pattern, headerPatterns) {
            QRegExp re(pattern, Qt::CaseInsensitive, QRegExp::Wildcard);
            if (re.exactMatch(fileName))
                return *p;
        }
        ++p;
    }
    return nullptr;
}

CodeParser *CodeParser::parserForSourceFile(const QString &filePath)
{
    QString fileName = QFileInfo(filePath).fileName();

    QList<CodeParser *>::ConstIterator p = parsers.constBegin();
    while (p != parsers.constEnd()) {

        QStringList sourcePatterns = (*p)->sourceFileNameFilter();
        foreach (const QString &pattern, sourcePatterns) {
            QRegExp re(pattern, Qt::CaseInsensitive, QRegExp::Wildcard);
            if (re.exactMatch(fileName))
                return *p;
        }
        ++p;
    }
    return nullptr;
}

static QSet<QString> commonMetaCommands_;
/*!
  Returns the set of strings representing the common metacommands.
 */
const QSet<QString>& CodeParser::commonMetaCommands()
{
    if (commonMetaCommands_.isEmpty()) {
        commonMetaCommands_ << COMMAND_ABSTRACT
                            << COMMAND_DEPRECATED
                            << COMMAND_INGROUP
                            << COMMAND_INJSMODULE
                            << COMMAND_INMODULE
                            << COMMAND_INPUBLICGROUP
                            << COMMAND_INQMLMODULE
                            << COMMAND_INTERNAL
                            << COMMAND_MAINCLASS
                            << COMMAND_NOAUTOLIST
                            << COMMAND_NONREENTRANT
                            << COMMAND_OBSOLETE
                            << COMMAND_PAGEKEYWORDS
                            << COMMAND_PRELIMINARY
                            << COMMAND_QMLABSTRACT
                            << COMMAND_QMLDEFAULT
                            << COMMAND_QMLINHERITS
                            << COMMAND_QMLREADONLY
                            << COMMAND_QTVARIABLE
                            << COMMAND_REENTRANT
                            << COMMAND_SINCE
                            << COMMAND_STARTPAGE
                            << COMMAND_SUBTITLE
                            << COMMAND_THREADSAFE
                            << COMMAND_TITLE
                            << COMMAND_WRAPPER;
   }
    return commonMetaCommands_;
}

/*!
  \internal
 */
void CodeParser::extractPageLinkAndDesc(const QString& arg,
                                        QString* link,
                                        QString* desc)
{
    QRegExp bracedRegExp(QLatin1String("\\{([^{}]*)\\}(?:\\{([^{}]*)\\})?"));

    if (bracedRegExp.exactMatch(arg)) {
        *link = bracedRegExp.cap(1);
        *desc = bracedRegExp.cap(2);
        if (desc->isEmpty())
            *desc = *link;
    }
    else {
        int spaceAt = arg.indexOf(QLatin1Char(' '));
        if (arg.contains(QLatin1String(".html")) && spaceAt != -1) {
            *link = arg.leftRef(spaceAt).trimmed().toString();
            *desc = arg.midRef(spaceAt).trimmed().toString();
        }
        else {
            *link = arg;
            *desc = arg;
        }
    }
}

/*!
  \internal
 */
void CodeParser::setLink(Node* node, Node::LinkType linkType, const QString& arg)
{
    QString link;
    QString desc;
    extractPageLinkAndDesc(arg, &link, &desc);
    node->setLink(linkType, link, desc);
}

/*!
  \brief Test for whether a doc comment warrants warnings.

  Returns true if qdoc should report that it has found something
  wrong with the qdoc comment in \a doc. Sometimes, qdoc should
  not report the warning, for example, when the comment contains
  the \c internal command, which normally means qdoc will not use
  the comment in the documentation anyway, so there is no point
  in reporting warnings about it.
 */
bool CodeParser::isWorthWarningAbout(const Doc &doc)
{
    return (showInternal_ || !doc.metaCommandsUsed().contains(QStringLiteral("internal")));
}

/*!
  Returns \c true if the file being parsed is a .h file.
 */
bool CodeParser::isParsingH() const
{
    return currentFile_.endsWith(".h");
}

/*!
  Returns \c true if the file being parsed is a .cpp file.
 */
bool CodeParser::isParsingCpp() const
{
    return currentFile_.endsWith(".cpp");
}

/*!
  Returns \c true if the file being parsed is a .qdoc file.
 */
bool CodeParser::isParsingQdoc() const
{
    return currentFile_.endsWith(".qdoc");
}

/*!
  For each node that will produce a documentation page, this function
  ensures that the node belongs to a module. Normally, the qdoc comment
  for an entity that will produce a documentation page will contain an
  \inmodule command to tell qdoc which module the entity belongs to.

  But now we normally run qdoc on each module in two passes. The first
  produces an index file; the second pass generates the docs after
  reading all the index files it needs.

  This means that all the pages generated during each pass 2 run of
  qdoc almost certainly belong to a single module, and the name of
  that module is, as a rule, used as the project name in the qdocconf
  file used when running qdoc on the module.

  So this function first asks if the node \a n has a non-empty module
  name. If it it does not have a non-empty module name, it sets the
  module name to be the project name.

  In some cases it prints a qdoc warning that it has done this. Namely,
  for C++ classes and namespaces.
 */
void CodeParser::checkModuleInclusion(Node* n)
{
    if (n->physicalModuleName().isEmpty()) {
        n->setPhysicalModuleName(Generator::defaultModuleName());
        QString word;
        switch (n->nodeType()) {
        case Node::Class:
            word = QLatin1String("Class");
            break;
        case Node::Struct:
            word = QLatin1String("Struct");
            break;
        case Node::Union:
            word = QLatin1String("Union");
            break;
        case Node::Namespace:
            word = QLatin1String("Namespace");
            break;
        default:
            return;
        }
        if (!n->isPrivate() && !n->name().isEmpty() && !n->doc().isEmpty()) {
            n->doc().location().warning(tr("%1 %2 has no \\inmodule command; "
                                           "using project name by default: %3")
                                        .arg(word).arg(n->name()).arg(Generator::defaultModuleName()));
        }
    }
}

QT_END_NAMESPACE
