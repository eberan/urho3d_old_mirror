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
#include "AnimatedModel.h"
#include "Animation.h"
#include "AnimationController.h"
#include "Camera.h"
#include "Context.h"
#include "CustomGeometry.h"
#include "DebugRenderer.h"
#include "DecalSet.h"
#include "Graphics.h"
#include "GraphicsEvents.h"
#include "GraphicsImpl.h"
#include "IndexBuffer.h"
#include "Log.h"
#include "Material.h"
#include "Octree.h"
#include "ParticleEmitter.h"
#include "ProcessUtils.h"
#include "Profiler.h"
#include "Shader.h"
#include "ShaderVariation.h"
#include "Skybox.h"
#include "StringUtils.h"
#include "Technique.h"
#include "Terrain.h"
#include "TerrainPatch.h"
#include "Texture2D.h"
#include "TextureCube.h"
#include "VertexBuffer.h"
#include "VertexDeclaration.h"
#include "Zone.h"

#include "DebugNew.h"

#ifdef _MSC_VER
#pragma warning(disable:4355)
#endif

namespace Urho3D
{

extern "C" HWND WIN_GetWindowHandle(SDL_Window* window);

static const D3DCMPFUNC d3dCmpFunc[] =
{
    D3DCMP_ALWAYS,
    D3DCMP_EQUAL,
    D3DCMP_NOTEQUAL,
    D3DCMP_LESS,
    D3DCMP_LESSEQUAL,
    D3DCMP_GREATER,
    D3DCMP_GREATEREQUAL
};

static const D3DTEXTUREFILTERTYPE d3dMinMagFilter[] =
{
    D3DTEXF_POINT,
    D3DTEXF_LINEAR,
    D3DTEXF_LINEAR,
    D3DTEXF_ANISOTROPIC
};

static const D3DTEXTUREFILTERTYPE d3dMipFilter[] =
{
    D3DTEXF_POINT,
    D3DTEXF_POINT,
    D3DTEXF_LINEAR,
    D3DTEXF_ANISOTROPIC
};

static const D3DTEXTUREADDRESS d3dAddressMode[] =
{
    D3DTADDRESS_WRAP,
    D3DTADDRESS_MIRROR,
    D3DTADDRESS_CLAMP,
    D3DTADDRESS_BORDER
};

static const DWORD d3dBlendEnable[] =
{
    FALSE,
    TRUE,
    TRUE,
    TRUE,
    TRUE,
    TRUE,
    TRUE
};

static const D3DBLEND d3dSrcBlend[] =
{
    D3DBLEND_ONE,
    D3DBLEND_ONE,
    D3DBLEND_DESTCOLOR,
    D3DBLEND_SRCALPHA,
    D3DBLEND_SRCALPHA,
    D3DBLEND_ONE,
    D3DBLEND_INVDESTALPHA,
};

static const D3DBLEND d3dDestBlend[] =
{
    D3DBLEND_ZERO,
    D3DBLEND_ONE,
    D3DBLEND_ZERO,
    D3DBLEND_INVSRCALPHA,
    D3DBLEND_ONE,
    D3DBLEND_INVSRCALPHA,
    D3DBLEND_DESTALPHA
};

static const D3DCULL d3dCullMode[] =
{
    D3DCULL_NONE,
    D3DCULL_CCW,
    D3DCULL_CW
};

static const D3DFILLMODE d3dFillMode[] =
{
    D3DFILL_SOLID,
    D3DFILL_WIREFRAME,
    D3DFILL_POINT
};

static const D3DSTENCILOP d3dStencilOp[] =
{
    D3DSTENCILOP_KEEP,
    D3DSTENCILOP_ZERO,
    D3DSTENCILOP_REPLACE,
    D3DSTENCILOP_INCR,
    D3DSTENCILOP_DECR
};

static unsigned GetD3DColor(const Color& color)
{
    unsigned r = (unsigned)(Clamp(color.r_ * 255.0f, 0.0f, 255.0f));
    unsigned g = (unsigned)(Clamp(color.g_ * 255.0f, 0.0f, 255.0f));
    unsigned b = (unsigned)(Clamp(color.b_ * 255.0f, 0.0f, 255.0f));
    unsigned a = (unsigned)(Clamp(color.a_ * 255.0f, 0.0f, 255.0f));
    return (((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff);
}

static unsigned numInstances = 0;

static unsigned depthStencilFormat = D3DFMT_D24S8;

OBJECTTYPESTATIC(Graphics);

Graphics::Graphics(Context* context) :
    Object(context),
    impl_(new GraphicsImpl()),
    externalWindow_(0),
    width_(0),
    height_(0),
    multiSample_(1),
    windowPosX_(0),
    windowPosY_(0),
    fullscreen_(false),
    resizable_(false),
    vsync_(false),
    tripleBuffer_(false),
    sRGB_(false),
    deviceLost_(false),
    lightPrepassSupport_(false),
    deferredSupport_(false),
    hardwareShadowSupport_(false),
    streamOffsetSupport_(false),
    sRGBSupport_(true),
    hasSM3_(false),
    forceSM2_(false),
    numPrimitives_(0),
    numBatches_(0),
    maxScratchBufferRequest_(0),
    defaultTextureFilterMode_(FILTER_BILINEAR)
{
    SetTextureUnitMappings();
    
    // If first instance in this process, initialize SDL under static mutex. Note that Graphics subsystem will also be in charge
    // of shutting down SDL as a whole, so it should be the last SDL-using subsystem (Audio and Input also use SDL) alive
    {
        MutexLock lock(GetStaticMutex());
        
        if (!numInstances)
            SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);
        ++numInstances;
    }
}

Graphics::~Graphics()
{
    // Release all GPU objects that still exist
    for (Vector<GPUObject*>::Iterator i = gpuObjects_.Begin(); i != gpuObjects_.End(); ++i)
        (*i)->Release();
    gpuObjects_.Clear();
    
    vertexDeclarations_.Clear();
    
    if (impl_->defaultColorSurface_)
    {
        impl_->defaultColorSurface_->Release();
        impl_->defaultColorSurface_ = 0;
    }
    if (impl_->defaultDepthStencilSurface_)
    {
        impl_->defaultDepthStencilSurface_->Release();
        impl_->defaultDepthStencilSurface_ = 0;
    }
    if (impl_->device_)
    {
        impl_->device_->Release();
        impl_->device_ = 0;
    }
    if (impl_->interface_)
    {
        impl_->interface_->Release();
        impl_->interface_ = 0;
    }
    if (impl_->window_)
    {
        MutexLock lock(GetStaticMutex());
        
        SDL_ShowCursor(SDL_TRUE);
        SDL_DestroyWindow(impl_->window_);
        impl_->window_ = 0;
    }
    
    delete impl_;
    impl_ = 0;
    
    // If last instance in this process, shut down SDL under static mutex
    {
        MutexLock lock(GetStaticMutex());
        
        --numInstances;
        if (!numInstances)
            SDL_Quit();
    }
}

void Graphics::SetExternalWindow(void* window)
{
    if (!impl_->window_)
        externalWindow_ = window;
    else
        LOGERROR("Window already opened, can not set external window");
}

void Graphics::SetWindowTitle(const String& windowTitle)
{
    windowTitle_ = windowTitle;
    if (impl_->window_)
        SDL_SetWindowTitle(impl_->window_, windowTitle_.CString());
}

bool Graphics::SetMode(int width, int height, bool fullscreen, bool resizable, bool vsync, bool tripleBuffer, int multiSample)
{
    PROFILE(SetScreenMode);
    
    // Find out the full screen mode display format (match desktop color depth)
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(0, &mode);
    D3DFORMAT fullscreenFormat = SDL_BITSPERPIXEL(mode.format) == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
    
    // If zero dimensions in windowed mode, set default. If zero in fullscreen, use desktop mode
    if (!width || !height)
    {
        if (!fullscreen)
        {
            width = 1024;
            height = 768;
        }
        else
        {
            width = mode.w;
            height = mode.h;
        }
    }
    
    // Fullscreen can not be resizable
    if (fullscreen)
        resizable = false;
    
    multiSample = Clamp(multiSample, 1, (int)D3DMULTISAMPLE_16_SAMPLES);
    
    // If nothing changes, do not reset the device
    if (width == width_ && height == height_ && fullscreen == fullscreen_ &&  resizable == resizable_ &&
        vsync == vsync_ && tripleBuffer == tripleBuffer_ && multiSample == multiSample_)
        return true;
    
    if (!impl_->window_)
    {
        if (!OpenWindow(width, height, resizable))
            return false;
    }
    
    if (!impl_->interface_)
    {
        if (!CreateInterface())
            return false;
        
        CheckFeatureSupport();
    }
    
    // Note: GetMultiSample() will not reflect the actual hardware multisample mode, but rather what the caller wanted.
    multiSample_ = multiSample;
    
    // Check fullscreen mode validity. If not valid, revert to windowed
    if (fullscreen)
    {
        PODVector<IntVector2> resolutions = GetResolutions();
        fullscreen = false;
        for (unsigned i = 0; i < resolutions.Size(); ++i)
        {
            if (width == resolutions[i].x_ && height == resolutions[i].y_)
            {
                fullscreen = true;
                break;
            }
        }
    }
    
    // Fall back to non-multisampled if unsupported multisampling mode
    if (multiSample > 1)
    {
        if (FAILED(impl_->interface_->CheckDeviceMultiSampleType(impl_->adapter_, impl_->deviceType_, fullscreenFormat, FALSE,
            (D3DMULTISAMPLE_TYPE)multiSample, NULL)))
            multiSample = 1;
    }
    
    AdjustWindow(width, height, fullscreen);
    
    if (fullscreen)
    {
        impl_->presentParams_.BackBufferFormat = fullscreenFormat;
        impl_->presentParams_.Windowed         = false;
    }
    else
    {
        impl_->presentParams_.BackBufferFormat = D3DFMT_UNKNOWN;
        impl_->presentParams_.Windowed         = true;
    }
    
    impl_->presentParams_.BackBufferWidth            = width;
    impl_->presentParams_.BackBufferHeight           = height;
    impl_->presentParams_.BackBufferCount            = tripleBuffer ? 2 : 1;
    impl_->presentParams_.MultiSampleType            = multiSample > 1 ? (D3DMULTISAMPLE_TYPE)multiSample : D3DMULTISAMPLE_NONE;
    impl_->presentParams_.MultiSampleQuality         = 0;
    impl_->presentParams_.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    impl_->presentParams_.hDeviceWindow              = WIN_GetWindowHandle(impl_->window_);
    impl_->presentParams_.EnableAutoDepthStencil     = TRUE;
    impl_->presentParams_.AutoDepthStencilFormat     = D3DFMT_D24S8;
    impl_->presentParams_.Flags                      = D3DPRESENT_LINEAR_CONTENT;
    impl_->presentParams_.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

    if (vsync)
        impl_->presentParams_.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    else
        impl_->presentParams_.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    
    width_ = width;
    height_ = height;
    fullscreen_ = fullscreen;
    resizable_ = resizable;
    vsync_ = vsync;
    tripleBuffer_ = tripleBuffer;
    
    if (!impl_->device_)
    {
        unsigned adapter = D3DADAPTER_DEFAULT;
        unsigned deviceType = D3DDEVTYPE_HAL;
        
        // Check for PerfHUD adapter
        for (unsigned i=0; i < impl_->interface_->GetAdapterCount(); ++i)
        {
            D3DADAPTER_IDENTIFIER9 identifier;
            impl_->interface_->GetAdapterIdentifier(i, 0, &identifier);
            if (strstr(identifier.Description, "PerfHUD") != 0)
            {
                adapter = i;
                deviceType = D3DDEVTYPE_REF;
                break;
            }
        }
        
        impl_->interface_->GetAdapterIdentifier(adapter, 0, &impl_->adapterIdentifier_);
        if (!CreateDevice(adapter, deviceType))
            return false;
    }
    else
        ResetDevice();
    
    // Clear the initial window contents to black
    impl_->device_->BeginScene();
    Clear(CLEAR_COLOR);
    impl_->device_->EndScene();
    impl_->device_->Present(0, 0, 0, 0);
    
    #ifdef ENABLE_LOGGING
    String msg;
    msg.AppendWithFormat("Set screen mode %dx%d %s", width_, height_, (fullscreen_ ? "fullscreen" : "windowed"));
    if (resizable_)
        msg.Append(" resizable");
    if (multiSample > 1)
        msg.AppendWithFormat(" multisample %d", multiSample);
    LOGINFO(msg);
    #endif

    using namespace ScreenMode;
    
    VariantMap eventData;
    eventData[P_WIDTH] = width_;
    eventData[P_HEIGHT] = height_;
    eventData[P_FULLSCREEN] = fullscreen_;
    eventData[P_RESIZABLE] = resizable_;
    SendEvent(E_SCREENMODE, eventData);
    
    return true;
}

bool Graphics::SetMode(int width, int height)
{
    return SetMode(width, height, fullscreen_, resizable_, vsync_, tripleBuffer_, multiSample_);
}

void Graphics::SetSRGB(bool enabled)
{
    sRGB_ = enabled;
}

bool Graphics::ToggleFullscreen()
{
    return SetMode(width_, height_, !fullscreen_, resizable_, vsync_, tripleBuffer_, multiSample_);
}

void Graphics::Close()
{
    if (impl_->window_)
    {
        MutexLock lock(GetStaticMutex());
        
        SDL_ShowCursor(SDL_TRUE);
        SDL_DestroyWindow(impl_->window_);
        impl_->window_ = 0;
    }
}

bool Graphics::TakeScreenShot(Image& destImage)
{
    PROFILE(TakeScreenShot);
    
    if (!impl_->device_)
        return false;
    
    D3DSURFACE_DESC surfaceDesc;
    impl_->defaultColorSurface_->GetDesc(&surfaceDesc);
    
    // If possible, get the backbuffer data, because it is a lot faster.
    // However, if we are multisampled, need to use the front buffer
    bool useBackBuffer = true;
    if (impl_->presentParams_.MultiSampleType)
    {
        useBackBuffer = false;
        surfaceDesc.Format = D3DFMT_A8R8G8B8;
    }
    
    IDirect3DSurface9* surface = 0;
    impl_->device_->CreateOffscreenPlainSurface(width_, height_, surfaceDesc.Format, D3DPOOL_SYSTEMMEM, &surface, 0);
    if (!surface)
        return false;
    
    if (useBackBuffer)
        impl_->device_->GetRenderTargetData(impl_->defaultColorSurface_, surface);
    else
        impl_->device_->GetFrontBufferData(0, surface);
    
    D3DLOCKED_RECT lockedRect;
    lockedRect.pBits = 0;
    surface->LockRect(&lockedRect, 0, D3DLOCK_NOSYSLOCK | D3DLOCK_READONLY);
    if (!lockedRect.pBits)
    {
        surface->Release();
        return false;
    }
    
    destImage.SetSize(width_, height_, 3);
    unsigned char* destData = destImage.GetData();
    
    if (surfaceDesc.Format == D3DFMT_R5G6B5)
    {
        for (int y = 0; y < height_; ++y)
        {
            unsigned short* src = (unsigned short*)((unsigned char*)lockedRect.pBits + y * lockedRect.Pitch);
            unsigned char* dest = destData + y * width_ * 3;
            
            for (int x = 0; x < width_; ++x)
            {
                unsigned short rgb = *src++;
                int b = rgb & 31;
                int g = (rgb >> 5) & 63;
                int r = (rgb >> 11);
                
                *dest++ = (int)(r * 255.0f / 31.0f);
                *dest++ = (int)(g * 255.0f / 63.0f);
                *dest++ = (int)(b * 255.0f / 31.0f);
            }
        }
    }
    else
    {
        for (int y = 0; y < height_; ++y)
        {
            unsigned char* src = (unsigned char*)lockedRect.pBits + y * lockedRect.Pitch;
            unsigned char* dest = destData + y * width_ * 3;
            
            for (int x = 0; x < width_; ++x)
            {
                *dest++ = src[2];
                *dest++ = src[1];
                *dest++ = src[0];
                src += 4;
            }
        }
    }
    
    surface->UnlockRect();
    surface->Release();
    
    return true;
}

bool Graphics::BeginFrame()
{
    if (!IsInitialized())
        return false;
    
    // If using an external window, check it for size changes, and reset screen mode if necessary
    if (externalWindow_)
    {
        int width, height;
        
        SDL_GetWindowSize(impl_->window_, &width, &height);
        if (width != width_ || height != height_)
            SetMode(width, height);
    }
    
    // Check for lost device before rendering
    HRESULT hr = impl_->device_->TestCooperativeLevel();
    if (hr != D3D_OK)
    {
        PROFILE(DeviceLost);
        
        deviceLost_ = true;
        
        // The device can not be reset yet, sleep and try again eventually
        if (hr == D3DERR_DEVICELOST)
        {
            Sleep(20);
            return false;
        }
        // The device is lost, but ready to be reset. Reset device but do not render on this frame yet
        if (hr == D3DERR_DEVICENOTRESET)
        {
            ResetDevice();
            return false;
        }
    }
    
    impl_->device_->BeginScene();
    
    // Set default rendertarget and depth buffer
    ResetRenderTargets();
    
    // Cleanup textures from previous frame
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        SetTexture(i, 0);
    
    // Cleanup stream frequencies from previous frame
    ResetStreamFrequencies();
    
    numPrimitives_ = 0;
    numBatches_ = 0;
    
    SendEvent(E_BEGINRENDERING);
    
    return true;
}

void Graphics::EndFrame()
{
    if (!IsInitialized())
        return;
    
    PROFILE(Present);
    
    SendEvent(E_ENDRENDERING);
    
    impl_->device_->EndScene();
    impl_->device_->Present(0, 0, 0, 0);
    
    // Clean up too large scratch buffers
    CleanupScratchBuffers();
}

void Graphics::Clear(unsigned flags, const Color& color, float depth, unsigned stencil)
{
    DWORD d3dFlags = 0;
    if (flags & CLEAR_COLOR)
        d3dFlags |= D3DCLEAR_TARGET;
    if (flags & CLEAR_DEPTH)
        d3dFlags |= D3DCLEAR_ZBUFFER;
    if (flags & CLEAR_STENCIL)
        d3dFlags |= D3DCLEAR_STENCIL;
    
    impl_->device_->Clear(0, 0, d3dFlags, GetD3DColor(color), depth, stencil);
}

bool Graphics::ResolveToTexture(Texture2D* destination, const IntRect& viewport)
{
    if (!destination || !destination->GetRenderSurface() || destination->GetWidth() != width_ ||
        destination->GetHeight() != height_)
        return false;
    
    PROFILE(ResolveToTexture);
    
    IntRect vpCopy = viewport;
    if (vpCopy.right_ <= vpCopy.left_)
        vpCopy.right_ = vpCopy.left_ + 1;
    if (vpCopy.bottom_ <= vpCopy.top_)
        vpCopy.bottom_ = vpCopy.top_ + 1;
    
    RECT rect;
    rect.left = Clamp(vpCopy.left_, 0, width_);
    rect.top = Clamp(vpCopy.top_, 0, height_);
    rect.right = Clamp(vpCopy.right_, 0, width_);
    rect.bottom = Clamp(vpCopy.bottom_, 0, height_);
    
    return SUCCEEDED(impl_->device_->StretchRect(impl_->defaultColorSurface_, &rect,
        (IDirect3DSurface9*)destination->GetRenderSurface()->GetSurface(), &rect, D3DTEXF_NONE));
}

void Graphics::Draw(PrimitiveType type, unsigned vertexStart, unsigned vertexCount)
{
    if (!vertexCount)
        return;
    
    ResetStreamFrequencies();
    
    unsigned primitiveCount = 0;
    
    switch (type)
    {
    case TRIANGLE_LIST:
        primitiveCount = vertexCount / 3;
        impl_->device_->DrawPrimitive(D3DPT_TRIANGLELIST, vertexStart, primitiveCount);
        break;
        
    case LINE_LIST:
        primitiveCount = vertexCount / 2;
        impl_->device_->DrawPrimitive(D3DPT_LINELIST, vertexStart, primitiveCount);
        break;
    }
    
    numPrimitives_ += primitiveCount;
    ++numBatches_;
}

void Graphics::Draw(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount)
{
    if (!indexCount)
        return;
    
    ResetStreamFrequencies();
    
    unsigned primitiveCount = 0;
    
    switch (type)
    {
    case TRIANGLE_LIST:
        primitiveCount = indexCount / 3;
        impl_->device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, minVertex, vertexCount, indexStart, primitiveCount);
        break;
        
    case LINE_LIST:
        primitiveCount = indexCount / 2;
        impl_->device_->DrawIndexedPrimitive(D3DPT_LINELIST, 0, minVertex, vertexCount, indexStart, primitiveCount);
        break;
    }
    
    numPrimitives_ += primitiveCount;
    ++numBatches_;
}

void Graphics::DrawInstanced(PrimitiveType type, unsigned indexStart, unsigned indexCount, unsigned minVertex, unsigned vertexCount,
    unsigned instanceCount)
{
    if (!indexCount || !instanceCount)
        return;
    
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
    {
        VertexBuffer* buffer = vertexBuffers_[i];
        if (buffer)
        {
            if (buffer->GetElementMask() & MASK_INSTANCEMATRIX1)
                SetStreamFrequency(i, D3DSTREAMSOURCE_INSTANCEDATA | 1);
            else
                SetStreamFrequency(i, D3DSTREAMSOURCE_INDEXEDDATA | instanceCount);
        }
    }
    
    unsigned primitiveCount = 0;
    
    switch (type)
    {
    case TRIANGLE_LIST:
        primitiveCount = indexCount / 3;
        impl_->device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, minVertex, vertexCount, indexStart, primitiveCount);
        break;
        
    case LINE_LIST:
        primitiveCount = indexCount / 2;
        impl_->device_->DrawIndexedPrimitive(D3DPT_LINELIST, 0, minVertex, vertexCount, indexStart, primitiveCount);
        break;
    }
    
