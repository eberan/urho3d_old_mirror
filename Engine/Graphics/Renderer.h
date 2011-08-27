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

#include "Batch.h"
#include "Color.h"
#include "Drawable.h"
#include "HashMap.h"
#include "HashSet.h"
#include "RenderSurface.h"

class DebugRenderer;
class Geometry;
class Drawable;
class Light;
class Material;
class Pass;
class Technique;
class Octree;
class Graphics;
class ResourceCache;
class Skeleton;
class OcclusionBuffer;
class Texture2D;
class View;
class Zone;

static const int SHADOW_MIN_PIXELS = 64;
static const int NUM_SHADOWMAP_RESOLUTIONS = 3;
static const int MIN_INSTANCES = 4;
static const int INSTANCING_BUFFER_DEFAULT_SIZE = 1024;

/// Light vertex shader variations.
enum LightVSVariation
{
    LVS_NONE = 0,
    LVS_SPOT,
    LVS_SHADOW,
    LVS_SPOTSHADOW,
    MAX_LIGHT_VS_VARIATIONS
};

/// Forward rendering light pixel shader variations.
enum LightPSVariation
{
    LPS_NONE = 0,
    LPS_SPEC,
    LPS_SPOT,
    LPS_SPOTSPEC,
    LPS_POINT,
    LPS_POINTSPEC,
    LPS_POINTMASK,
    LPS_POINTMASKSPEC,
    MAX_LIGHT_PS_VARIATIONS
};

/// Deferred light volume vertex shader variations.
enum DeferredLightVSVariation
{
    DLVS_NONE = 0,
    DLVS_DIR,
    DLVS_ORTHO,
    DLVS_ORTHODIR,
    MAX_DEFERRED_LIGHT_VS_VARIATIONS
};

/// Deferred light volume pixels shader variations.
enum DeferredLightPSVariation
{
    DLPS_NONE = 0,
    DLPS_SPEC,
    DLPS_SPOT,
    DLPS_SPOTSPEC,
    DLPS_POINT,
    DLPS_POINTSPEC,
    DLPS_POINTMASK,
    DLPS_POINTMASKSPEC,
    DLPS_ORTHO,
    DLPS_ORTHOSPEC,
    DLPS_ORTHOSPOT,
    DLPS_ORTHOSPOTSPEC,
    DLPS_ORTHOPOINT,
    DLPS_ORTHOPOINTSPEC,
    DLPS_ORTHOPOINTMASK,
    DLPS_ORTHOPOINTMASKSPEC,
    DLPS_HW,
    DLPS_HWSPEC,
    DLPS_HWSPOT,
    DLPS_HWSPOTSPEC,
    DLPS_HWPOINT,
    DLPS_HWPOINTSPEC,
    DLPS_HWPOINTMASK,
    DLPS_HWPOINTMASKSPEC,
    DLPS_SHADOW,
    DLPS_SHADOWSPEC,
    DLPS_SPOTSHADOW,
    DLPS_SPOTSHADOWSPEC,
    DLPS_POINTSHADOW,
    DLPS_POINTSHADOWSPEC,
    DLPS_POINTMASKSHADOW,
    DLPS_POINTMASKSHADOWSPEC,
    DLPS_ORTHOSHADOW,
    DLPS_ORTHOSHADOWSPEC,
    DLPS_ORTHOSPOTSHADOW,
    DLPS_ORTHOSPOTSHADOWSPEC,
    DLPS_ORTHOPOINTSHADOW,
    DLPS_ORTHOPOINTSHADOWSPEC,
    DLPS_ORTHOPOINTMASKSHADOW,
    DLPS_ORTHOPOINTMASKSHADOWSPEC,
    DLPS_HWSHADOW,
    DLPS_HWSHADOWSPEC,
    DLPS_HWSPOTSHADOW,
    DLPS_HWSPOTSHADOWSPEC,
    DLPS_HWPOINTSHADOW,
    DLPS_HWPOINTSHADOWSPEC,
    DLPS_HWPOINTMASKSHADOW,
    DLPS_HWPOINTMASKSHADOWSPEC,
    MAX_DEFERRED_LIGHT_PS_VARIATIONS
};

