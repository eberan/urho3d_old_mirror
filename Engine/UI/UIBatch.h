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
#include "Matrix3x4.h"
#include "Rect.h"

namespace Urho3D
{

class PixelShader;
class Graphics;
class ShaderVariation;
class Texture;
class UIElement;

/// %UI rendering quad.
struct UIQuad
{
    /// Construct with defaults.
    UIQuad();
    /// Construct.
    UIQuad(const UIElement& element, int x, int y, int width, int height, int texOffsetX, int texOffsetY,
        int texWidth = 0, int texHeight = 0, Color* color = 0);

    /// Return an interpolated color for an UI element.
    static unsigned GetInterpolatedColor(const UIElement& element, int x, int y);

    /// Left coordinate.
    int left_;
    /// Top coordinate.
    int top_;
    /// Right coordinate.
    int right_;
    /// Bottom coordinate.
    int bottom_;
    /// Left texture coordinate.
    short leftUV_;
    /// Top texture coordinate.
    short topUV_;
    /// Right texture coordinate.
    short rightUV_;
    /// Bottom texture coordinate.
    short bottomUV_;
    /// Top left color.
    unsigned topLeftColor_;
    /// Top right color.
    unsigned topRightColor_;
    /// Bottom left color.
    unsigned bottomLeftColor_;
    /// Bottom right color.
    unsigned bottomRightColor_;
    /// Defined flag.
    bool defined_;
};

/// %UI rendering draw call.
class UIBatch
{
public:
    /// Construct with defaults.
    UIBatch();
    /// Construct
    UIBatch(const Matrix3x4& transform, BlendMode blendMode, const IntRect& scissor, Texture* texture, PODVector<UIQuad>* quads);
    
    /// Begin adding quads.
    void Begin(PODVector<UIQuad>* quads);
    /// Add a collection of quads. Each quad is added without first checking whether it is properly defined.
    void AddQuad(const PODVector<UIQuad>& quads);
    /// Add a quad.
    void AddQuad(UIQuad quad);
    /// Add a quad.
    void AddQuad(const UIElement& element, int x, int y, int width, int height, int texOffsetX, int texOffsetY);
    /// Add a quad with scaled texture.
    void AddQuad(const UIElement& element, int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth, int texHeight);
    /// Add a quad with tiled texture.
    void AddQuad(const UIElement& element, int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth, int texHeight, bool tiled);
    /// Add a quad with custom color.
    void AddQuad(const UIElement& element, int x, int y, int width, int height, int texOffsetX, int texOffsetY, int texWidth, int texHeight, const Color& color);
    /// Merge with another batch.
    bool Merge(const UIBatch& batch);
    /// Update the vertex data.
    void UpdateGeometry(Graphics* graphics, void* lockedData);
    
    /// Add or merge a batch.
    static void AddOrMerge(const UIBatch& batch, PODVector<UIBatch>& batches);
    
    /// Transform matrix.
    Matrix3x4 transform_;
    /// Blending mode.
    BlendMode blendMode_;
    /// Scissor rectangle.
    IntRect scissor_;
    /// Texture.
    Texture* texture_;
    /// Quads.
    PODVector<UIQuad>* quads_;
    /// Quad start index.
    unsigned quadStart_;
    /// Number of quads.
    unsigned quadCount_;
};

}