    numPrimitives_ += instanceCount * primitiveCount;
    ++numBatches_;
}

void Graphics::SetVertexBuffer(VertexBuffer* buffer)
{
    Vector<VertexBuffer*> vertexBuffers(1);
    PODVector<unsigned> elementMasks(1);
    vertexBuffers[0] = buffer;
    elementMasks[0] = MASK_DEFAULT;
    SetVertexBuffers(vertexBuffers, elementMasks);
}

bool Graphics::SetVertexBuffers(const Vector<VertexBuffer*>& buffers, const PODVector<unsigned>&
    elementMasks, unsigned instanceOffset)
{
   if (buffers.Size() > MAX_VERTEX_STREAMS)
    {
        LOGERROR("Too many vertex buffers");
        return false;
    }
    if (buffers.Size() != elementMasks.Size())
    {
        LOGERROR("Amount of element masks and vertex buffers does not match");
        return false;
    }
    
    // Build vertex declaration hash code out of the buffers & masks
    unsigned long long hash = 0;
    for (unsigned i = 0; i < buffers.Size(); ++i)
    {
        if (!buffers[i])
            continue;
        
        hash |= buffers[i]->GetBufferHash(i, elementMasks[i]);
    }
    
    if (hash)
    {
        // If no previous vertex declaration for that hash, create new
        if (!vertexDeclarations_.Contains(hash))
        {
            SharedPtr<VertexDeclaration> newDeclaration(new VertexDeclaration(this, buffers, elementMasks));
            if (!newDeclaration->GetDeclaration())
            {
                LOGERROR("Failed to create vertex declaration");
                return false;
            }
            
            vertexDeclarations_[hash] = newDeclaration;
        }
        
        VertexDeclaration* declaration = vertexDeclarations_[hash];
        if (declaration != vertexDeclaration_)
        {
            impl_->device_->SetVertexDeclaration(declaration->GetDeclaration());
            vertexDeclaration_ = declaration;
        }
    }
    
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
    {
        VertexBuffer* buffer = 0;
        unsigned offset = 0;
        
        if (i < buffers.Size())
        {
            buffer = buffers[i];
            if (buffer && buffer->GetElementMask() & MASK_INSTANCEMATRIX1)
                offset = instanceOffset * buffer->GetVertexSize();
        }
        
        if (buffer != vertexBuffers_[i] || offset != streamOffsets_[i])
        {
            if (buffer)
                impl_->device_->SetStreamSource(i, (IDirect3DVertexBuffer9*)buffer->GetGPUObject(), offset, buffer->GetVertexSize());
            else
                impl_->device_->SetStreamSource(i, 0, 0, 0);
            
            vertexBuffers_[i] = buffer;
            streamOffsets_[i] = offset;
        }
    }
    
    return true;
}