/// Deferred rendering edge filter parameters.
struct EdgeFilterParameters
{
    /// Construct undefined.
    EdgeFilterParameters()
    {
    }
    
    /// Construct with initial values.
    EdgeFilterParameters(float radius, float threshold, float strength) :
        radius_(radius),
        threshold_(threshold),
        strength_(strength)
    {
    }
    
    //! Validate parameters.
    void Validate();
    
    //! Radius for calculating luminance gradient.
    float radius_;
    //! Luminance difference threshold needed to pass pixel.
    float threshold_;
    //! Filter strength.
    float strength_;
};

/// High-level rendering subsystem. Manages drawing of 3D views.
class Renderer : public Object
{
    OBJECT(Object);
    
    friend class View;
    
public:
    /// Construct.
    Renderer(Context* context);
    /// Destruct.
    virtual ~Renderer();
    
    /// %Set number of viewports to render.
    void SetNumViewports(unsigned num);
    /// %Set a viewport.
    void SetViewport(unsigned index, const Viewport& viewport);
    /// %Set specular lighting on/off.
    void SetSpecularLighting(bool enable);
    /// %Set shadows on/off.
    void SetDrawShadows(bool enable);
    /// %Set texture anisotropy.
    void SetTextureAnisotropy(int level);
    /// %Set texture filtering.
    void SetTextureFilterMode(TextureFilterMode mode);
    /// %Set texture quality level.
    void SetTextureQuality(int quality);
    /// %Set material quality level.
    void SetMaterialQuality(int quality);
    /// %Set shadow map resolution.
    void SetShadowMapSize(int size);
    /// %Set shadow map 24-bit depth on/off.
    void SetShadowMapHiresDepth(bool enable);
    /// %Set dynamic instancing on/off.
    void SetDynamicInstancing(bool enable);
    /// %Set deferred rendering edge filter parameters.
    void SetEdgeFilter(const EdgeFilterParameters& parameters);
    /// %Set maximum number of occluder trianges.
    void SetMaxOccluderTriangles(int triangles);
    /// %Set occluder buffer width.
    void SetOcclusionBufferSize(int size);
    /// %Set required size (1.0 = full screen) for occluders.
    void SetOccluderSizeThreshold(float screenSize);
    /// Return number of viewports.
    unsigned GetNumViewports() const { return viewports_.Size(); }
    /// Return viewport.
    const Viewport& GetViewport(unsigned index) const;
    /// Return whether is using the fallback lighting model.
    bool IsFallback() const { return fallback_; }
    /// Return whether specular lighting is enabled.
    bool GetSpecularLighting() const { return specularLighting_; }
    /// Return whether drawing shadows is enabled.
    bool GetDrawShadows() const { return drawShadows_; }
    /// Return texture anisotropy.
    int GetTextureAnisotropy() const { return textureAnisotropy_; }
    /// Return texture filtering.
    TextureFilterMode GetTextureFilterMode() const { return textureFilterMode_; }
    /// Return texture quality level.
    int GetTextureQuality() const { return textureQuality_; }
    /// Return material quality level.
    int GetMaterialQuality() const { return materialQuality_; }
    /// Return shadow map resolution.
    int GetShadowMapSize() const { return shadowMapSize_; }
    /// Return whether shadow maps use 24-bit depth.
    bool GetShadowMapHiresDepth() const { return shadowMapHiresDepth_; }
    /// Return whether dynamic instancing is in use.
    bool GetDynamicInstancing() const { return dynamicInstancing_; }
    /// Return deferred rendering edge filter parameters.
    const EdgeFilterParameters& GetEdgeFilter() const { return edgeFilter_; }
    /// Return maximum number of occluder triangles.
    int GetMaxOccluderTriangles() const { return maxOccluderTriangles_; }
    /// Return occlusion buffer width.
    int GetOcclusionBufferSize() const { return occlusionBufferSize_; }
    /// Return occluder screen size threshold.
    float GetOccluderSizeThreshold() const { return occluderSizeThreshold_; }
    /// Return number of views rendered.
    unsigned GetNumViews() const { return numViews_; }
    /// Return number of primitives rendered.
    unsigned GetNumPrimitives() const { return numPrimitives_; }
    /// Return number of batches rendered.
    unsigned GetNumBatches() const { return numBatches_; }
    /// Return number of geometries rendered.
    unsigned GetNumGeometries(bool allViews = false) const;
    /// Return number of lights rendered.
    unsigned GetNumLights(bool allViews = false) const;
    /// Return number of shadow maps rendered.
    unsigned GetNumShadowMaps(bool allViews = false) const;
    /// Return number of occluders rendered.
    unsigned GetNumOccluders(bool allViews = false) const;
    /// Return number of directional light shadow occluders rendered.
    unsigned GetNumShadowOccluders(bool allViews = false) const;
    /// Return an occlusion buffer for inspection.
    const OcclusionBuffer* GetOcclusionBuffer(float aspectRatio, bool halfResolution = false);
    /// Return the default zone.
    Zone* GetDefaultZone() const { return defaultZone_; }
    /// Return the default material.
    Material* GetDefaultMaterial() const { return defaultMaterial_; }
    /// Return the default range attenuation texture.
    Texture2D* GetDefaultLightRamp() const { return defaultLightRamp_; }
    /// Return the default spotlight attenuation texture.
    Texture2D* GetDefaultLightSpot() const { return defaultLightSpot; }
    /// Return a vertex shader by name.
    ShaderVariation* GetVertexShader(const String& name, bool checkExists = false) const;
    /// Return a pixel shader by name.
    ShaderVariation* GetPixelShader(const String& name, bool checkExists = false) const;
    /// Return the frame update parameters.
    const FrameInfo& GetFrameInfo() { return frame_; }
    
