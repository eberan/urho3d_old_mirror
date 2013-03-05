//
// Copyright (c) 2008-2013 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "BorderImage.h"
#include "Image.h"

#include <SDL_mouse.h>

namespace Urho3D
{

/// %Cursor shapes recognized by the UI subsystem.
enum CursorShape
{
    CS_NORMAL = 0,
    CS_RESIZEVERTICAL,
    CS_RESIZEDIAGONAL_TOPRIGHT,
    CS_RESIZEHORIZONTAL,
    CS_RESIZEDIAGONAL_TOPLEFT,
    CS_ACCEPTDROP,
    CS_REJECTDROP,
    CS_BUSY,
    CS_MAX_SHAPES
};

/// %Cursor image and hotspot information.
struct CursorShapeInfo
{
    /// Texture.
    SharedPtr<Texture> texture_;
    /// Image rectangle.
    IntRect imageRect_;
    /// Hotspot coordinates.
    IntVector2 hotSpot_;
    /// OS cursor.
    SDL_Cursor* osCursor_;
};

/// Mouse cursor %UI element.
class Cursor : public BorderImage
{
    OBJECT(Cursor);
    
public:
    /// Construct.
    Cursor(Context* context);
    /// Destruct.
    virtual ~Cursor();
    /// Register object factory.
    static void RegisterObject(Context* context);
    
    /// Return UI rendering batches.
    virtual void GetBatches(PODVector<UIBatch>& batches, PODVector<UIQuad>& quads, const IntRect& currentScissor);
    
    /// Define a shape.
    void DefineShape(CursorShape shape, Image* image, const IntRect& imageRect, const IntVector2& hotSpot, bool osMouseVisible = false);
    /// Set current shape.
    void SetShape(CursorShape shape);
    
    /// Get current shape.
    CursorShape GetShape() const { return shape_; }
    
    /// Set shapes attribute.
    void SetShapesAttr(VariantVector value);
    /// Return shapes attribute.
    VariantVector GetShapesAttr() const;
    
protected:
    /// Current shape index.
    CursorShape shape_;
    /// Shape definitions.
    CursorShapeInfo shapeInfos_[CS_MAX_SHAPES];
};

}