bool Graphics::SetVertexBuffers(const Vector<SharedPtr<VertexBuffer> >& buffers, const PODVector<unsigned>&
    elementMasks, unsigned instanceOffset)
{
   if (buffers.Size() > MAX_VERTEX_STREAMS)
    {
        LOGERROR("Too many vertex buffers");
        return false;
    }
    if (buffers.Size() != elementMasks.Size())
    {
        LOGERROR("Amount of element masks and vertex buffers does not match");
        return false;
    }
    
    unsigned long long hash = 0;
    for (unsigned i = 0; i < buffers.Size(); ++i)
    {
        if (!buffers[i])
            continue;
        
        hash |= buffers[i]->GetBufferHash(i, elementMasks[i]);
    }
    
    if (hash)
    {
        if (!vertexDeclarations_.Contains(hash))
        {
            SharedPtr<VertexDeclaration> newDeclaration(new VertexDeclaration(this, buffers, elementMasks));
            if (!newDeclaration->GetDeclaration())
            {
                LOGERROR("Failed to create vertex declaration");
                return false;
            }
            
            vertexDeclarations_[hash] = newDeclaration;
        }
        
        VertexDeclaration* declaration = vertexDeclarations_[hash];
        if (declaration != vertexDeclaration_)
        {
            impl_->device_->SetVertexDeclaration(declaration->GetDeclaration());
            vertexDeclaration_ = declaration;
        }
    }
    
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
    {
        VertexBuffer* buffer = 0;
        unsigned offset = 0;
        
        if (i < buffers.Size())
        {
            buffer = buffers[i];
            if (buffer && buffer->GetElementMask() & MASK_INSTANCEMATRIX1)
                offset = instanceOffset * buffer->GetVertexSize();
        }
        
        if (buffer != vertexBuffers_[i] || offset != streamOffsets_[i])
        {
            if (buffer)
                impl_->device_->SetStreamSource(i, (IDirect3DVertexBuffer9*)buffer->GetGPUObject(), offset, buffer->GetVertexSize());
            else
                impl_->device_->SetStreamSource(i, 0, 0, 0);
            
            vertexBuffers_[i] = buffer;
            streamOffsets_[i] = offset;
        }
    }
    
    return true;
}

void Graphics::SetIndexBuffer(IndexBuffer* buffer)
{
    if (buffer != indexBuffer_)
    {
        if (buffer)
            impl_->device_->SetIndices((IDirect3DIndexBuffer9*)buffer->GetGPUObject());
        else
            impl_->device_->SetIndices(0);
            
        indexBuffer_ = buffer;
    }
}

