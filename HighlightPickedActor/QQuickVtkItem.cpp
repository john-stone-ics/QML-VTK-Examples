#include "QQuickVtkItem.h"

#include <QtQuick/QSGTextureProvider>
#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/QSGRenderNode>
#include <QtQuick/QQuickWindow>

#include <QtGui/QOpenGLContext>
#include <QtGui/QScreen>

#include <QtCore/QEvent>
#include <QtCore/QMap>
#include <QtCore/QQueue>
#include <QtCore/QThread>
#include <QtCore/QRunnable>
#include <QtCore/QSharedPointer>

#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkOpenGLFramebufferObject.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRendererCollection.h>
#include <vtkTextureObject.h>
#include <vtkOpenGLState.h>
#include <vtkRenderer.h>

#include <QVTKInteractorAdapter.h>
#include <QVTKInteractor.h>

#include <limits>
#include <queue>

// no touch events for now
#define NO_TOUCH

/* -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- */

class QSGVtkObjectNode;

class QQuickVtkItemPrivate
{
public:
    QQuickVtkItemPrivate(QQuickVtkItem* ptr) : q_ptr(ptr)
    {}

    QQueue<std::function<void(vtkRenderWindow*, QQuickVtkItem::vtkUserData)>> asyncDispatch;

    QVTKInteractorAdapter qt2vtkInteractorAdapter;

    bool scheduleRender = false;

    mutable QSGVtkObjectNode* node = nullptr;

private:
    Q_DISABLE_COPY(QQuickVtkItemPrivate)
    Q_DECLARE_PUBLIC(QQuickVtkItem)
    QQuickVtkItem * const q_ptr;
};

/* -+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- */

QQuickVtkItem::QQuickVtkItem(QQuickItem* parent) : QQuickItem(parent), d_ptr(new QQuickVtkItemPrivate(this))
{
    setAcceptHoverEvents(true);
#ifndef NO_TOUCH
    setAcceptTouchEvents(true);
#endif
    setAcceptedMouseButtons(Qt::AllButtons);

    setFlag(QQuickItem::ItemIsFocusScope);
    setFlag(QQuickItem::ItemHasContents);
}

QQuickVtkItem::~QQuickVtkItem() = default;

void QQuickVtkItem::dispatch_async(std::function<void(vtkRenderWindow*, vtkUserData)> f)
{
    Q_D(QQuickVtkItem);

    d->asyncDispatch.append(f);

    update();
}

#if 0
void QQuickVtkItem::qtRect2vtkViewport(QRectF const& qtRect, double vtkViewport[4], QRectF* glRect)
{
    // Calculate our scaled size
    auto sz = size() * window()->devicePixelRatio();

    // Use a temporary if not supplied by caller
    QRectF tmp; if (!glRect) 
        glRect = &tmp;

    // Convert origin to be bottom-left
    *glRect = QRectF{{qtRect.x(), sz.height() - qtRect.bottom() - 1.0}, qtRect.size()};

    // Convert to a vtkViewport
    if (vtkViewport) {
        vtkViewport[0] = glRect->topLeft    ().x() / (sz.width () - 1.0);
        vtkViewport[1] = glRect->topLeft    ().y() / (sz.height() - 1.0);
        vtkViewport[2] = glRect->bottomRight().x() / (sz.width () - 1.0);
        vtkViewport[3] = glRect->bottomRight().y() / (sz.height() - 1.0);
    };
}
#endif

class QSGVtkObjectNode : public QSGTextureProvider, public QSGSimpleTextureNode
{
    Q_OBJECT
public:
    QSGVtkObjectNode() 
    {
        qsgnode_set_description(this, QStringLiteral("vtknode"));
    }

    ~QSGVtkObjectNode()
    {
        delete QSGVtkObjectNode::texture();

        // Cleanup the VTK window resources
        vtkWindow->GetRenderers()->InitTraversal(); while (auto renderer = vtkWindow->GetRenderers()->GetNextItem())
            renderer->ReleaseGraphicsResources(vtkWindow);
        vtkWindow->ReleaseGraphicsResources(vtkWindow);
        vtkWindow = nullptr;

        // Cleanup the User Data
        vtkUserData = nullptr;
    }

