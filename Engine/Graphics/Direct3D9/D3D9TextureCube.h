//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
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

#include "RenderSurface.h"
#include "SharedPtr.h"
#include "Texture.h"

class Deserializer;
class Image;

/// Cube texture resource
class TextureCube : public Texture
{
    OBJECT(TextureCube);
    
public:
    /// Construct
    TextureCube(Context* context);
    /// Destruct
    virtual ~TextureCube();
    /// Register object factory
    static void RegisterObject(Context* context);
    
    /// Load resource. Return true if successful
    virtual bool Load(Deserializer& source);
    /// Release default pool resources
    virtual void OnDeviceLost();
    /// ReCreate default pool resources
    virtual void OnDeviceReset();
    /// Release texture
    virtual void Release();
    
    /// Set size, format and usage. Return true if successful
    bool SetSize(int size, unsigned format, TextureUsage usage = TEXTURE_STATIC);
    /// Load one face from a stream. Return true if successful
    bool Load(CubeMapFace face, Deserializer& source);
    /// Load one face from an image. Return true if successful
    bool Load(CubeMapFace face, SharedPtr<Image> image);
    /// Lock a rectangular area from one face and mipmap level. A null rect locks the entire face. Return true if successful
    bool Lock(CubeMapFace face, unsigned level, const IntRect* rect, LockedRect& lockedRect);
    /// Unlock texture
    void Unlock();
    
    /// Return render surface for one face
    RenderSurface* GetRenderSurface(CubeMapFace face) const { return renderSurfaces_[face]; }
    
private:
    /// Create texture
    bool Create();
    
    /// Render surfaces
    SharedPtr<RenderSurface> renderSurfaces_[MAX_CUBEMAP_FACES];
    /// Memory use per face
    unsigned faceMemoryUse_[MAX_CUBEMAP_FACES];
    /// Currently locked mipmap level
    int lockedLevel_;
    /// Currently locked face
    CubeMapFace lockedFace_;
};