    /// Update for rendering. Called by HandleRenderUpdate().
    void Update(float timeStep);
    /// Render. Called by Engine.
    void Render();
    /// Add debug geometry to the debug graphics(s).
    void DrawDebugGeometry(bool depthTest);
    
private:
    /// Initialize when screen mode initially set.
    void Initialize();
    /// Clear views from previous frame.
    void ResetViews();
    /// Add a view. Return true if successful.
    bool AddView(RenderSurface* renderTarget, const Viewport& viewport);
    /// Return an occlusion buffer for use.
    OcclusionBuffer* GetOrCreateOcclusionBuffer(Camera* camera, int maxOccluderTriangles, bool halfResolution = false);
    /// Return volume geometry for a light.
    Geometry* GetLightGeometry(Light* light);
    /// Return shadow map by resolution. If shadow map reuse is disabled, a different map is returned each time.
    Texture2D* GetShadowMap(float resolution);
    /// Get a shader program.
    ShaderVariation* GetShader(const String& name, const String& extension, bool checkExists) const;
    /// Choose forward lighting shaders for a batch.
    void SetBatchShaders(Batch& batch, Technique* technique, Pass* pass);
    /// Choose shaders for a deferred light volume batch.
    void SetLightVolumeShaders(Batch& batch);
    /// Reload shaders.
    void LoadShaders();
    /// Reload shaders for a material technique.
    void LoadMaterialShaders(Technique* technique);
    /// Reload shaders for a material pass.
    void LoadPassShaders(Technique* technique, PassType type);
    /// Release shaders used in materials.
    void ReleaseMaterialShaders();
    /// Reload textures.
    void ReloadTextures();
    /// Create light volume geometries.
    void CreateGeometries();
    /// Create instancing vertex buffer.
    void CreateInstancingBuffer();
    /// Ensure sufficient size of the instancing vertex buffer. Return true if successful.
    bool ResizeInstancingBuffer(unsigned numInstances);
    /// Create shadow maps. Return true if successful.
    bool CreateShadowMaps();
    /// Split a light into several for shadow mapping.
    unsigned SplitLight(Light* light);
    /// Allocate a shadow camera and a scene node for it.
    Camera* CreateShadowCamera();
    /// Allocate (if necessary) and clone a light. Attach it to a temporary scene node.
    Light* CreateSplitLight(Light* original);
    /// Allocate a temporary scene node for attaching a split light or a shadow camera.
    Node* CreateTempNode();
    /// Handle screen mode event.
    void HandleScreenMode(StringHash eventType, VariantMap& eventData);
    /// Handle render update event.
    void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
    
