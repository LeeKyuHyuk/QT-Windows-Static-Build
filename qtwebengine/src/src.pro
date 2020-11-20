load(functions)

include($$QTWEBENGINE_OUT_ROOT/src/buildtools/qtbuildtools-config.pri)
include($$QTWEBENGINE_OUT_ROOT/src/core/qtwebenginecore-config.pri)
include($$QTWEBENGINE_OUT_ROOT/src/webengine/qtwebengine-config.pri)
include($$QTWEBENGINE_OUT_ROOT/src/webenginewidgets/qtwebenginewidgets-config.pri)

QT_FOR_CONFIG += buildtools-private webenginecore webenginecore-private webengine-private \
    webenginewidgets-private

TEMPLATE = subdirs


qtConfig(build-qtwebengine-core):qtConfig(webengine-core-support) {
    core.depends = buildtools
    process.depends = core
    webengine.depends = core
    webenginewidgets.depends = core webengine
    webengine_plugin.subdir = webengine/plugin
    webengine_plugin.target = sub-webengine-plugin
    webengine_plugin.depends = webengine

    SUBDIRS += buildtools core process

    qtConfig(webengine-spellchecker):!qtConfig(webengine-native-spellchecker):!cross_compile {
        SUBDIRS += qwebengine_convert_dict
        qwebengine_convert_dict.subdir = tools/qwebengine_convert_dict
        qwebengine_convert_dict.depends = core
    }

    qtConfig(webengine-qml) {
        SUBDIRS += webengine
    }

    qtConfig(webengine-widgets) {
        SUBDIRS += plugins webenginewidgets
        plugins.depends = webenginewidgets
    }
}

!qtConfig(webengine-core-support): qtConfig(build-qtwebengine-core) {
    !qtwebengine_makeCheckError():!isEmpty(skipBuildReason):!build_pass {
        errorbuild.commands = @echo Modules will not be built. $${skipBuildReason}
        errorbuild.CONFIG = phony
        QMAKE_EXTRA_TARGETS += errorbuild
        first.depends += errorbuild
        QMAKE_EXTRA_TARGETS += first
    }
}