void Graphics::SetShaders(ShaderVariation* vs, ShaderVariation* ps)
{
    if (vs == vertexShader_ && ps == pixelShader_)
        return;
    
    ClearParameterSources();
    
    if (vs != vertexShader_)
    {
        // Clear all previous vertex shader register mappings
        for (HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Begin(); i != shaderParameters_.End(); ++i)
        {
            if (i->second_.type_ == VS)
                i->second_.register_ = M_MAX_UNSIGNED;
        }
        
        // Create the shader now if not yet created. If already attempted, do not retry
        if (vs && !vs->IsCreated())
        {
            if (!vs->IsFailed())
            {
                PROFILE(CreateVertexShader);

                bool success = vs->Create();
                if (success)
                    LOGDEBUG("Created vertex shader " + vs->GetName());
                else
                {
                    LOGERROR("Failed to create vertex shader " + vs->GetName());
                    vs = 0;
                }
            }
            else
                vs = 0;
        }
        
        if (vs && vs->GetShaderType() == VS)
        {
            impl_->device_->SetVertexShader((IDirect3DVertexShader9*)vs->GetGPUObject());
            
            // Update the parameter-to-register mappings
            const HashMap<StringHash, ShaderParameter>& parameters = vs->GetParameters();
            for (HashMap<StringHash, ShaderParameter>::ConstIterator i = parameters.Begin(); i != parameters.End(); ++i)
                shaderParameters_[i->first_].register_ = i->second_.register_;
        }
        else
        {
            impl_->device_->SetVertexShader(0);
            vs = 0;
        }
        
        vertexShader_ = vs;
    }
    
    if (ps != pixelShader_)
    {
        for (HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Begin(); i != shaderParameters_.End(); ++i)
        {
            if (i->second_.type_ == PS)
                i->second_.register_ = M_MAX_UNSIGNED;
        }
        
        if (ps && !ps->IsCreated())
        {
            if (!ps->IsFailed())
            {
                PROFILE(CreatePixelShader);
                
                bool success = ps->Create();
                if (success)
                    LOGDEBUG("Created pixel shader " + ps->GetName());
                else
                {
                    LOGERROR("Failed to create pixel shader " + ps->GetName());
                    ps = 0;
                }
            }
            else
                ps = 0;
        }
        
        if (ps && ps->GetShaderType() == PS)
        {
            impl_->device_->SetPixelShader((IDirect3DPixelShader9*)ps->GetGPUObject());
            
            const HashMap<StringHash, ShaderParameter>& parameters = ps->GetParameters();
            for (HashMap<StringHash, ShaderParameter>::ConstIterator i = parameters.Begin(); i != parameters.End(); ++i)
                shaderParameters_[i->first_].register_ = i->second_.register_;
        }
        else
        {
            impl_->device_->SetPixelShader(0);
            ps = 0;
        }
        
        pixelShader_ = ps;
    }
}

void Graphics::SetShaderParameter(StringHash param, const float* data, unsigned count)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, data, count / 4);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, data, count / 4);
}

void Graphics::SetShaderParameter(StringHash param, float value)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    float data[4];
    
    data[0] = value;
    data[1] = 0.0f;
    data[2] = 0.0f;
    data[3] = 0.0f;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, &data[0], 1);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, &data[0], 1);
}

void Graphics::SetShaderParameter(StringHash param, const Color& color)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, color.Data(), 1);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, color.Data(), 1);
}

void Graphics::SetShaderParameter(StringHash param, const Matrix3& matrix)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    float data[12];
    
    data[0] = matrix.m00_;
    data[1] = matrix.m01_;
    data[2] = matrix.m02_;
    data[3] = 0.0f;
    data[4] = matrix.m10_;
    data[5] = matrix.m11_;
    data[6] = matrix.m12_;
    data[7] = 0.0f;
    data[8] = matrix.m20_;
    data[9] = matrix.m21_;
    data[10] = matrix.m22_;
    data[11] = 0.0f;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, &data[0], 3);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, &data[0], 3);
}

void Graphics::SetShaderParameter(StringHash param, const Vector3& vector)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    float data[4];
    
    data[0] = vector.x_;
    data[1] = vector.y_;
    data[2] = vector.z_;
    data[3] = 0.0f;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, &data[0], 1);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, &data[0], 1);
}

void Graphics::SetShaderParameter(StringHash param, const Matrix4& matrix)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, matrix.Data(), 4);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, matrix.Data(), 4);
}

void Graphics::SetShaderParameter(StringHash param, const Vector4& vector)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, vector.Data(), 1);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, vector.Data(), 1);
}

void Graphics::SetShaderParameter(StringHash param, const Matrix3x4& matrix)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    if (i == shaderParameters_.End() || i->second_.register_ >= MAX_CONSTANT_REGISTERS)
        return;
    
    if (i->second_.type_ == VS)
        impl_->device_->SetVertexShaderConstantF(i->second_.register_, matrix.Data(), 3);
    else
        impl_->device_->SetPixelShaderConstantF(i->second_.register_, matrix.Data(), 3);
}

void Graphics::RegisterShaderParameter(StringHash param, const ShaderParameter& definition)
{
    HashMap<StringHash, ShaderParameter>::Iterator i = shaderParameters_.Find(param);
    
    if (i == shaderParameters_.End())
    {
        // Define new parameter
        i = shaderParameters_.Insert(MakePair(param, definition));
        i->second_.register_ = M_MAX_UNSIGNED;
        
        // Rehash the parameters to ensure minimum load factor and fast queries
        shaderParameters_.Rehash(NextPowerOfTwo(shaderParameters_.Size()));
    }
    else
    {
        // Existing parameter: check that there is no conflict
        if (i->second_.type_ != definition.type_)
            LOGWARNING("Shader type mismatch on shader parameter " + String(param));
        
        // The same parameter is possibly defined with different sizes in different shaders. Use the highest size
        if (i->second_.regCount_ < definition.regCount_)
            i->second_.regCount_ = definition.regCount_;
    }
}

bool Graphics::NeedParameterUpdate(ShaderParameterGroup group, const void* source)
{
    if ((unsigned)shaderParameterSources_[group] == M_MAX_UNSIGNED || shaderParameterSources_[group] != source)
    {
        shaderParameterSources_[group] = source;
        return true;
    }
    else
        return false;
}

bool Graphics::HasShaderParameter(ShaderType type, StringHash param)
{
    if (type == VS)
        return vertexShader_ && vertexShader_->HasParameter(param);
    else
        return pixelShader_ && pixelShader_->HasParameter(param);
}

bool Graphics::HasTextureUnit(TextureUnit unit)
{
    return pixelShader_ && pixelShader_->HasTextureUnit(unit);
}

void Graphics::ClearParameterSource(ShaderParameterGroup group)
{
    shaderParameterSources_[group] = (const void*)M_MAX_UNSIGNED;
}

void Graphics::ClearParameterSources()
{
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        shaderParameterSources_[i] = (const void*)M_MAX_UNSIGNED;
}

void Graphics::ClearTransformSources()
{
    shaderParameterSources_[SP_CAMERA] = (const void*)M_MAX_UNSIGNED;
    shaderParameterSources_[SP_OBJECTTRANSFORM] = (const void*)M_MAX_UNSIGNED;
}

void Graphics::SetTexture(unsigned index, Texture* texture)
{
    if (index >= MAX_TEXTURE_UNITS)
        return;
    
    // Check if texture is currently bound as a rendertarget. In that case, use its backup texture, or blank if not defined
    if (texture)
    {
        if (texture == viewTexture_ || (renderTargets_[0] && renderTargets_[0]->GetParentTexture() == texture))
            texture = texture->GetBackupTexture();
    }
    
    if (texture != textures_[index])
    {
        if (texture)
            impl_->device_->SetTexture(index, (IDirect3DBaseTexture9*)texture->GetGPUObject());
        else
            impl_->device_->SetTexture(index, 0);
        
        textures_[index] = texture;
    }
    
    if (texture)
    {
        TextureFilterMode filterMode = texture->GetFilterMode();
        if (filterMode == FILTER_DEFAULT)
            filterMode = defaultTextureFilterMode_;
        
        D3DTEXTUREFILTERTYPE minMag, mip;
        minMag = d3dMinMagFilter[filterMode];
        if (minMag != impl_->minMagFilters_[index])
        {
            impl_->device_->SetSamplerState(index, D3DSAMP_MAGFILTER, minMag);
            impl_->device_->SetSamplerState(index, D3DSAMP_MINFILTER, minMag);
            impl_->minMagFilters_[index] = minMag;
        }
        mip = d3dMipFilter[filterMode];
        if (mip != impl_->mipFilters_[index])
        {
            impl_->device_->SetSamplerState(index, D3DSAMP_MIPFILTER, mip);
            impl_->mipFilters_[index] = mip;
        }
        D3DTEXTUREADDRESS u, v;
        u = d3dAddressMode[texture->GetAddressMode(COORD_U)];
        if (u != impl_->uAddressModes_[index])
        {
            impl_->device_->SetSamplerState(index, D3DSAMP_ADDRESSU, u);
            impl_->uAddressModes_[index] = u;
        }
        v = d3dAddressMode[texture->GetAddressMode(COORD_V)];
        if (v != impl_->vAddressModes_[index])
        {
            impl_->device_->SetSamplerState(index, D3DSAMP_ADDRESSV, v);
            impl_->vAddressModes_[index] = v;
        }
        if (texture->GetType() == TextureCube::GetTypeStatic())
        {
            D3DTEXTUREADDRESS w = d3dAddressMode[texture->GetAddressMode(COORD_W)];
            if (w != impl_->wAddressModes_[index])
            {
                impl_->device_->SetSamplerState(index, D3DSAMP_ADDRESSW, w);
                impl_->wAddressModes_[index] = w;
            }
        }
        if (u == D3DTADDRESS_BORDER || v == D3DTADDRESS_BORDER)
        {
            const Color& borderColor = texture->GetBorderColor();
            if (borderColor != impl_->borderColors_[index])
            {
                impl_->device_->SetSamplerState(index, D3DSAMP_BORDERCOLOR, GetD3DColor(borderColor));
                impl_->borderColors_[index] = borderColor;
            }
        }
        bool sRGB = texture->GetSRGB();
        if (sRGB != impl_->sRGBModes_[index])
        {
            impl_->device_->SetSamplerState(index, D3DSAMP_SRGBTEXTURE, sRGB ? TRUE : FALSE);
            impl_->sRGBModes_[index] = sRGB;
        }
    }
}

