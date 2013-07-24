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

#include "Precompiled.h"
#include "Context.h"
#include "Cursor.h"
#include "Input.h"
#include "InputEvents.h"
#include "Log.h"
#include "Ptr.h"
#include "ResourceCache.h"
#include "StringUtils.h"
#include "Texture2D.h"
#include "UI.h"

#include "DebugNew.h"

namespace Urho3D
{

static const char* shapeNames[] =
{
    "Normal",
    "ResizeVertical",
    "ResizeDiagonalTopRight",
    "ResizeHorizontal",
    "ResizeDiagonalTopLeft",
    "AcceptDrop",
    "RejectDrop",
    "Busy",
    0
};

/// OS cursor shape lookup table matching cursor shape enumeration
static const int osCursorLookup[CS_MAX_SHAPES] =
{
    SDL_SYSTEM_CURSOR_ARROW,    // CS_NORMAL
    SDL_SYSTEM_CURSOR_SIZENS,   // CS_RESIZEVERTICAL
    SDL_SYSTEM_CURSOR_SIZENESW, // CS_RESIZEDIAGONAL_TOPRIGHT
    SDL_SYSTEM_CURSOR_SIZEWE,   // CS_RESIZEHORIZONTAL
    SDL_SYSTEM_CURSOR_SIZENWSE, // CS_RESIZEDIAGONAL_TOPLEFT
    SDL_SYSTEM_CURSOR_HAND,     // CS_ACCEPTDROP
    SDL_SYSTEM_CURSOR_NO,       // CS_REJECTDROP
    SDL_SYSTEM_CURSOR_WAIT      // CS_BUSY
};

extern const char* UI_CATEGORY;

Cursor::Cursor(Context* context) :
    BorderImage(context),
    shape_(CS_NORMAL),
    useSystemShapes_(false)
{
    // Show on top of all other UI elements
    priority_ = M_MAX_INT;
    
    // Subscribe to OS mouse cursor visibility changes to be able to reapply the cursor shape
    SubscribeToEvent(E_MOUSEVISIBLECHANGED, HANDLER(Cursor, HandleMouseVisibleChanged));
}

Cursor::~Cursor()
{
    for (unsigned i = 0; i < CS_MAX_SHAPES; ++i)
    {
        CursorShapeInfo& info = shapeInfos_[i];
        if (info.osCursor_)
        {
            SDL_FreeCursor(info.osCursor_);
            info.osCursor_ = 0;
        }
    }
}

void Cursor::RegisterObject(Context* context)
{
    context->RegisterFactory<Cursor>(UI_CATEGORY);

    COPY_BASE_ATTRIBUTES(Cursor, BorderImage);
    UPDATE_ATTRIBUTE_DEFAULT_VALUE(Cursor, "Priority", M_MAX_INT);
    ACCESSOR_ATTRIBUTE(Cursor, VAR_BOOL, "Use System Shapes", GetUseSystemShapes, SetUseSystemShapes, bool, false, AM_FILE);
    ACCESSOR_ATTRIBUTE(Cursor, VAR_VARIANTVECTOR, "Shapes", GetShapesAttr, SetShapesAttr, VariantVector, Variant::emptyVariantVector, AM_FILE);
}

void Cursor::SetUseSystemShapes(bool enable)
{
    if (enable != useSystemShapes_)
    {
        useSystemShapes_ = enable;
        
        // Reapply current shape
        ApplyShape();
    }
}

void Cursor::DefineShape(CursorShape shape, Image* image, const IntRect& imageRect, const IntVector2& hotSpot)
{
    if (shape < CS_NORMAL || shape >= CS_MAX_SHAPES)
    {
        LOGERROR("Shape index out of bounds, can not define cursor shape");
        return;
    }
    
    if (!image)
        return;
    
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    CursorShapeInfo& info = shapeInfos_[shape];
    
    // Prefer to get the texture with same name from cache to prevent creating several copies of the texture
    if (cache->Exists(image->GetName()))
        info.texture_ = cache->GetResource<Texture2D>(image->GetName());
    else
    {
        Texture2D* texture = new Texture2D(context_);
        texture->Load(SharedPtr<Image>(image));
        info.texture_ = texture;
    }
    
    info.image_ = image;
    info.imageRect_ = imageRect;
    info.hotSpot_ = hotSpot;
    
    // Remove existing SDL cursor
    if (info.osCursor_)
    {
        SDL_FreeCursor(info.osCursor_);
        info.osCursor_ = 0;
    }

    // Reset current shape if it was edited
    if (shape == shape_)
        ApplyShape();
}

void Cursor::SetShape(CursorShape shape)
{
    if (shape_ == shape || shape < CS_NORMAL || shape >= CS_MAX_SHAPES)
        return;

    shape_ = shape;
    ApplyShape();
}

void Cursor::SetShapesAttr(VariantVector value)
{
    unsigned index = 0;
    if (!value.Size())
        return;

    unsigned numShapes = value[index++].GetUInt();
    while (numShapes-- && (index + 4) <= value.Size())
    {
        CursorShape shape = (CursorShape)GetStringListIndex(value[index++].GetString().CString(), shapeNames, CS_MAX_SHAPES);
        if (shape != CS_MAX_SHAPES)
        {
            ResourceRef ref = value[index++].GetResourceRef();
            IntRect imageRect = value[index++].GetIntRect();
            IntVector2 hotSpot = value[index++].GetIntVector2();
            DefineShape(shape, GetSubsystem<ResourceCache>()->GetResource<Image>(ref.id_), imageRect, hotSpot);
        }
        else
            index += 3;
    }
}

VariantVector Cursor::GetShapesAttr() const
{
    VariantVector ret;

    unsigned numShapes = 0;
    for (unsigned i = 0; i < CS_MAX_SHAPES; ++i)
    {
        if (shapeInfos_[i].imageRect_ != IntRect::ZERO)
            ++numShapes;
    }

    ret.Push(numShapes);
    for (unsigned i = 0; i < CS_MAX_SHAPES; ++i)
    {
        if (shapeInfos_[i].imageRect_ != IntRect::ZERO)
        {
            ret.Push(String(shapeNames[i]));
            ret.Push(GetResourceRef(shapeInfos_[i].texture_, Texture2D::GetTypeStatic()));
            ret.Push(shapeInfos_[i].imageRect_);
            ret.Push(shapeInfos_[i].hotSpot_);
        }
    }

    return ret;
}

void Cursor::GetBatches(PODVector<UIBatch>& batches, PODVector<float>& vertexData, const IntRect& currentScissor)
{
    unsigned initialSize = vertexData.Size();
    const IntVector2& offset = shapeInfos_[shape_].hotSpot_;
    Vector2 floatOffset(-(float)offset.x_, -(float)offset.y_);

    BorderImage::GetBatches(batches, vertexData, currentScissor);
    for (unsigned i = initialSize; i < vertexData.Size(); i += 6)
    {
        vertexData[i] += floatOffset.x_;
        vertexData[i + 1] += floatOffset.y_;
    }
}

void Cursor::ApplyShape()
{
    CursorShapeInfo& info = shapeInfos_[shape_];
    texture_ = info.texture_;
    imageRect_ = info.imageRect_;
    SetSize(info.imageRect_.Size());

    // If the OS cursor is being shown, define/set SDL cursor shape if necessary
    // Only do this when we are the active UI cursor
    if (GetSubsystem<Input>()->IsMouseVisible() && GetSubsystem<UI>()->GetCursor() == this)
    {
        // Remove existing SDL cursor if is not a system shape while we should be using those, or vice versa
        if (info.osCursor_ && info.systemDefined_ != useSystemShapes_)
        {
            SDL_FreeCursor(info.osCursor_);
            info.osCursor_ = 0;
        }
        
        // Create SDL cursor now if necessary
        if (!info.osCursor_)
        {
            // Create from image
            if (!useSystemShapes_ && info.image_)
            {
                unsigned comp = info.image_->GetComponents();
                int imageWidth = info.image_->GetWidth();
                int width = imageRect_.Width();
                int height = imageRect_.Height();

                // Assume little-endian for all the supported platforms
                unsigned rMask = 0x000000ff;
                unsigned gMask = 0x0000ff00;
                unsigned bMask = 0x00ff0000;
                unsigned aMask = 0xff000000;

                SDL_Surface* surface = (comp >= 3 ? SDL_CreateRGBSurface(0, width, height, comp * 8, rMask, gMask, bMask, aMask) : 0);
                if (surface)
                {
                    unsigned char* destination = reinterpret_cast<unsigned char*>(surface->pixels);
                    unsigned char* source = info.image_->GetData() + comp * (imageWidth * imageRect_.top_ + imageRect_.left_);
                    for (int i = 0; i < height; ++i)
                    {
                        memcpy(destination, source, comp * width);
                        destination += comp * width;
                        source += comp * imageWidth;
                    }
                    info.osCursor_ = SDL_CreateColorCursor(surface, info.hotSpot_.x_, info.hotSpot_.y_);
                    info.systemDefined_ = false;
                    if (!info.osCursor_)
                        LOGERROR("Could not create cursor from image " + info.image_->GetName());
                    SDL_FreeSurface(surface);
                }
            }
            
            // Create a system default shape
            if (useSystemShapes_)
            {
                info.osCursor_ = SDL_CreateSystemCursor((SDL_SystemCursor)osCursorLookup[shape_]);
                info.systemDefined_ = true;
                if (!info.osCursor_)
                    LOGERROR("Could not create system cursor");
            }
        }
        
        if (info.osCursor_)
            SDL_SetCursor(info.osCursor_);
    }
}

void Cursor::HandleMouseVisibleChanged(StringHash eventType, VariantMap& eventData)
{
    ApplyShape();
}

}