    QSGTexture* texture() const override
    {
        return QSGSimpleTextureNode::texture();
    }

    void initialize(QQuickVtkItem* item)
    {
        // Create and initialize the vtkWindow
        vtkWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
        vtkWindow->SetMultiSamples(0);
        vtkWindow->SetReadyForRendering(false);
        vtkWindow->SetFrameBlitModeToNoBlit();
        vtkNew<QVTKInteractor> iren;
        iren->SetRenderWindow(vtkWindow);
        vtkNew<vtkInteractorStyleTrackballCamera> style;
        iren->SetInteractorStyle(style);
        vtkUserData = item->initializeVTK(vtkWindow);
        if (auto ia = vtkWindow->GetInteractor(); ia && !QVTKInteractor::SafeDownCast(ia)) {
            qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__ << ", YIKES!! Only QVTKInteractor is supported";
            return;
        }
        vtkWindow->SetReadyForRendering(false);
        vtkWindow->GetInteractor()->Initialize();
        vtkWindow->SetMapped(true);
        vtkWindow->SetIsCurrent(true);
        vtkWindow->SetForceMaximumHardwareLineWidth(1);
        vtkWindow->SetOwnContext(false);
        vtkWindow->OpenGLInitContext();
    }

    void scheduleRender()
    {
        m_renderPending = true;
        m_window->update();
    }

public Q_SLOTS:
    void render()
    {
        if (m_renderPending) {
            m_renderPending = false;

            // Render VTK into it's framebuffer
            auto ostate = vtkWindow->GetState();
            ostate->Reset();
            ostate->Push();
            ostate->vtkglDepthFunc(GL_LEQUAL);          // note: By default, Qt sets the depth function to GL_LESS but VTK expects GL_LEQUAL
            vtkWindow->SetReadyForRendering(true);
            vtkWindow->GetInteractor()->ProcessEvents();
            vtkWindow->GetInteractor()->Render();
            vtkWindow->SetReadyForRendering(false);
            ostate->Pop();

            markDirty(QSGNode::DirtyMaterial);
            Q_EMIT textureChanged();
        }
    }

    void handleScreenChange()
    {
        if (m_window->effectiveDevicePixelRatio() != m_devicePixelRatio) {

            m_item->update();
        }
    }

private:
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> vtkWindow;
    vtkSmartPointer<vtkObject> vtkUserData;
    bool m_renderPending = false;

protected:
    // variables set in QQuickVtkItem::updatePaintNode()
    QQuickWindow* m_window = nullptr;
    QQuickItem* m_item = nullptr;
    qreal m_devicePixelRatio = 0;
    QSizeF size;
    friend class QQuickVtkItem;
};