void Graphics::SetDefaultTextureFilterMode(TextureFilterMode mode)
{
    defaultTextureFilterMode_ = mode;
}

void Graphics::ResetRenderTargets()
{
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
        SetRenderTarget(i, (RenderSurface*)0);
    SetDepthStencil((RenderSurface*)0);
    SetViewport(IntRect(0, 0, width_, height_));
}

void Graphics::ResetRenderTarget(unsigned index)
{
    SetRenderTarget(index, (RenderSurface*)0);
}

void Graphics::ResetDepthStencil()
{
    SetDepthStencil((RenderSurface*)0);
}

void Graphics::SetRenderTarget(unsigned index, RenderSurface* renderTarget)
{
    if (index >= MAX_RENDERTARGETS)
        return;
    
    IDirect3DSurface9* newColorSurface = 0;
    
    if (renderTarget)
    {
        if (renderTarget->GetUsage() != TEXTURE_RENDERTARGET)
            return;
        newColorSurface = (IDirect3DSurface9*)renderTarget->GetSurface();
    }
    else
    {
        if (!index)
            newColorSurface = impl_->defaultColorSurface_;
    }
    
    renderTargets_[index] = renderTarget;
    
    if (newColorSurface != impl_->colorSurfaces_[index])
    {
        impl_->device_->SetRenderTarget(index, newColorSurface);
        impl_->colorSurfaces_[index] = newColorSurface;
        // Setting the first rendertarget causes viewport to be reset
        if (!index)
        {
            IntVector2 rtSize = GetRenderTargetDimensions();
            viewport_ = IntRect(0, 0, rtSize.x_, rtSize.y_);
        }
    }
    
    // If the rendertarget is also bound as a texture, replace with backup texture or null
    if (renderTarget)
    {
        Texture* parentTexture = renderTarget->GetParentTexture();
        
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            if (textures_[i] == parentTexture)
                SetTexture(i, textures_[i]->GetBackupTexture());
        }
    }
    
    // First rendertarget controls sRGB write mode
    if (!index)
    {
        bool sRGBWrite = renderTarget ? renderTarget->GetParentTexture()->GetSRGB() : sRGB_;
        if (sRGBWrite != impl_->sRGBWrite_)
        {
            impl_->device_->SetRenderState(D3DRS_SRGBWRITEENABLE, sRGBWrite ? TRUE : FALSE);
            impl_->sRGBWrite_ = sRGBWrite;
        }
    }
}

void Graphics::SetRenderTarget(unsigned index, Texture2D* texture)
{
    RenderSurface* renderTarget = 0;
    if (texture)
        renderTarget = texture->GetRenderSurface();
    
    SetRenderTarget(index, renderTarget);
}

void Graphics::SetDepthStencil(RenderSurface* depthStencil)
{
    IDirect3DSurface9* newDepthStencilSurface = 0;
    if (depthStencil && depthStencil->GetUsage() == TEXTURE_DEPTHSTENCIL)
    {
        newDepthStencilSurface = (IDirect3DSurface9*)depthStencil->GetSurface();
        depthStencil_ = depthStencil;
    }
    if (!newDepthStencilSurface)
    {
        newDepthStencilSurface = impl_->defaultDepthStencilSurface_;
        depthStencil_ = 0;
    }
    if (newDepthStencilSurface != impl_->depthStencilSurface_)
    {
        impl_->device_->SetDepthStencilSurface(newDepthStencilSurface);
        impl_->depthStencilSurface_ = newDepthStencilSurface;
    }
}

void Graphics::SetDepthStencil(Texture2D* texture)
{
    RenderSurface* depthStencil = 0;
    if (texture)
        depthStencil = texture->GetRenderSurface();
    
    SetDepthStencil(depthStencil);
}

void Graphics::SetViewTexture(Texture* texture)
{
    viewTexture_ = texture;
    
    if (viewTexture_)
    {
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            if (textures_[i] == viewTexture_)
                SetTexture(i, textures_[i]->GetBackupTexture());
        }
    }
}

void Graphics::SetViewport(const IntRect& rect)
{
    IntVector2 size = GetRenderTargetDimensions();
    
    IntRect rectCopy = rect;
    
    if (rectCopy.right_ <= rectCopy.left_)
        rectCopy.right_ = rectCopy.left_ + 1;
    if (rectCopy.bottom_ <= rectCopy.top_)
        rectCopy.bottom_ = rectCopy.top_ + 1;
    rectCopy.left_ = Clamp(rectCopy.left_, 0, size.x_);
    rectCopy.top_ = Clamp(rectCopy.top_, 0, size.y_);
    rectCopy.right_ = Clamp(rectCopy.right_, 0, size.x_);
    rectCopy.bottom_ = Clamp(rectCopy.bottom_, 0, size.y_);
    
    D3DVIEWPORT9 vp;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    vp.X = rectCopy.left_;
    vp.Y = rectCopy.top_;
    vp.Width = rectCopy.Width();
    vp.Height = rectCopy.Height();
    
    impl_->device_->SetViewport(&vp);
    viewport_ = rectCopy;
    
    // Disable scissor test, needs to be re-enabled by the user
    SetScissorTest(false);
}

void Graphics::SetTextureAnisotropy(unsigned level)
{
    if (level < 1)
        level = 1;
    
    if (level != textureAnisotropy_)
    {
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
            impl_->device_->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, level);
        
        textureAnisotropy_ = level;
    }
}

void Graphics::SetBlendMode(BlendMode mode)
{
    if (mode != blendMode_)
    {
        if (d3dBlendEnable[mode] != impl_->blendEnable_)
        {
            impl_->device_->SetRenderState(D3DRS_ALPHABLENDENABLE, d3dBlendEnable[mode]);
            impl_->blendEnable_ = d3dBlendEnable[mode];
        }
        
        if (impl_->blendEnable_)
        {
            if (d3dSrcBlend[mode] != impl_->srcBlend_)
            {
                impl_->device_->SetRenderState(D3DRS_SRCBLEND, d3dSrcBlend[mode]);
                impl_->srcBlend_ = d3dSrcBlend[mode];
            }
            if (d3dDestBlend[mode] != impl_->destBlend_)
            {
                impl_->device_->SetRenderState(D3DRS_DESTBLEND, d3dDestBlend[mode]);
                impl_->destBlend_ = d3dDestBlend[mode];
            }
        }
        
        blendMode_ = mode;
    }
}

void Graphics::SetColorWrite(bool enable)
{
    if (enable != colorWrite_)
    {
        impl_->device_->SetRenderState(D3DRS_COLORWRITEENABLE, enable ? D3DCOLORWRITEENABLE_RED |
            D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA : 0);
        colorWrite_ = enable;
    }
}

void Graphics::SetCullMode(CullMode mode)
{
    if (mode != cullMode_)
    {
        impl_->device_->SetRenderState(D3DRS_CULLMODE, d3dCullMode[mode]);
        cullMode_ = mode;
    }
}

void Graphics::SetDepthBias(float constantBias, float slopeScaledBias)
{
    if (constantBias != constantDepthBias_)
    {
        impl_->device_->SetRenderState(D3DRS_DEPTHBIAS, *((DWORD*)&constantBias));
        constantDepthBias_ = constantBias;
    }
    if (slopeScaledBias != slopeScaledDepthBias_)
    {
        impl_->device_->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, *((DWORD*)&slopeScaledBias));
        slopeScaledDepthBias_ = slopeScaledBias;
    }
}

void Graphics::SetDepthTest(CompareMode mode)
{
    if (mode != depthTestMode_)
    {
        impl_->device_->SetRenderState(D3DRS_ZFUNC, d3dCmpFunc[mode]);
        depthTestMode_ = mode;
    }
}

void Graphics::SetDepthWrite(bool enable)
{
    if (enable != depthWrite_)
    {
        impl_->device_->SetRenderState(D3DRS_ZWRITEENABLE, enable ? TRUE : FALSE);
        depthWrite_ = enable;
    }
}
void Graphics::SetFillMode(FillMode mode)
{
    if (mode != fillMode_)
    {
        impl_->device_->SetRenderState(D3DRS_FILLMODE, d3dFillMode[mode]);
        fillMode_ = mode;
    }
}