    /// Graphics subsystem.
    WeakPtr<Graphics> graphics_;
    /// Resource cache subsystem.
    WeakPtr<ResourceCache> cache_;
    /// Default zone.
    SharedPtr<Zone> defaultZone_;
    /// Directional light geometry.
    SharedPtr<Geometry> dirLightGeometry_;
    /// Point light volume geometry.
    SharedPtr<Geometry> pointLightGeometry_;
    /// Spot light volume geometry.
    SharedPtr<Geometry> spotLightGeometry_;
    /// Instance stream vertex buffer.
    SharedPtr<VertexBuffer> instancingBuffer_;
    /// Default material.
    SharedPtr<Material> defaultMaterial_;
    /// Default range attenuation texture.
    SharedPtr<Texture2D> defaultLightRamp_;
    /// Default spotlight attenuation texture.
    SharedPtr<Texture2D> defaultLightSpot;
    /// Shadow maps by resolution.
    SharedPtr<Texture2D> shadowMaps_[NUM_SHADOWMAP_RESOLUTIONS];
    /// Shadow map dummy color textures by resolution.
    SharedPtr<Texture2D> colorShadowMaps_[NUM_SHADOWMAP_RESOLUTIONS];
    /// Stencil rendering vertex shader.
    SharedPtr<ShaderVariation> stencilVS_;
    /// Stencil rendering pixel shader.
    SharedPtr<ShaderVariation> stencilPS_;
    /// Light vertex shaders.
    Vector<SharedPtr<ShaderVariation> > lightVS_;
    /// Light pixel shaders.
    Vector<SharedPtr<ShaderVariation> > lightPS_;
    /// Reusable shadow cameras.
    Vector<SharedPtr<Camera> > shadowCameraStore_;
    /// Reusable split lights.
    Vector<SharedPtr<Light> > splitLightStore_;
    /// Reusable temporary scene nodes.
    Vector<SharedPtr<Node> > tempNodeStore_;
    /// Occlusion buffers.
    HashMap<int, SharedPtr<OcclusionBuffer> > occlusionBuffers_;
    /// Viewports.
    Vector<Viewport> viewports_;
    /// Views.
    Vector<SharedPtr<View> > views_;
    /// Octrees that have been updated during the frame.
    HashSet<Octree*> updateOctrees_;
    /// Techniques for which missing shader error has been displayed.
    HashSet<Technique*> shaderErrorDisplayed_;
    /// Vertex shader format.
    String vsFormat_;
    /// Pixel shader format.
    String psFormat_;
    /// Base directory for shaders.
    String shaderPath_;
    /// Number of views.
    unsigned numViews_;
    /// Number of shadow cameras.
    unsigned numShadowCameras_;
    /// Number of split lights.
    unsigned numSplitLights_;
    /// Number of temporary scene nodes.
    unsigned numTempNodes_;
    /// Number of primitives (3D geometry only.)
    unsigned numPrimitives_;
    /// Number of batches (3D geometry only.)
    unsigned numBatches_;
    /// Fallback lighting model flag.
    bool fallback_;
    /// Specular lighting flag.
    bool specularLighting_;
    /// Draw shadows flag.
    bool drawShadows_;
    /// Texture anisotropy level.
    int textureAnisotropy_;
    /// Texture filtering mode.
    TextureFilterMode textureFilterMode_;
    /// Texture quality level.
    int textureQuality_;
    /// Material quality level.
    int materialQuality_;
    /// Shadow map resolution.
    int shadowMapSize_;
    /// Shadow map 24-bit depth flag.
    bool shadowMapHiresDepth_;
    /// Shadow map reuse flag.
    bool reuseShadowMaps_;
    /// Dynamic instancing flag.
    bool dynamicInstancing_;
    /// Deferred rendering edge filter parameters.
    EdgeFilterParameters edgeFilter_;
    /// Maximum occluder triangles.
    int maxOccluderTriangles_;
    /// Occlusion buffer width.
    int occlusionBufferSize_;
    /// Occluder screen size threshold.
    float occluderSizeThreshold_;
    /// Frame number on which shaders last changed.
    unsigned shadersChangedFrameNumber_;
    /// Frame info for rendering.
    FrameInfo frame_;
    /// Shaders need reloading flag.
    bool shadersDirty_;
    /// Initialized flag.
    bool initialized_;
};