QSGNode* QQuickVtkItem::updatePaintNode(QSGNode* node, UpdatePaintNodeData*)
{
    auto* n = static_cast<QSGVtkObjectNode*>(node);
    
    // Don't create the node if our size is invalid
    if (!n && (width() <= 0 || height() <= 0))
        return nullptr;

    Q_D(QQuickVtkItem);

    // Create the QSGRenderNode 
    if (!n) {
        auto api = window()->rendererInterface()->graphicsApi();
        if (api != QSGRendererInterface::OpenGL && api != QSGRendererInterface::OpenGLRhi) {
            qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__ << ", YIKES!! Unsupported graphicsApi(): " << api;
            return nullptr;
        }
        if (!d->node) {
            d->node = new QSGVtkObjectNode;
        }
        n = d->node;
    }
        
    // Initialize the QSGRenderNode
    if (!n->m_item) {
        n->initialize(this);
        n->m_window = window();
        n->m_item = this;
        connect(window(), &QQuickWindow::beforeRendering, n, &QSGVtkObjectNode::render);
        connect(window(), &QQuickWindow::screenChanged, n, &QSGVtkObjectNode::handleScreenChange);
    }

    // Watch for size changes
    n->m_devicePixelRatio = window()->devicePixelRatio();
    auto sz = size() * n->m_devicePixelRatio;
    bool dirtySize = sz != n->size; 
    if (dirtySize) {
        n->vtkWindow->SetSize(sz.width(), sz.height());
        n->vtkWindow->GetInteractor()->SetSize(n->vtkWindow->GetSize());
        delete n->texture();
        n->size = sz;
    }

    // Dispatch commands to VTK
    if (d->asyncDispatch.size()) {
        n->scheduleRender();

        n->vtkWindow->SetReadyForRendering(true);
        while (d->asyncDispatch.size())
            d->asyncDispatch.dequeue()(n->vtkWindow, n->vtkUserData);
        n->vtkWindow->SetReadyForRendering(false);
    }
    
    // Whenever the size changes we need to get a new FBO from VTK so we need to render right now (with the gui-thread blocked) for this one frame.
    if (dirtySize) {
        n->scheduleRender();
        n->render();
        if (auto fb = n->vtkWindow->GetDisplayFramebuffer(); fb && fb->GetNumberOfColorAttachments() > 0) {
            GLuint texId = fb->GetColorAttachmentAsTextureObject(0)->GetHandle();
            auto texture = window()->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture, &texId, 0, sz.toSize(), QQuickWindow::TextureHasAlphaChannel);
            n->setTexture(texture);
        } else if (!fb)
            qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__ << ", YIKES!!, Render() didn't create a FrameBuffer!?";
        else
            qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__ << ", YIKES!!, Render() didn't create any ColorBufferAttachements to its FrameBuffer!?";

    }

    n->setTextureCoordinatesTransform(QSGSimpleTextureNode::MirrorVertically);
    n->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
    n->setRect(0, 0, width(), height());

    // ??? n->scheduleRender();

    if (d->scheduleRender) {
        n->scheduleRender();
        d->scheduleRender = false;
    }

    return n;
}

void QQuickVtkItem::scheduleRender()
{
    Q_D(QQuickVtkItem);

    d->scheduleRender = true;
    update();
}

bool QQuickVtkItem::isTextureProvider() const
{
    return true;
}

QSGTextureProvider* QQuickVtkItem::textureProvider() const 
{
    // When Item::layer::enabled == true, QQuickItem will be a texture provider. 
    // In this case we should prefer to return the layer rather than the VTK texture.
    if (QQuickItem::isTextureProvider())
        return QQuickItem::textureProvider();

    QQuickWindow* w = window();
    if (!w || !w->openglContext() || QThread::currentThread() != w->openglContext()->thread()) {
        qWarning("QQuickFramebufferObject::textureProvider: can only be queried on the rendering thread of an exposed window");
        return nullptr;
    }

    auto api = window()->rendererInterface()->graphicsApi();
    if (api != QSGRendererInterface::OpenGL && api != QSGRendererInterface::OpenGLRhi) {
        qWarning().nospace() << "QQuickVTKItem.cpp:" << __LINE__ << ", Unsupported graphicsApi(): " << api;
        return nullptr;
    }

    Q_D(const QQuickVtkItem);

    if (!d->node)
        d->node = new QSGVtkObjectNode;

    return d->node;
}

void QQuickVtkItem::releaseResources()
{
    // When release resources is called on the GUI thread, we only need to
    // forget about the node. Since it is the node we returned from updatePaintNode
    // it will be managed by the scene graph.
    Q_D(QQuickVtkItem);
    d->node = nullptr;
}

void QQuickVtkItem::invalidateSceneGraph()
{
    Q_D(QQuickVtkItem);
    d->node = nullptr; 
}