void Graphics::SetScissorTest(bool enable, const Rect& rect, bool borderInclusive)
{
    // During some light rendering loops, a full rect is toggled on/off repeatedly.
    // Disable scissor in that case to reduce state changes
    if (rect.min_.x_ <= 0.0f && rect.min_.y_ <= 0.0f && rect.max_.x_ >= 1.0f && rect.max_.y_ >= 1.0f)
        enable = false;
    
    if (enable)
    {
        IntVector2 rtSize(GetRenderTargetDimensions());
        IntVector2 viewSize(viewport_.Size());
        IntVector2 viewPos(viewport_.left_, viewport_.top_);
        IntRect intRect;
        int expand = borderInclusive ? 1 : 0;
        
        intRect.left_ = Clamp((int)((rect.min_.x_ + 1.0f) * 0.5f * viewSize.x_) + viewPos.x_, 0, rtSize.x_ - 1);
        intRect.top_ = Clamp((int)((-rect.max_.y_ + 1.0f) * 0.5f * viewSize.y_) + viewPos.y_, 0, rtSize.y_ - 1);
        intRect.right_ = Clamp((int)((rect.max_.x_ + 1.0f) * 0.5f * viewSize.x_) + viewPos.x_ + expand, 0, rtSize.x_);
        intRect.bottom_ = Clamp((int)((-rect.min_.y_ + 1.0f) * 0.5f * viewSize.y_) + viewPos.y_ + expand, 0, rtSize.y_);
        
        if (intRect.right_ == intRect.left_)
            intRect.right_++;
        if (intRect.bottom_ == intRect.top_)
            intRect.bottom_++;
        
        if (intRect.right_ < intRect.left_ || intRect.bottom_ < intRect.top_)
            enable = false;
        
        if (enable && scissorRect_ != intRect)
        {
            RECT d3dRect;
            d3dRect.left = intRect.left_;
            d3dRect.top = intRect.top_;
            d3dRect.right = intRect.right_;
            d3dRect.bottom = intRect.bottom_;
            
            impl_->device_->SetScissorRect(&d3dRect);
            scissorRect_ = intRect;
        }
    }
    else
        scissorRect_ = IntRect::ZERO;
    
    if (enable != scissorTest_)
    {
        impl_->device_->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE);
        scissorTest_ = enable;
    }
}

void Graphics::SetScissorTest(bool enable, const IntRect& rect)
{
    IntVector2 rtSize(GetRenderTargetDimensions());
    IntVector2 viewSize(viewport_.Size());
    IntVector2 viewPos(viewport_.left_, viewport_.top_);
    
    if (enable)
    {
        IntRect intRect;
        intRect.left_ = Clamp(rect.left_ + viewPos.x_, 0, rtSize.x_ - 1);
        intRect.top_ = Clamp(rect.top_ + viewPos.y_, 0, rtSize.y_ - 1);
        intRect.right_ = Clamp(rect.right_ + viewPos.x_, 0, rtSize.x_);
        intRect.bottom_ = Clamp(rect.bottom_ + viewPos.y_, 0, rtSize.y_);
        
        if (intRect.right_ == intRect.left_)
            intRect.right_++;
        if (intRect.bottom_ == intRect.top_)
            intRect.bottom_++;
        
        if (intRect.right_ < intRect.left_ || intRect.bottom_ < intRect.top_)
            enable = false;
        
        if (enable && scissorRect_ != intRect)
        {
            RECT d3dRect;
            d3dRect.left = intRect.left_;
            d3dRect.top = intRect.top_;
            d3dRect.right = intRect.right_;
            d3dRect.bottom = intRect.bottom_;
            
            impl_->device_->SetScissorRect(&d3dRect);
            scissorRect_ = intRect;
        }
    }
    else
        scissorRect_ = IntRect::ZERO;
    
    if (enable != scissorTest_)
    {
        impl_->device_->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE);
        scissorTest_ = enable;
    }
}

void Graphics::SetStencilTest(bool enable, CompareMode mode, StencilOp pass, StencilOp fail, StencilOp zFail, unsigned stencilRef, unsigned compareMask, unsigned writeMask)
{
    if (enable != stencilTest_)
    {
        impl_->device_->SetRenderState(D3DRS_STENCILENABLE, enable ? TRUE : FALSE);
        stencilTest_ = enable;
    }
    
    if (enable)
    {
        if (mode != stencilTestMode_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILFUNC, d3dCmpFunc[mode]);
            stencilTestMode_ = mode;
        }
        if (pass != stencilPass_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILPASS, d3dStencilOp[pass]);
            stencilPass_ = pass;
        }
        if (fail != stencilFail_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILFAIL, d3dStencilOp[fail]);
            stencilFail_ = fail;
        }
        if (zFail != stencilZFail_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILZFAIL, d3dStencilOp[zFail]);
            stencilZFail_ = zFail;
        }
        if (stencilRef != stencilRef_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILREF, stencilRef);
            stencilRef_ = stencilRef;
        }
        if (compareMask != stencilCompareMask_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILMASK, compareMask);
            stencilCompareMask_ = compareMask;
        }
        if (writeMask != stencilWriteMask_)
        {
            impl_->device_->SetRenderState(D3DRS_STENCILWRITEMASK, writeMask);
            stencilWriteMask_ = writeMask;
        }
    }
}

void Graphics::SetStreamFrequency(unsigned index, unsigned frequency)
{
    if (index < MAX_VERTEX_STREAMS && streamFrequencies_[index] != frequency)
    {
        impl_->device_->SetStreamSourceFreq(index, frequency);
        streamFrequencies_[index] = frequency;
    }
}

void Graphics::ResetStreamFrequencies()
{
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
    {
        if (streamFrequencies_[i] != 1)
        {
            impl_->device_->SetStreamSourceFreq(i, 1);
            streamFrequencies_[i] = 1;
        }
    }
}

void Graphics::SetForceSM2(bool enable)
{
    if (!IsInitialized())
        forceSM2_ = enable;
    else
        LOGERROR("Force Shader Model 2 can not be changed after setting the initial screen mode");
}

bool Graphics::IsInitialized() const
{
    return impl_->window_ != 0 && impl_->GetDevice() != 0;
}

PODVector<IntVector2> Graphics::GetResolutions() const
{
    PODVector<IntVector2> ret;
    unsigned numModes = SDL_GetNumDisplayModes(0);
    
    for (unsigned i = 0; i < numModes; ++i)
    {
        SDL_DisplayMode mode;
        SDL_GetDisplayMode(0, i, &mode);
        int width = mode.w;
        int height  = mode.h;
        
        // Store mode if unique
        bool unique = true;
        for (unsigned j = 0; j < ret.Size(); ++j)
        {
            if (ret[j].x_ == width && ret[j].y_ == height)
            {
                unique = false;
                break;
            }
        }
        
        if (unique)
            ret.Push(IntVector2(width, height));
    }
    
    return ret;
}

PODVector<int> Graphics::GetMultiSampleLevels() const
{
    PODVector<int> ret;
    // No multisampling always supported
    ret.Push(1);
    
    if (!impl_->interface_)
        return ret;
    
    SDL_DisplayMode mode;
    SDL_GetDesktopDisplayMode(0, &mode);
    D3DFORMAT fullscreenFormat = SDL_BITSPERPIXEL(mode.format) == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
    
    for (unsigned i = (int)D3DMULTISAMPLE_2_SAMPLES; i < (int)D3DMULTISAMPLE_16_SAMPLES; ++i)
    {
        if (SUCCEEDED(impl_->interface_->CheckDeviceMultiSampleType(impl_->adapter_, impl_->deviceType_, fullscreenFormat, FALSE,
            (D3DMULTISAMPLE_TYPE)i, NULL)))
            ret.Push(i);
    }
    
    return ret;
}

unsigned Graphics::GetFormat(CompressedFormat format) const
{
    switch (format)
    {
    case CF_DXT1:
        return D3DFMT_DXT1;
        
    case CF_DXT3:
        return D3DFMT_DXT3;
        
    case CF_DXT5:
        return D3DFMT_DXT5;
    }
    
    return 0;
}

VertexBuffer* Graphics::GetVertexBuffer(unsigned index) const
{
    return index < MAX_VERTEX_STREAMS ? vertexBuffers_[index] : 0;
}

TextureUnit Graphics::GetTextureUnit(const String& name)
{
    HashMap<String, TextureUnit>::Iterator i = textureUnits_.Find(name);
    if (i != textureUnits_.End())
        return i->second_;
    else
        return MAX_TEXTURE_UNITS;
}

Texture* Graphics::GetTexture(unsigned index) const
{
    return index < MAX_TEXTURE_UNITS ? textures_[index] : 0;
}

RenderSurface* Graphics::GetRenderTarget(unsigned index) const
{
    return index < MAX_RENDERTARGETS ? renderTargets_[index] : 0;
}

unsigned Graphics::GetStreamFrequency(unsigned index) const
{
    return index < MAX_VERTEX_STREAMS ? streamFrequencies_[index] : 0;
}

IntVector2 Graphics::GetRenderTargetDimensions() const
{
    int width, height;
    
    if (renderTargets_[0])
    {
        width = renderTargets_[0]->GetWidth();
        height = renderTargets_[0]->GetHeight();
    }
    else
    {
        width = width_;
        height = height_;
    }
    
    return IntVector2(width, height);
}

