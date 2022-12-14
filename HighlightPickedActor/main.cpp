#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <QVTKRenderWindowAdapter.h>

#include "MyVtkItem.h"

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::OpenGLRhi);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
#endif
    QSurfaceFormat::setDefaultFormat(QVTKRenderWindowAdapter::defaultFormat());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QGuiApplication app(argc, argv);

    qmlRegisterType<MyVtkItem>("Vtk", 1, 0, "MyVtkItem");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
