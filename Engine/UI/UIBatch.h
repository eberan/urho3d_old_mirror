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

#include "Color.h"
#include "GraphicsDefs.h"
#include "Rect.h"

namespace Urho3D
{

class PixelShader;
class Graphics;
class Matrix3x4;
class ShaderVariation;
class Texture;
class UIElement;

static const unsigned UI_VERTEX_SIZE = 6;

/// %UI rendering draw call.
class URHO3D_API UIBatch
{
public:
    /// Construct with defaults.
    UIBatch();
    /// Construct.
    UIBatch(UIElement* element, BlendMode blendMode, const IntRect& scissor, Texture* texture, PODVector<float>* vertexData);
    
    /// Add a quad.
    void AddQuad(int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth = 0, int texHeight = 0, Color* color = 0);
    /// Add a quad using a transform matrix.
    void AddQuad(const Matrix3x4& transform, int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth = 0, int texHeight = 0, Color* color = 0);
    /// Add a quad with tiled texture.
    void AddQuad(int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth, int texHeight, bool tiled);
    /// Add a quad with custom color.
    void AddQuad(int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth, int texHeight, const Color& color);
    /// Merge with another batch.
    bool Merge(const UIBatch& batch);
    /// Return an interpolated color for the UI element.
    unsigned GetInterpolatedColor(int x, int y);
    
    /// Add or merge a batch.
    static void AddOrMerge(const UIBatch& batch, PODVector<UIBatch>& batches);
    
    /// Element this batch represents.
    UIElement* element_;
    /// Blending mode.
    BlendMode blendMode_;
    /// Scissor rectangle.
    IntRect scissor_;
    /// Texture.
    Texture* texture_;
    /// Inverse texture size.
    Vector2 invTextureSize_;
    /// Element color if not using a gradient.
    unsigned fixedColor_;
    /// Vertex data.
    PODVector<float>* vertexData_;
    /// Vertex data start index.
    unsigned vertexStart_;
    /// Vertex data end index.
    unsigned vertexEnd_;
};

}