bool QQuickVtkItem::event(QEvent * ev)
{
    Q_D(QQuickVtkItem);

    if (!ev)
        return false;

    switch (ev->type())
    {
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
    {
        auto e = static_cast<QHoverEvent *>(ev);
        dispatch_async([d, 
            e = QHoverEvent(
                e->type(), 
                e->posF(),
                e->oldPosF(), 
                e->modifiers())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });
        break;
    }
    case QEvent::Enter:
    {
      auto e = static_cast<QEnterEvent*>(ev);
      dispatch_async([d,
          e = QEnterEvent(
              e->localPos(), 
              e->windowPos(), 
              e->screenPos())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::Leave:
    {
      auto e = static_cast<QEvent*>(ev);
      dispatch_async([d,
          e = QEvent(
              e->type())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::DragEnter:
    {
      auto e = static_cast<QDragEnterEvent*>(ev);
      dispatch_async([d,
          e = QDragEnterEvent(
              e->pos(), 
              e->possibleActions(), 
              e->mimeData(),
              e->mouseButtons(),
              e->keyboardModifiers())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::DragLeave:
    {
      dispatch_async([d,
          e = QDragLeaveEvent()]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::DragMove:
    {
      auto e = static_cast<QDragMoveEvent*>(ev);
      dispatch_async([d,
          e = QDragMoveEvent(
              e->pos(), 
              e->possibleActions(), 
              e->mimeData(),
              e->mouseButtons(),
              e->keyboardModifiers())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::Drop:
    {
      auto e = static_cast<QDropEvent*>(ev);
      dispatch_async([d,
          e = QDropEvent(
              e->pos(), 
              e->possibleActions(), 
              e->mimeData(),
              e->mouseButtons(),
              e->keyboardModifiers())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::ContextMenu:
    {
      auto e = static_cast<QContextMenuEvent*>(ev);
      dispatch_async([d,
          e = QContextMenuEvent(
              e->reason(), 
              e->pos(), 
              e->globalPos(),
              e->modifiers())]
          (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
              d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
          });
      break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    {
        auto e = static_cast<QKeyEvent *>(ev);
        dispatch_async([d, 
            e = QKeyEvent(
                e->type(), 
                e->key(), 
                e->modifiers(), 
                e->nativeScanCode(),
                e->nativeVirtualKey(), 
                e->nativeModifiers(), 
                e->text(), 
                e->isAutoRepeat(), 
                e->count())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });
        break;
    }
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    {
        auto e = static_cast<QFocusEvent *>(ev);
        dispatch_async([d, 
            e = QFocusEvent(
                e->type(), 
                e->reason())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });
        break;
    }
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    {
        auto e = static_cast<QMouseEvent *>(ev);
        dispatch_async([d, 
            e = QMouseEvent(
                e->type(), 
                e->localPos(),
                e->windowPos(),
                e->screenPos(),
                e->button(), 
                e->buttons(), 
                e->modifiers(),
                e->source())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });        
        break;
    }
#ifndef QT_NO_WHEELEVENT
    case QEvent::Wheel:
    {
        auto e = static_cast<QWheelEvent *>(ev);
        dispatch_async([d, 
            e = QWheelEvent(
                e->position(),
                e->globalPosition(), 
                e->pixelDelta(), 
                e->angleDelta(),
                e->buttons(), 
                e->modifiers(), 
                e->phase(), 
                e->inverted(), 
                e->source())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });
        break;
    }
#endif
#ifndef NO_TOUCH
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    {
        auto e = static_cast<QTouchEvent *>(ev);
        dispatch_async([d, 
            e = QTouchEvent(e->type(),
                e->device(),
                e->modifiers(),
                e->touchPointStates(),
                e->touchPoints())]
            (vtkRenderWindow* vtkWindow, vtkUserData) mutable {
                d->qt2vtkInteractorAdapter.ProcessEvent(&e, vtkWindow->GetInteractor());
            });
        break;
    }
#endif
    default:
        return QQuickItem::event(ev);
    }

    ev->accept();

    return true;
}

#include "QQuickVtkItem.moc"
#include "moc_QQuickVtkItem.cpp"