void Graphics::WindowResized(int width, int height)
{
    if (!impl_->device_ || (width == width_ && height == height_))
        return;
    
    width_ = width;
    height_ = height;
    
    impl_->presentParams_.BackBufferWidth            = width;
    impl_->presentParams_.BackBufferHeight           = height;
    ResetDevice();
    
    // Reset rendertargets and viewport for the new screen size
    ResetRenderTargets();
    
    LOGDEBUG(ToString("Window was resized to %dx%d", width_, height_));
    
    using namespace ScreenMode;
    
    VariantMap eventData;
    eventData[P_WIDTH] = width_;
    eventData[P_HEIGHT] = height_;
    eventData[P_FULLSCREEN] = fullscreen_;
    eventData[P_RESIZABLE] = resizable_;
    SendEvent(E_SCREENMODE, eventData);
}

void Graphics::AddGPUObject(GPUObject* object)
{
    gpuObjects_.Push(object);
}

void Graphics::RemoveGPUObject(GPUObject* object)
{
    gpuObjects_.Remove(object);
}

void* Graphics::ReserveScratchBuffer(unsigned size)
{
    if (!size)
        return 0;
    
    if (size > maxScratchBufferRequest_)
        maxScratchBufferRequest_ = size;
    
    // First check for a free buffer that is large enough
    for (Vector<ScratchBuffer>::Iterator i = scratchBuffers_.Begin(); i != scratchBuffers_.End(); ++i)
    {
        if (!i->reserved_ && i->size_ >= size)
        {
            i->reserved_ = true;
            return i->data_.Get();
        }
    }
    
    // Then check if a free buffer can be resized
    for (Vector<ScratchBuffer>::Iterator i = scratchBuffers_.Begin(); i != scratchBuffers_.End(); ++i)
    {
        if (!i->reserved_)
        {
            i->data_ = new unsigned char[size];
            i->size_ = size;
            i->reserved_ = true;
            
            LOGDEBUG("Resized scratch buffer to size " + String(size));
            
            return i->data_.Get();
        }
    }
    
    // Finally allocate a new buffer
    ScratchBuffer newBuffer;
    newBuffer.data_ = new unsigned char[size];
    newBuffer.size_ = size;
    newBuffer.reserved_ = true;
    scratchBuffers_.Push(newBuffer);
    return newBuffer.data_.Get();
    
    LOGDEBUG("Allocated scratch buffer with size " + String(size));
}

void Graphics::FreeScratchBuffer(void* buffer)
{
    if (!buffer)
        return;
    
    for (Vector<ScratchBuffer>::Iterator i = scratchBuffers_.Begin(); i != scratchBuffers_.End(); ++i)
    {
        if (i->reserved_ && i->data_.Get() == buffer)
        {
            i->reserved_ = false;
            return;
        }
    }

    LOGWARNING("Reserved scratch buffer " + ToStringHex((unsigned)buffer) + " not found");
}

void Graphics::CleanupScratchBuffers()
{
    for (Vector<ScratchBuffer>::Iterator i = scratchBuffers_.Begin(); i != scratchBuffers_.End(); ++i)
    {
        if (!i->reserved_ && i->size_ > maxScratchBufferRequest_ * 2)
        {
            i->data_ = maxScratchBufferRequest_ > 0 ? new unsigned char[maxScratchBufferRequest_] : 0;
            i->size_ = maxScratchBufferRequest_;
            
            LOGDEBUG("Resized scratch buffer to size " + String(maxScratchBufferRequest_));
        }
    }
    
    maxScratchBufferRequest_ = 0;
}

unsigned Graphics::GetAlphaFormat()
{
    return D3DFMT_A8;
}

unsigned Graphics::GetLuminanceFormat()
{
    return D3DFMT_L8;
}

unsigned Graphics::GetLuminanceAlphaFormat()
{
    return D3DFMT_A8L8;
}

unsigned Graphics::GetRGBFormat()
{
    return D3DFMT_X8R8G8B8;
}

unsigned Graphics::GetRGBAFormat()
{
    return D3DFMT_A8R8G8B8;
}

unsigned Graphics::GetRGBA16Format()
{
    return D3DFMT_A16B16G16R16;
}

unsigned Graphics::GetRGBAFloat16Format()
{
    return D3DFMT_A16B16G16R16F;
}

unsigned Graphics::GetRGBAFloat32Format()
{
    return D3DFMT_A32B32G32R32F;
}

unsigned Graphics::GetRG16Format()
{
    return D3DFMT_G16R16;
}

unsigned Graphics::GetRGFloat16Format()
{
    return D3DFMT_G16R16F;
}

unsigned Graphics::GetRGFloat32Format()
{
    return D3DFMT_G32R32F;
}

unsigned Graphics::GetFloat16Format()
{
    return D3DFMT_R16F;
}

unsigned Graphics::GetFloat32Format()
{
    return D3DFMT_R32F;
}

unsigned Graphics::GetLinearDepthFormat()
{
    return D3DFMT_R32F;
}

unsigned Graphics::GetDepthStencilFormat()
{
    return depthStencilFormat;
}

unsigned Graphics::GetFormat(const String& formatName)
{
    String nameLower = formatName.ToLower().Trimmed();
    
    if (nameLower == "a")
        return GetAlphaFormat();
    if (nameLower == "l")
        return GetLuminanceFormat();
    if (nameLower == "la")
        return GetLuminanceAlphaFormat();
    if (nameLower == "rgb")
        return GetRGBFormat();
    if (nameLower == "rgba")
        return GetRGBAFormat();
    if (nameLower == "rgba16")
        return GetRGBA16Format();
    if (nameLower == "rgba16f")
        return GetRGBAFloat16Format();
    if (nameLower == "rgba32f")
        return GetRGBAFloat32Format();
    if (nameLower == "rg16")
        return GetRG16Format();
    if (nameLower == "rg16f")
        return GetRGFloat16Format();
    if (nameLower == "rg32f")
        return GetRGFloat32Format();
    if (nameLower == "r16f")
        return GetFloat16Format();
    if (nameLower == "r32f" || nameLower == "float")
        return GetFloat32Format();
    if (nameLower == "lineardepth" || nameLower == "depth")
        return GetLinearDepthFormat();
    if (nameLower == "d24s8")
        return GetDepthStencilFormat();
    
    return GetRGBFormat();
}

