#ifndef MYVTKITEM_H
#define MYVTKITEM_H

#include "QQuickVtkItem.h"

class MyVtkItem : public QQuickVtkItem
{
public:
    vtkUserData initializeVTK(vtkRenderWindow *renderWindow) override;
};

#endif // MYVTKITEM_H
