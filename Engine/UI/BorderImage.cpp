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
#include "BorderImage.h"
#include "Context.h"
#include "ResourceCache.h"
#include "Texture2D.h"

#include "DebugNew.h"

namespace Urho3D
{

extern const char* blendModeNames[];
extern const char* UI_CATEGORY;

template<> BlendMode Variant::Get<BlendMode>() const
{
    return (BlendMode)GetInt();
}

BorderImage::BorderImage(Context* context) :
    UIElement(context),
    imageRect_(IntRect::ZERO),
    border_(IntRect::ZERO),
    hoverOffset_(IntVector2::ZERO),
    blendMode_(BLEND_REPLACE),
    tiled_(false)
{
}

BorderImage::~BorderImage()
{
}

void BorderImage::RegisterObject(Context* context)
{
    context->RegisterFactory<BorderImage>(UI_CATEGORY);

    COPY_BASE_ATTRIBUTES(BorderImage, UIElement);
    ACCESSOR_ATTRIBUTE(BorderImage, VAR_RESOURCEREF, "Texture", GetTextureAttr, SetTextureAttr, ResourceRef, ResourceRef(Texture2D::GetTypeStatic()), AM_FILE);
    REF_ACCESSOR_ATTRIBUTE(BorderImage, VAR_INTRECT, "Image Rect", GetImageRect, SetImageRect, IntRect, IntRect::ZERO, AM_FILE);
    REF_ACCESSOR_ATTRIBUTE(BorderImage, VAR_INTRECT, "Border", GetBorder, SetBorder, IntRect, IntRect::ZERO, AM_FILE);
    REF_ACCESSOR_ATTRIBUTE(BorderImage, VAR_INTVECTOR2, "Hover Image Offset", GetHoverOffset, SetHoverOffset, IntVector2, IntVector2::ZERO, AM_FILE);
    ACCESSOR_ATTRIBUTE(BorderImage, VAR_BOOL, "Tiled", IsTiled, SetTiled, bool, false, AM_FILE);
    ENUM_ACCESSOR_ATTRIBUTE(BorderImage, "Blend Mode", GetBlendMode, SetBlendMode, BlendMode, blendModeNames, 0, AM_FILE);
}

void BorderImage::GetBatches(PODVector<UIBatch>& batches, PODVector<float>& vertexData, const IntRect& currentScissor)
{
    GetBatches(batches, vertexData, currentScissor, hovering_ || selected_ ? hoverOffset_ : IntVector2::ZERO);
}

void BorderImage::SetTexture(Texture* texture)
{
    texture_ = texture;
    if (imageRect_ == IntRect::ZERO)
        SetFullImageRect();
}

void BorderImage::SetImageRect(const IntRect& rect)
{
    if (rect != IntRect::ZERO)
        imageRect_ = rect;
}

void BorderImage::SetFullImageRect()
{
    if (texture_)
        SetImageRect(IntRect(0, 0, texture_->GetWidth(), texture_->GetHeight()));
}

void BorderImage::SetBorder(const IntRect& rect)
{
    border_.left_ = Max(rect.left_, 0);
    border_.top_ = Max(rect.top_, 0);
    border_.right_ = Max(rect.right_, 0);
    border_.bottom_ = Max(rect.bottom_, 0);
}

void BorderImage::SetHoverOffset(const IntVector2& offset)
{
    hoverOffset_ = offset;
}

void BorderImage::SetHoverOffset(int x, int y)
{
    hoverOffset_ = IntVector2(x, y);
}

void BorderImage::SetBlendMode(BlendMode mode)
{
    blendMode_ = mode;
}

void BorderImage::SetTiled(bool enable)
{
    tiled_ = enable;
}

void BorderImage::GetBatches(PODVector<UIBatch>& batches, PODVector<float>& vertexData, const IntRect& currentScissor, const IntVector2& offset)
{
    bool allOpaque = true;
    if (GetDerivedOpacity() < 1.0f || color_[C_TOPLEFT].a_ < 1.0f || color_[C_TOPRIGHT].a_ < 1.0f ||
        color_[C_BOTTOMLEFT].a_ < 1.0f || color_[C_BOTTOMRIGHT].a_ < 1.0f)
        allOpaque = false;

    UIBatch batch(this, blendMode_ == BLEND_REPLACE && !allOpaque ? BLEND_ALPHA : blendMode_, currentScissor, texture_, &vertexData);

    // Calculate size of the inner rect, and texture dimensions of the inner rect
    int x = GetIndentWidth();
    IntVector2 size = GetSize();
    size.x_ -= x;
    IntVector2 innerSize(
        Max(size.x_ - border_.left_ - border_.right_, 0),
        Max(size.y_ - border_.top_ - border_.bottom_, 0));
    IntVector2 innerTextureSize(
        Max(imageRect_.right_ - imageRect_.left_ - border_.left_ - border_.right_, 0),
        Max(imageRect_.bottom_ - imageRect_.top_ - border_.top_ - border_.bottom_, 0));

    IntVector2 topLeft(imageRect_.left_, imageRect_.top_);
    topLeft += offset;

    // Top
    if (border_.top_)
    {
        if (border_.left_)
            batch.AddQuad(x, 0, border_.left_, border_.top_, topLeft.x_, topLeft.y_);
        if (innerSize.x_)
            batch.AddQuad(x + border_.left_, 0, innerSize.x_, border_.top_, topLeft.x_ + border_.left_, topLeft.y_,
                innerTextureSize.x_, border_.top_, tiled_);
        if (border_.right_)
            batch.AddQuad(x + border_.left_ + innerSize.x_, 0, border_.right_, border_.top_, topLeft.x_ + border_.left_ +
                innerTextureSize.x_, topLeft.y_);
    }
    // Middle
    if (innerSize.y_)
    {
        if (border_.left_)
            batch.AddQuad(x, border_.top_, border_.left_, innerSize.y_, topLeft.x_, topLeft.y_ + border_.top_, border_.left_,
                innerTextureSize.y_, tiled_);
        if (innerSize.x_)
            batch.AddQuad(x + border_.left_, border_.top_, innerSize.x_, innerSize.y_, topLeft.x_ + border_.left_, topLeft.y_ +
                border_.top_, innerTextureSize.x_, innerTextureSize.y_, tiled_);
        if (border_.right_)
            batch.AddQuad(x + border_.left_ + innerSize.x_, border_.top_, border_.right_, innerSize.y_, topLeft.x_ +
                border_.left_ + innerTextureSize.x_, topLeft.y_ + border_.top_, border_.right_, innerTextureSize.y_, tiled_);
    }
    // Bottom
    if (border_.bottom_)
    {
        if (border_.left_)
            batch.AddQuad(x, border_.top_ + innerSize.y_, border_.left_, border_.bottom_, topLeft.x_, topLeft.y_ + border_.top_ +
                innerTextureSize.y_);
        if (innerSize.x_)
            batch.AddQuad(x + border_.left_, border_.top_ + innerSize.y_, innerSize.x_, border_.bottom_, topLeft.x_ +
                border_.left_, topLeft.y_ + border_.top_ + innerTextureSize.y_, innerTextureSize.x_, border_.bottom_, tiled_);
        if (border_.right_)
            batch.AddQuad(x + border_.left_ + innerSize.x_, border_.top_ + innerSize.y_, border_.right_, border_.bottom_,
                topLeft.x_ + border_.left_ + innerTextureSize.x_, topLeft.y_ + border_.top_ + innerTextureSize.y_);
    }

    UIBatch::AddOrMerge(batch, batches);

    // Reset hovering for next frame
    hovering_ = false;
}

void BorderImage::SetTextureAttr(ResourceRef value)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    SetTexture(cache->GetResource<Texture2D>(value.id_));
}

ResourceRef BorderImage::GetTextureAttr() const
{
    return GetResourceRef(texture_, Texture2D::GetTypeStatic());
}

}