bool Graphics::OpenWindow(int width, int height, bool resizable)
{
    if (!externalWindow_)
        impl_->window_ = SDL_CreateWindow(windowTitle_.CString(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, resizable ? SDL_WINDOW_RESIZABLE : 0);
    else
        impl_->window_ = SDL_CreateWindowFrom(externalWindow_, 0);
    
    if (!impl_->window_)
    {
        LOGERROR("Could not create window");
        return false;
    }
    
    return true;
}

void Graphics::AdjustWindow(int& newWidth, int& newHeight, bool& newFullscreen)
{
    if (!externalWindow_)
    {
        SDL_SetWindowSize(impl_->window_, newWidth, newHeight);
        SDL_SetWindowFullscreen(impl_->window_, newFullscreen ? SDL_TRUE : SDL_FALSE);
    }
    else
    {
        // If external window, must ask its dimensions instead of trying to set them
        SDL_GetWindowSize(impl_->window_, &newWidth, &newHeight);
        newFullscreen = false;
    }
}

bool Graphics::CreateInterface()
{
    impl_->interface_ = Direct3DCreate9(D3D9b_SDK_VERSION);
    if (!impl_->interface_)
    {
        LOGERROR("Could not create Direct3D9 interface");
        return false;
    }
    
    if (FAILED(impl_->interface_->GetDeviceCaps(impl_->adapter_, impl_->deviceType_, &impl_->deviceCaps_)))
    {
        LOGERROR("Could not get Direct3D capabilities");
        return false;
    }
    
    if (FAILED(impl_->interface_->GetAdapterIdentifier(impl_->adapter_, 0, &impl_->adapterIdentifier_)))
    {
        LOGERROR("Could not get Direct3D adapter identifier");
        return false;
    }
    
    if (impl_->deviceCaps_.PixelShaderVersion < D3DPS_VERSION(2, 0))
    {
        LOGERROR("Shader model 2.0 display adapter is required");
        return false;
    }
    
    return true;
}

bool Graphics::CreateDevice(unsigned adapter, unsigned deviceType)
{
    DWORD behaviorFlags = 0;
    if (impl_->deviceCaps_.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
    {
        behaviorFlags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
        if (impl_->deviceCaps_.DevCaps & D3DDEVCAPS_PUREDEVICE)
            behaviorFlags |= D3DCREATE_PUREDEVICE;
    }
    else
        behaviorFlags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    
    if (FAILED(impl_->interface_->CreateDevice(
        adapter,
        (D3DDEVTYPE)deviceType,
        WIN_GetWindowHandle(impl_->window_),
        behaviorFlags,
        &impl_->presentParams_,
        &impl_->device_)))
    {
        LOGERROR("Could not create Direct3D9 device");
        return false;
    }
    
    impl_->adapter_ = adapter;
    impl_->deviceType_ = (D3DDEVTYPE)deviceType;
    
    OnDeviceReset();
    
    LOGINFO("Created Direct3D9 device");
    return true;
}

void Graphics::CheckFeatureSupport()
{
    // Reset features first
    lightPrepassSupport_ = false;
    deferredSupport_ = false;
    hardwareShadowSupport_ = false;
    streamOffsetSupport_ = false;
    hasSM3_ = false;
    depthStencilFormat = D3DFMT_D24S8;
    
    // Check hardware shadow map support: prefer NVIDIA style hardware depth compared shadow maps if available
    shadowMapFormat_ = D3DFMT_D16;
    if (impl_->CheckFormatSupport((D3DFORMAT)shadowMapFormat_, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE))
    {
        hardwareShadowSupport_ = true;
    
        // Check for hires depth support
        hiresShadowMapFormat_ = D3DFMT_D24X8;
        if (!impl_->CheckFormatSupport((D3DFORMAT)hiresShadowMapFormat_, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE))
            hiresShadowMapFormat_ = 0;
    }
    else
    {
        // ATI DF16 format needs manual depth compare in the shader
        shadowMapFormat_ = MAKEFOURCC('D', 'F', '1', '6');
        if (impl_->CheckFormatSupport((D3DFORMAT)shadowMapFormat_, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE))
        {
            // Check for hires depth support
            hiresShadowMapFormat_ = MAKEFOURCC('D', 'F', '2', '4');
            if (!impl_->CheckFormatSupport((D3DFORMAT)hiresShadowMapFormat_, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE))
                hiresShadowMapFormat_ = 0;
        }
        else
        {
            // No shadow map support
            shadowMapFormat_ = 0;
            hiresShadowMapFormat_ = 0;
        }
    }
    
   // Check for Intel 4 Series with an old driver, enable manual shadow map compare in that case
    if (shadowMapFormat_ == D3DFMT_D16)
    {
        if (impl_->adapterIdentifier_.VendorId == 0x8086 && impl_->adapterIdentifier_.DeviceId == 0x2a42 &&
            impl_->adapterIdentifier_.DriverVersion.QuadPart <= 0x0007000f000a05d0ULL)
            hardwareShadowSupport_ = false;
    }
    
    // Check for dummy color rendertarget format used with hardware shadow maps
    dummyColorFormat_ = D3DFMT_A8R8G8B8;
    D3DFORMAT nullFormat = (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L');
    if (impl_->CheckFormatSupport(nullFormat, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE))
        dummyColorFormat_ = nullFormat;
    else if (impl_->CheckFormatSupport(D3DFMT_R16F, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE))
        dummyColorFormat_ = D3DFMT_R16F;
    else if (impl_->CheckFormatSupport(D3DFMT_R5G6B5, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE))
        dummyColorFormat_ = D3DFMT_R5G6B5;
    else if (impl_->CheckFormatSupport(D3DFMT_A4R4G4B4, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE))
        dummyColorFormat_ = D3DFMT_A4R4G4B4;
        
    // Check for Shader Model 3
    if (!forceSM2_)
    {
        if (impl_->deviceCaps_.VertexShaderVersion >= D3DVS_VERSION(3, 0) && impl_->deviceCaps_.PixelShaderVersion >=
            D3DPS_VERSION(3, 0))
            hasSM3_ = true;
    }
    
    // Check for light prepass and deferred rendering support
    if (impl_->deviceCaps_.NumSimultaneousRTs >= 2 && impl_->CheckFormatSupport(D3DFMT_R32F, D3DUSAGE_RENDERTARGET,
        D3DRTYPE_TEXTURE))
    {
        lightPrepassSupport_ = true;
        if (impl_->deviceCaps_.NumSimultaneousRTs >= 4)
            deferredSupport_ = true;
    }
    
    // Check for stream offset (needed for instancing)
    if (impl_->deviceCaps_.DevCaps2 & D3DDEVCAPS2_STREAMOFFSET)
        streamOffsetSupport_ = true;
    
    SendEvent(E_GRAPHICSFEATURES);
}

void Graphics::ResetDevice()
{
    OnDeviceLost();
    
    if (SUCCEEDED(impl_->device_->Reset(&impl_->presentParams_)))
    {
        deviceLost_ = false;
        OnDeviceReset();
    }
}

void Graphics::OnDeviceLost()
{
    LOGINFO("Device lost");
    
    if (impl_->defaultColorSurface_)
    {
        impl_->defaultColorSurface_->Release();
        impl_->defaultColorSurface_ = 0;
    }
    if (impl_->defaultDepthStencilSurface_)
    {
        impl_->defaultDepthStencilSurface_->Release();
        impl_->defaultDepthStencilSurface_ = 0;
    }
    
    for (unsigned i = 0; i < gpuObjects_.Size(); ++i)
        gpuObjects_[i]->OnDeviceLost();
}

void Graphics::OnDeviceReset()
{
    for (unsigned i = 0; i < gpuObjects_.Size(); ++i)
        gpuObjects_[i]->OnDeviceReset();
    
    // Get default surfaces
    impl_->device_->GetRenderTarget(0, &impl_->defaultColorSurface_);
    impl_->device_->GetDepthStencilSurface(&impl_->defaultDepthStencilSurface_);
    
    ResetCachedState();
}

void Graphics::ResetCachedState()
{
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
    {
        vertexBuffers_[i] = 0;
        streamOffsets_[i] = 0;
    }
    
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        textures_[i] = 0;
        impl_->minMagFilters_[i] = D3DTEXF_POINT;
        impl_->mipFilters_[i] = D3DTEXF_NONE;
        impl_->uAddressModes_[i] = D3DTADDRESS_WRAP;
        impl_->vAddressModes_[i] = D3DTADDRESS_WRAP;
        impl_->wAddressModes_[i] = D3DTADDRESS_WRAP;
        impl_->borderColors_[i] = Color(0.0f, 0.0f, 0.0f, 0.0f);
        impl_->sRGBModes_[i] = false;
    }
    
    for (unsigned i = 0; i < MAX_RENDERTARGETS; ++i)
    {
        renderTargets_[i] = 0;
        impl_->colorSurfaces_[i] = 0;
    }
    
    depthStencil_ = 0;
    impl_->depthStencilSurface_ = 0;
    viewTexture_ = 0;
    viewport_ = IntRect(0, 0, width_, height_);
    impl_->sRGBWrite_ = false;
    
    for (unsigned i = 0; i < MAX_VERTEX_STREAMS; ++i)
        streamFrequencies_[i] = 1;
    
    indexBuffer_ = 0;
    vertexDeclaration_ = 0;
    vertexShader_ = 0;
    pixelShader_ = 0;
    blendMode_ = BLEND_REPLACE;
    textureAnisotropy_ = 1;
    colorWrite_ = true;
    cullMode_ = CULL_CCW;
    constantDepthBias_ = 0.0f;
    slopeScaledDepthBias_ = 0.0f;
    depthTestMode_ = CMP_LESSEQUAL;
    depthWrite_ = true;
    fillMode_ = FILL_SOLID;
    scissorTest_ = false;
    scissorRect_ = IntRect::ZERO;
    stencilTest_ = false;
    stencilTestMode_ = CMP_ALWAYS;
    stencilPass_ = OP_KEEP;
    stencilFail_ = OP_KEEP;
    stencilZFail_ = OP_KEEP;
    stencilRef_ = 0;
    stencilCompareMask_ = M_MAX_UNSIGNED;
    stencilWriteMask_ = M_MAX_UNSIGNED;
    impl_->blendEnable_ = FALSE;
    impl_->srcBlend_ = D3DBLEND_ONE;
    impl_->destBlend_ = D3DBLEND_ZERO;
}

void Graphics::SetTextureUnitMappings()
{
    textureUnits_["DiffMap"] = TU_DIFFUSE;
    textureUnits_["DiffCubeMap"] = TU_DIFFUSE;
    textureUnits_["NormalMap"] = TU_NORMAL;
    textureUnits_["SpecMap"] = TU_SPECULAR;
    textureUnits_["EmissiveMap"] = TU_EMISSIVE;
    textureUnits_["EnvMap"] = TU_ENVIRONMENT;
    textureUnits_["EnvCubeMap"] = TU_ENVIRONMENT;
    textureUnits_["LightRampMap"] = TU_LIGHTRAMP;
    textureUnits_["LightSpotMap"] = TU_LIGHTSHAPE;
    textureUnits_["LightCubeMap"]  = TU_LIGHTSHAPE;
    textureUnits_["ShadowMap"] = TU_SHADOWMAP;
    textureUnits_["FaceSelectCubeMap"] = TU_FACESELECT;
    textureUnits_["IndirectionCubeMap"] = TU_INDIRECTION;
}

void RegisterGraphicsLibrary(Context* context)
{
    Animation::RegisterObject(context);
    Material::RegisterObject(context);
    Model::RegisterObject(context);
    Shader::RegisterObject(context);
    Technique::RegisterObject(context);
    Texture2D::RegisterObject(context);
    TextureCube::RegisterObject(context);
    Camera::RegisterObject(context);
    Drawable::RegisterObject(context);
    Light::RegisterObject(context);
    StaticModel::RegisterObject(context);
    Skybox::RegisterObject(context);
    AnimatedModel::RegisterObject(context);
    AnimationController::RegisterObject(context);
    BillboardSet::RegisterObject(context);
    ParticleEmitter::RegisterObject(context);
    CustomGeometry::RegisterObject(context);
    DecalSet::RegisterObject(context);
    Terrain::RegisterObject(context);
    TerrainPatch::RegisterObject(context);
    DebugRenderer::RegisterObject(context);
    Octree::RegisterObject(context);
    Zone::RegisterObject(context);
}

}
