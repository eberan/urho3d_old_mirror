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
#include "Camera.h"
#include "DebugRenderer.h"
#include "Geometry.h"
#include "Graphics.h"
#include "GraphicsImpl.h"
#include "Log.h"
#include "Material.h"
#include "OcclusionBuffer.h"
#include "Octree.h"
#include "Renderer.h"
#include "RenderPath.h"
#include "ResourceCache.h"
#include "Profiler.h"
#include "Scene.h"
#include "ShaderVariation.h"
#include "Skybox.h"
#include "Technique.h"
#include "Texture2D.h"
#include "TextureCube.h"
#include "VertexBuffer.h"
#include "View.h"
#include "WorkQueue.h"
#include "Zone.h"

#include "DebugNew.h"

namespace Urho3D
{

static const Vector3* directions[] =
{
    &Vector3::RIGHT,
    &Vector3::LEFT,
    &Vector3::UP,
    &Vector3::DOWN,
    &Vector3::FORWARD,
    &Vector3::BACK
};

static const int CHECK_DRAWABLES_PER_WORK_ITEM = 64;
static const float LIGHT_INTENSITY_THRESHOLD = 0.001f;

/// %Frustum octree query for shadowcasters.
class ShadowCasterOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    ShadowCasterOctreeQuery(PODVector<Drawable*>& result, const Frustum& frustum, unsigned char drawableFlags = DRAWABLE_ANY,
        unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
    {
    }
    
    /// Intersection test for drawables.
    virtual void TestDrawables(Drawable** start, Drawable** end, bool inside)
    {
        while (start != end)
        {
            Drawable* drawable = *start++;
            
            if (drawable->GetCastShadows() && (drawable->GetDrawableFlags() & drawableFlags_) &&
                (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.Push(drawable);
            }
        }
    }
};

/// %Frustum octree query for zones and occluders.
class ZoneOccluderOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum and query parameters.
    ZoneOccluderOctreeQuery(PODVector<Drawable*>& result, const Frustum& frustum, unsigned char drawableFlags = DRAWABLE_ANY,
        unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, drawableFlags, viewMask)
    {
    }
    
    /// Intersection test for drawables.
    virtual void TestDrawables(Drawable** start, Drawable** end, bool inside)
    {
        while (start != end)
        {
            Drawable* drawable = *start++;
            unsigned char flags = drawable->GetDrawableFlags();
            
            if ((flags == DRAWABLE_ZONE || (flags == DRAWABLE_GEOMETRY && drawable->IsOccluder())) && (drawable->GetViewMask() &
                viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.Push(drawable);
            }
        }
    }
};

/// %Frustum octree query with occlusion.
class OccludedFrustumOctreeQuery : public FrustumOctreeQuery
{
public:
    /// Construct with frustum, occlusion buffer and query parameters.
    OccludedFrustumOctreeQuery(PODVector<Drawable*>& result, const Frustum& frustum, OcclusionBuffer* buffer, unsigned char
        drawableFlags = DRAWABLE_ANY, unsigned viewMask = DEFAULT_VIEWMASK) :
        FrustumOctreeQuery(result, frustum, drawableFlags, viewMask),
        buffer_(buffer)
    {
    }
    
    /// Intersection test for an octant.
    virtual Intersection TestOctant(const BoundingBox& box, bool inside)
    {
        if (inside)
            return buffer_->IsVisible(box) ? INSIDE : OUTSIDE;
        else
        {
            Intersection result = frustum_.IsInside(box);
            if (result != OUTSIDE && !buffer_->IsVisible(box))
                result = OUTSIDE;
            return result;
        }
    }
    
    /// Intersection test for drawables. Note: drawable occlusion is performed later in worker threads.
    virtual void TestDrawables(Drawable** start, Drawable** end, bool inside)
    {
        while (start != end)
        {
            Drawable* drawable = *start++;
            
            if ((drawable->GetDrawableFlags() & drawableFlags_) && (drawable->GetViewMask() & viewMask_))
            {
                if (inside || frustum_.IsInsideFast(drawable->GetWorldBoundingBox()))
                    result_.Push(drawable);
            }
        }
    }
    
    /// Occlusion buffer.
    OcclusionBuffer* buffer_;
};

void CheckVisibilityWork(const WorkItem* item, unsigned threadIndex)
{
    View* view = reinterpret_cast<View*>(item->aux_);
    Drawable** start = reinterpret_cast<Drawable**>(item->start_);
    Drawable** end = reinterpret_cast<Drawable**>(item->end_);
    OcclusionBuffer* buffer = view->occlusionBuffer_;
    const Matrix3x4& viewMatrix = view->camera_->GetInverseWorldTransform();
    Vector3 viewZ = Vector3(viewMatrix.m20_, viewMatrix.m21_, viewMatrix.m22_);
    Vector3 absViewZ = viewZ.Abs();
    
    while (start != end)
    {
        Drawable* drawable = *start++;
        drawable->UpdateBatches(view->frame_);
        
        // If draw distance non-zero, check it
        float maxDistance = drawable->GetDrawDistance();
        if ((maxDistance <= 0.0f || drawable->GetDistance() <= maxDistance) && (!buffer || !drawable->IsOccludee() ||
            buffer->IsVisible(drawable->GetWorldBoundingBox())))
        {
            drawable->MarkInView(view->frame_);
            
            // For geometries, clear lights and calculate view space Z range
            if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
            {
                const BoundingBox& geomBox = drawable->GetWorldBoundingBox();
                Vector3 center = geomBox.Center();
                float viewCenterZ = viewZ.DotProduct(center) + viewMatrix.m23_;
                Vector3 edge = geomBox.Size() * 0.5f;
                float viewEdgeZ = absViewZ.DotProduct(edge);
                
                drawable->SetMinMaxZ(viewCenterZ - viewEdgeZ, viewCenterZ + viewEdgeZ);
                drawable->ClearLights();
            }
        }
    }
}

void ProcessLightWork(const WorkItem* item, unsigned threadIndex)
{
    View* view = reinterpret_cast<View*>(item->aux_);
    LightQueryResult* query = reinterpret_cast<LightQueryResult*>(item->start_);
    
    view->ProcessLight(*query, threadIndex);
}

void UpdateDrawableGeometriesWork(const WorkItem* item, unsigned threadIndex)
{
    const FrameInfo& frame = *(reinterpret_cast<FrameInfo*>(item->aux_));
    Drawable** start = reinterpret_cast<Drawable**>(item->start_);
    Drawable** end = reinterpret_cast<Drawable**>(item->end_);
    
    while (start != end)
    {
        Drawable* drawable = *start++;
        drawable->UpdateGeometry(frame);
    }
}

void SortBatchQueueFrontToBackWork(const WorkItem* item, unsigned threadIndex)
{
    BatchQueue* queue = reinterpret_cast<BatchQueue*>(item->start_);
    
    queue->SortFrontToBack();
}

void SortBatchQueueBackToFrontWork(const WorkItem* item, unsigned threadIndex)
{
    BatchQueue* queue = reinterpret_cast<BatchQueue*>(item->start_);
    
    queue->SortBackToFront();
}

void SortLightQueueWork(const WorkItem* item, unsigned threadIndex)
{
    LightBatchQueue* start = reinterpret_cast<LightBatchQueue*>(item->start_);
    start->litBatches_.SortFrontToBack();
}

void SortShadowQueueWork(const WorkItem* item, unsigned threadIndex)
{
    LightBatchQueue* start = reinterpret_cast<LightBatchQueue*>(item->start_);
    for (unsigned i = 0; i < start->shadowSplits_.Size(); ++i)
        start->shadowSplits_[i].shadowBatches_.SortFrontToBack();
}

OBJECTTYPESTATIC(View);

View::View(Context* context) :
    Object(context),
    graphics_(GetSubsystem<Graphics>()),
    renderer_(GetSubsystem<Renderer>()),
    scene_(0),
    octree_(0),
    camera_(0),
    cameraZone_(0),
    farClipZone_(0),
    renderTarget_(0),
    tempDrawables_(GetSubsystem<WorkQueue>()->GetNumThreads() + 1)  // Create octree query vector for each thread
{
    frame_.camera_ = 0;
}

View::~View()
{
}

bool View::Define(RenderSurface* renderTarget, Viewport* viewport)
{
    Scene* scene = viewport->GetScene();
    Camera* camera = viewport->GetCamera();
    if (!scene || !camera || !camera->IsEnabledEffective())
        return false;
    
    // If scene is loading asynchronously, it is incomplete and should not be rendered
    if (scene->IsAsyncLoading())
        return false;
    
    Octree* octree = scene->GetComponent<Octree>();
    if (!octree)
        return false;
    
    // Do not accept view if camera projection is illegal
    // (there is a possibility of crash if occlusion is used and it can not clip properly)
    if (!camera->IsProjectionValid())
        return false;
    
    scene_ = scene;
    octree_ = octree;
    camera_ = camera;
    cameraNode_ = camera->GetNode();
    renderTarget_ = renderTarget;
    renderPath_ = viewport->GetRenderPath();
    
    gBufferPassName_ = StringHash();
    basePassName_  = PASS_BASE;
    alphaPassName_ = PASS_ALPHA;
    lightPassName_ = PASS_LIGHT;
    litBasePassName_ = PASS_LITBASE;
    litAlphaPassName_ = PASS_LITALPHA;
    
    // Make sure that all necessary batch queues exist
    scenePasses_.Clear();
    for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
    {
        const RenderPathCommand& command = renderPath_->commands_[i];
        if (!command.enabled_)
            continue;
        
        if (command.type_ == CMD_SCENEPASS)
        {
            ScenePassInfo info;
            info.pass_ = command.pass_;
            info.allowInstancing_ = command.sortMode_ != SORT_BACKTOFRONT;
            info.markToStencil_ = command.markToStencil_;
            info.useScissor_ = command.useScissor_;
            info.vertexLights_ = command.vertexLights_;
            
            // Check scenepass metadata for defining custom passes which interact with lighting
            String metadata = command.metadata_.Trimmed().ToLower();
            if (!metadata.Empty())
            {
                if (metadata == "gbuffer")
                    gBufferPassName_ = command.pass_;
                else if (metadata == "base")
                {
                    basePassName_ = command.pass_;
                    litBasePassName_ = "lit" + command.pass_;
                }
                else if (metadata == "alpha")
                {
                    alphaPassName_ = command.pass_;
                    litAlphaPassName_ = "lit" + command.pass_;
                }
            }
            
            HashMap<StringHash, BatchQueue>::Iterator j = batchQueues_.Find(command.pass_);
            if (j == batchQueues_.End())
                j = batchQueues_.Insert(Pair<StringHash, BatchQueue>(command.pass_, BatchQueue()));
            info.batchQueue_ = &j->second_;
            
            scenePasses_.Push(info);
        }
        else if (command.type_ == CMD_FORWARDLIGHTS)
        {
            if (!command.pass_.Trimmed().Empty())
                lightPassName_ = command.pass_;
        }
    }
    
    // Get light volume shaders according to the renderpath, if it needs them
    deferred_ = false;
    for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
    {
        const RenderPathCommand& command = renderPath_->commands_[i];
        if (!command.enabled_)
            continue;
        
        if (command.type_ == CMD_LIGHTVOLUMES)
        {
            renderer_->GetLightVolumeShaders(lightVS_, lightPS_, command.vertexShaderName_, command.pixelShaderName_);
            deferred_ = true;
        }
    }
    if (!deferred_)
    {
        lightVS_.Clear();
        lightPS_.Clear();
    }
    
    // Validate the rect and calculate size. If zero rect, use whole rendertarget size
    int rtWidth = renderTarget ? renderTarget->GetWidth() : graphics_->GetWidth();
    int rtHeight = renderTarget ? renderTarget->GetHeight() : graphics_->GetHeight();
    const IntRect& rect = viewport->GetRect();
    
    if (rect != IntRect::ZERO)
    {
        viewRect_.left_ = Clamp(rect.left_, 0, rtWidth - 1);
        viewRect_.top_ = Clamp(rect.top_, 0, rtHeight - 1);
        viewRect_.right_ = Clamp(rect.right_, viewRect_.left_ + 1, rtWidth);
        viewRect_.bottom_ = Clamp(rect.bottom_, viewRect_.top_ + 1, rtHeight);
    }
    else
        viewRect_ = IntRect(0, 0, rtWidth, rtHeight);
    
    viewSize_ = viewRect_.Size();
    rtSize_ = IntVector2(rtWidth, rtHeight);
    
    // On OpenGL flip the viewport if rendering to a texture for consistent UV addressing with Direct3D9
    #ifdef USE_OPENGL
    if (renderTarget_)
    {
        viewRect_.bottom_ = rtSize_.y_ - viewRect_.top_;
        viewRect_.top_ = viewRect_.bottom_ - viewSize_.y_;
    }
    #endif
    
    drawShadows_ = renderer_->GetDrawShadows();
    materialQuality_ = renderer_->GetMaterialQuality();
    maxOccluderTriangles_ = renderer_->GetMaxOccluderTriangles();
    
    // Set possible quality overrides from the camera
    unsigned viewOverrideFlags = camera_->GetViewOverrideFlags();
    if (viewOverrideFlags & VO_LOW_MATERIAL_QUALITY)
        materialQuality_ = QUALITY_LOW;
    if (viewOverrideFlags & VO_DISABLE_SHADOWS)
        drawShadows_ = false;
    if (viewOverrideFlags & VO_DISABLE_OCCLUSION)
        maxOccluderTriangles_ = 0;
    
    return true;
}

void View::Update(const FrameInfo& frame)
{
    if (!camera_ || !octree_)
        return;
    
    frame_.camera_ = camera_;
    frame_.timeStep_ = frame.timeStep_;
    frame_.frameNumber_ = frame.frameNumber_;
    frame_.viewSize_ = viewSize_;
    
    int maxSortedInstances = renderer_->GetMaxSortedInstances();
    
    // Clear screen buffers, geometry, light, occluder & batch lists
    screenBuffers_.Clear();
    renderTargets_.Clear();
    geometries_.Clear();
    shadowGeometries_.Clear();
    lights_.Clear();
    zones_.Clear();
    occluders_.Clear();
    vertexLightQueues_.Clear();
    for (HashMap<StringHash, BatchQueue>::Iterator i = batchQueues_.Begin(); i != batchQueues_.End(); ++i)
        i->second_.Clear(maxSortedInstances);
    
    // Set automatic aspect ratio if required
    if (camera_->GetAutoAspectRatio())
        camera_->SetAspectRatio((float)frame_.viewSize_.x_ / (float)frame_.viewSize_.y_);
    
    GetDrawables();
    GetBatches();
}

void View::Render()
{
    if (!octree_ || !camera_)
        return;
    
    // Actually update geometry data now
    UpdateGeometries();
    
    // Allocate screen buffers as necessary
    AllocateScreenBuffers();
    
    // Initialize screenbuffer indices to use for read and write (pingponging)
    writeBuffer_ = 0;
    readBuffer_ = 0;
    
    // Forget parameter sources from the previous view
    graphics_->ClearParameterSources();
    
    // If stream offset is supported, write all instance transforms to a single large buffer
    // Else we must lock the instance buffer for each batch group
    if (renderer_->GetDynamicInstancing() && graphics_->GetStreamOffsetSupport())
        PrepareInstancingBuffer();
    
    // It is possible, though not recommended, that the same camera is used for multiple main views. Set automatic aspect ratio
    // again to ensure correct projection will be used
    if (camera_->GetAutoAspectRatio())
        camera_->SetAspectRatio((float)(viewSize_.x_) / (float)(viewSize_.y_));
    
    // Bind the face selection and indirection cube maps for point light shadows
    if (renderer_->GetDrawShadows())
    {
        graphics_->SetTexture(TU_FACESELECT, renderer_->GetFaceSelectCubeMap());
        graphics_->SetTexture(TU_INDIRECTION, renderer_->GetIndirectionCubeMap());
    }
    
    // Set "view texture" to prevent destination texture sampling during all renderpasses
    if (renderTarget_)
    {
        graphics_->SetViewTexture(renderTarget_->GetParentTexture());
        
        // On OpenGL, flip the projection if rendering to a texture so that the texture can be addressed in the same way
        // as a render texture produced on Direct3D9
        #ifdef USE_OPENGL
        camera_->SetFlipVertical(true);
        #endif
    }
    
    // Render
    ExecuteRenderPathCommands();
    
    #ifdef USE_OPENGL
    camera_->SetFlipVertical(false);
    #endif
    
    graphics_->SetDepthBias(0.0f, 0.0f);
    graphics_->SetScissorTest(false);
    graphics_->SetStencilTest(false);
    graphics_->SetViewTexture(0);
    graphics_->ResetStreamFrequencies();
    
    // Run framebuffer blitting if necessary
    if (screenBuffers_.Size() && currentRenderTarget_ != renderTarget_)
        BlitFramebuffer(static_cast<Texture2D*>(currentRenderTarget_->GetParentTexture()), renderTarget_, true);
    
    // If this is a main view, draw the associated debug geometry now
    if (!renderTarget_)
    {
        DebugRenderer* debug = octree_->GetComponent<DebugRenderer>();
        if (debug)
        {
            debug->SetView(camera_);
            debug->Render();
        }
    }
    
    // "Forget" the scene, camera, octree and zone after rendering
    scene_ = 0;
    camera_ = 0;
    octree_ = 0;
    cameraZone_ = 0;
    farClipZone_ = 0;
    occlusionBuffer_ = 0;
    frame_.camera_ = 0;
}

Graphics* View::GetGraphics() const
{
    return graphics_;
}

Renderer* View::GetRenderer() const
{
    return renderer_;
}

void View::GetDrawables()
{
    PROFILE(GetDrawables);
    
    WorkQueue* queue = GetSubsystem<WorkQueue>();
    PODVector<Drawable*>& tempDrawables = tempDrawables_[0];
    
    // Get zones and occluders first
    {
        ZoneOccluderOctreeQuery query(tempDrawables, camera_->GetFrustum(), DRAWABLE_GEOMETRY | DRAWABLE_ZONE, camera_->GetViewMask());
        octree_->GetDrawables(query);
    }
    
    highestZonePriority_ = M_MIN_INT;
    int bestPriority = M_MIN_INT;
    Vector3 cameraPos = cameraNode_->GetWorldPosition();
    
    // Get default zone first in case we do not have zones defined
    Zone* defaultZone = renderer_->GetDefaultZone();
    cameraZone_ = farClipZone_ = defaultZone;
    
    for (PODVector<Drawable*>::ConstIterator i = tempDrawables.Begin(); i != tempDrawables.End(); ++i)
    {
        Drawable* drawable = *i;
        unsigned char flags = drawable->GetDrawableFlags();
        
        if (flags & DRAWABLE_ZONE)
        {
            Zone* zone = static_cast<Zone*>(drawable);
            zones_.Push(zone);
            int priority = zone->GetPriority();
            if (priority > highestZonePriority_)
                highestZonePriority_ = priority;
            if (priority > bestPriority && zone->IsInside(cameraPos))
            {
                cameraZone_ = zone;
                bestPriority = priority;
            }
        }
        else
            occluders_.Push(drawable);
    }
    
    // Determine the zone at far clip distance. If not found, or camera zone has override mode, use camera zone
    cameraZoneOverride_ = cameraZone_->GetOverride();
    if (!cameraZoneOverride_)
    {
        Vector3 farClipPos = cameraPos + cameraNode_->GetWorldDirection() * Vector3(0.0f, 0.0f, camera_->GetFarClip());
        bestPriority = M_MIN_INT;
        
        for (PODVector<Zone*>::Iterator i = zones_.Begin(); i != zones_.End(); ++i)
        {
            int priority = (*i)->GetPriority();
            if (priority > bestPriority && (*i)->IsInside(farClipPos))
            {
                farClipZone_ = *i;
                bestPriority = priority;
            }
        }
    }
    if (farClipZone_ == defaultZone)
        farClipZone_ = cameraZone_;
    
    // If occlusion in use, get & render the occluders
    occlusionBuffer_ = 0;
    if (maxOccluderTriangles_ > 0)
    {
        UpdateOccluders(occluders_, camera_);
        if (occluders_.Size())
        {
            PROFILE(DrawOcclusion);
            
            occlusionBuffer_ = renderer_->GetOcclusionBuffer(camera_);
            DrawOccluders(occlusionBuffer_, occluders_);
        }
    }
    
    // Get lights and geometries. Coarse occlusion for octants is used at this point
    if (occlusionBuffer_)
    {
        OccludedFrustumOctreeQuery query(tempDrawables, camera_->GetFrustum(), occlusionBuffer_, DRAWABLE_GEOMETRY |
            DRAWABLE_LIGHT, camera_->GetViewMask());
        octree_->GetDrawables(query);
    }
    else
    {
        FrustumOctreeQuery query(tempDrawables, camera_->GetFrustum(), DRAWABLE_GEOMETRY | DRAWABLE_LIGHT,
            camera_->GetViewMask());
        octree_->GetDrawables(query);
    }
    
    // Check drawable occlusion and find zones for moved drawables in worker threads
    {
        WorkItem item;
        item.workFunction_ = CheckVisibilityWork;
        item.aux_ = this;
        
        PODVector<Drawable*>::Iterator start = tempDrawables.Begin();
        while (start != tempDrawables.End())
        {
            PODVector<Drawable*>::Iterator end = tempDrawables.End();
            if (end - start > CHECK_DRAWABLES_PER_WORK_ITEM)
                end = start + CHECK_DRAWABLES_PER_WORK_ITEM;
            
            item.start_ = &(*start);
            item.end_ = &(*end);
            queue->AddWorkItem(item);
            
            start = end;
        }
        
        queue->Complete(M_MAX_UNSIGNED);
    }
    
    // Sort into geometries & lights, and build visible scene bounding boxes in world and view space
    sceneBox_.min_ = sceneBox_.max_ = Vector3::ZERO;
    sceneBox_.defined_ = false;
    minZ_ = M_INFINITY;
    maxZ_ = 0.0f;
    
    unsigned cameraViewMask = camera_->GetViewMask();
    
    for (unsigned i = 0; i < tempDrawables.Size(); ++i)
    {
        Drawable* drawable = tempDrawables[i];
        if (!drawable->IsInView(frame_))
            continue;
        
        if (drawable->GetDrawableFlags() & DRAWABLE_GEOMETRY)
        {
            // Find zone for the drawable if necessary
            Zone* drawableZone = drawable->GetZone();
            if ((drawable->IsZoneDirty() || !drawableZone || (drawableZone->GetViewMask() & cameraViewMask) == 0) && !cameraZoneOverride_)
                FindZone(drawable);
            
            // Expand the scene bounding box and Z range (skybox not included because of infinite size) and store the drawawble
            if (drawable->GetType() != Skybox::GetTypeStatic())
            {
                sceneBox_.Merge(drawable->GetWorldBoundingBox());
                minZ_ = Min(minZ_, drawable->GetMinZ());
                maxZ_ = Max(maxZ_, drawable->GetMaxZ());
            }
            geometries_.Push(drawable);
        }
        else
        {
            Light* light = static_cast<Light*>(drawable);
            // Skip lights which are so dim that they can not contribute to a rendertarget
            if (light->GetColor().Intensity() > LIGHT_INTENSITY_THRESHOLD)
                lights_.Push(light);
        }
    }
    
    if (minZ_ == M_INFINITY)
        minZ_ = 0.0f;
    
    // Sort the lights to brightest/closest first
    for (unsigned i = 0; i < lights_.Size(); ++i)
    {
        Light* light = lights_[i];
        light->SetIntensitySortValue(camera_->GetDistance(light->GetNode()->GetWorldPosition()));
        light->SetLightQueue(0);
    }
    
    Sort(lights_.Begin(), lights_.End(), CompareDrawables);
}

void View::GetBatches()
{
    WorkQueue* queue = GetSubsystem<WorkQueue>();
    PODVector<Light*> vertexLights;
    BatchQueue* alphaQueue = batchQueues_.Contains(alphaPassName_) ? &batchQueues_[alphaPassName_] : (BatchQueue*)0;
    
    // Check whether to use the lit base pass optimization
    bool useLitBase = true;
    for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
    {
        const RenderPathCommand& command = renderPath_->commands_[i];
        if (command.type_ == CMD_FORWARDLIGHTS)
            useLitBase = command.useLitBase_;
    }
    
    // Process lit geometries and shadow casters for each light
    {
        PROFILE(ProcessLights);
        
        lightQueryResults_.Resize(lights_.Size());
        
        WorkItem item;
        item.workFunction_ = ProcessLightWork;
        item.aux_ = this;
        
        for (unsigned i = 0; i < lightQueryResults_.Size(); ++i)
        {
            LightQueryResult& query = lightQueryResults_[i];
            query.light_ = lights_[i];
            
            item.start_ = &query;
            queue->AddWorkItem(item);
        }
        
        // Ensure all lights have been processed before proceeding
        queue->Complete(M_MAX_UNSIGNED);
    }
    
    // Build light queues and lit batches
    {
        PROFILE(GetLightBatches);
        
        // Preallocate light queues: per-pixel lights which have lit geometries
        unsigned numLightQueues = 0;
        unsigned usedLightQueues = 0;
        for (Vector<LightQueryResult>::ConstIterator i = lightQueryResults_.Begin(); i != lightQueryResults_.End(); ++i)
        {
            if (!i->light_->GetPerVertex() && i->litGeometries_.Size())
                ++numLightQueues;
        }
        
        lightQueues_.Resize(numLightQueues);
        maxLightsDrawables_.Clear();
        unsigned maxSortedInstances = renderer_->GetMaxSortedInstances();
        
        for (Vector<LightQueryResult>::Iterator i = lightQueryResults_.Begin(); i != lightQueryResults_.End(); ++i)
        {
            LightQueryResult& query = *i;
            
            // If light has no affected geometries, no need to process further
            if (query.litGeometries_.Empty())
                continue;
            
            Light* light = query.light_;
            
            // Per-pixel light
            if (!light->GetPerVertex())
            {
                unsigned shadowSplits = query.numSplits_;
                
                // Initialize light queue and store it to the light so that it can be found later
                LightBatchQueue& lightQueue = lightQueues_[usedLightQueues++];
                light->SetLightQueue(&lightQueue);
                lightQueue.light_ = light;
                lightQueue.shadowMap_ = 0;
                lightQueue.litBatches_.Clear(maxSortedInstances);
                lightQueue.volumeBatches_.Clear();
                
                // Allocate shadow map now
                if (shadowSplits > 0)
                {
                    lightQueue.shadowMap_ = renderer_->GetShadowMap(light, camera_, viewSize_.x_, viewSize_.y_);
                    // If did not manage to get a shadow map, convert the light to unshadowed
                    if (!lightQueue.shadowMap_)
                        shadowSplits = 0;
                }
                
                // Setup shadow batch queues
                lightQueue.shadowSplits_.Resize(shadowSplits);
                for (unsigned j = 0; j < shadowSplits; ++j)
                {
                    ShadowBatchQueue& shadowQueue = lightQueue.shadowSplits_[j];
                    Camera* shadowCamera = query.shadowCameras_[j];
                    shadowQueue.shadowCamera_ = shadowCamera;
                    shadowQueue.nearSplit_ = query.shadowNearSplits_[j];
                    shadowQueue.farSplit_ = query.shadowFarSplits_[j];
                    shadowQueue.shadowBatches_.Clear(maxSortedInstances);
                    
                    // Setup the shadow split viewport and finalize shadow camera parameters
                    shadowQueue.shadowViewport_ = GetShadowMapViewport(light, j, lightQueue.shadowMap_);
                    FinalizeShadowCamera(shadowCamera, light, shadowQueue.shadowViewport_, query.shadowCasterBox_[j]);
                    
                    // Loop through shadow casters
                    for (PODVector<Drawable*>::ConstIterator k = query.shadowCasters_.Begin() + query.shadowCasterBegin_[j];
                        k < query.shadowCasters_.Begin() + query.shadowCasterEnd_[j]; ++k)
                    {
                        Drawable* drawable = *k;
                        if (!drawable->IsInView(frame_, false))
                        {
                            drawable->MarkInView(frame_, false);
                            shadowGeometries_.Push(drawable);
                        }
                        
                        Zone* zone = GetZone(drawable);
                        const Vector<SourceBatch>& batches = drawable->GetBatches();
                        
                        for (unsigned l = 0; l < batches.Size(); ++l)
                        {
                            const SourceBatch& srcBatch = batches[l];
                            
                            Technique* tech = GetTechnique(drawable, srcBatch.material_);
                            if (!srcBatch.geometry_ || !tech)
                                continue;
                            
                            Pass* pass = tech->GetPass(PASS_SHADOW);
                            // Skip if material has no shadow pass
                            if (!pass)
                                continue;
                            
                            Batch destBatch(srcBatch);
                            destBatch.pass_ = pass;
                            destBatch.camera_ = shadowCamera;
                            destBatch.zone_ = zone;
                            destBatch.lightQueue_ = &lightQueue;
                            
                            AddBatchToQueue(shadowQueue.shadowBatches_, destBatch, tech);
                        }
                    }
                }
                
                // Process lit geometries
                for (PODVector<Drawable*>::ConstIterator j = query.litGeometries_.Begin(); j != query.litGeometries_.End(); ++j)
                {
                    Drawable* drawable = *j;
                    drawable->AddLight(light);
                    
                    // If drawable limits maximum lights, only record the light, and check maximum count / build batches later
                    if (!drawable->GetMaxLights())
                        GetLitBatches(drawable, lightQueue, alphaQueue, useLitBase);
                    else
                        maxLightsDrawables_.Insert(drawable);
                }
                
                // In deferred modes, store the light volume batch now
                if (deferred_)
                {
                    Batch volumeBatch;
                    volumeBatch.geometry_ = renderer_->GetLightGeometry(light);
                    volumeBatch.worldTransform_ = &light->GetVolumeTransform(camera_);
                    volumeBatch.overrideView_ = light->GetLightType() == LIGHT_DIRECTIONAL;
                    volumeBatch.camera_ = camera_;
                    volumeBatch.lightQueue_ = &lightQueue;
                    volumeBatch.distance_ = light->GetDistance();
                    volumeBatch.material_ = 0;
                    volumeBatch.pass_ = 0;
                    volumeBatch.zone_ = 0;
                    renderer_->SetLightVolumeBatchShaders(volumeBatch, lightVS_, lightPS_);
                    lightQueue.volumeBatches_.Push(volumeBatch);
                }
            }
            // Per-vertex light
            else
            {
                // Add the vertex light to lit drawables. It will be processed later during base pass batch generation
                for (PODVector<Drawable*>::ConstIterator j = query.litGeometries_.Begin(); j != query.litGeometries_.End(); ++j)
                {
                    Drawable* drawable = *j;
                    drawable->AddVertexLight(light);
                }
            }
        }
    }
    
    // Process drawables with limited per-pixel light count
    if (maxLightsDrawables_.Size())
    {
        PROFILE(GetMaxLightsBatches);
        
        for (HashSet<Drawable*>::Iterator i = maxLightsDrawables_.Begin(); i != maxLightsDrawables_.End(); ++i)
        {
            Drawable* drawable = *i;
            drawable->LimitLights();
            const PODVector<Light*>& lights = drawable->GetLights();
            
            for (unsigned i = 0; i < lights.Size(); ++i)
            {
                Light* light = lights[i];
                // Find the correct light queue again
                LightBatchQueue* queue = light->GetLightQueue();
                if (queue)
                    GetLitBatches(drawable, *queue, alphaQueue, useLitBase);
            }
        }
    }
    
    // Build base pass batches
    {
        PROFILE(GetBaseBatches);
        
        for (PODVector<Drawable*>::ConstIterator i = geometries_.Begin(); i != geometries_.End(); ++i)
        {
            Drawable* drawable = *i;
            Zone* zone = GetZone(drawable);
            const Vector<SourceBatch>& batches = drawable->GetBatches();
            
            const PODVector<Light*>& drawableVertexLights = drawable->GetVertexLights();
            if (!drawableVertexLights.Empty())
                drawable->LimitVertexLights();
            
            for (unsigned j = 0; j < batches.Size(); ++j)
            {
                const SourceBatch& srcBatch = batches[j];
                
                // Check here if the material refers to a rendertarget texture with camera(s) attached
                // Only check this for backbuffer views (null rendertarget)
                if (srcBatch.material_ && srcBatch.material_->GetAuxViewFrameNumber() != frame_.frameNumber_ && !renderTarget_)
                    CheckMaterialForAuxView(srcBatch.material_);
                
                Technique* tech = GetTechnique(drawable, srcBatch.material_);
                if (!srcBatch.geometry_ || !tech)
                    continue;
                
                Batch destBatch(srcBatch);
                destBatch.camera_ = camera_;
                destBatch.zone_ = zone;
                destBatch.isBase_ = true;
                destBatch.pass_ = 0;
                destBatch.lightMask_ = GetLightMask(drawable);
                
                // Check each of the scene passes
                for (unsigned k = 0; k < scenePasses_.Size(); ++k)
                {
                    ScenePassInfo& info = scenePasses_[k];
                    destBatch.pass_ = tech->GetPass(info.pass_);
                    if (!destBatch.pass_)
                        continue;
                    
                    // Skip forward base pass if the corresponding litbase pass already exists
                    if (info.pass_ == basePassName_ && j < 32 && drawable->HasBasePass(j))
                        continue;
                    
                    if (info.vertexLights_ && !drawableVertexLights.Empty())
                    {
                        // For a deferred opaque batch, check if the vertex lights include converted per-pixel lights, and remove
                        // them to prevent double-lighting
                        if (deferred_ && destBatch.pass_->GetBlendMode() == BLEND_REPLACE)
                        {
                            vertexLights.Clear();
                            for (unsigned i = 0; i < drawableVertexLights.Size(); ++i)
                            {
                                if (drawableVertexLights[i]->GetPerVertex())
                                    vertexLights.Push(drawableVertexLights[i]);
                            }
                        }
                        else
                            vertexLights = drawableVertexLights;
                        
                        if (!vertexLights.Empty())
                        {
                            // Find a vertex light queue. If not found, create new
                            unsigned long long hash = GetVertexLightQueueHash(vertexLights);
                            HashMap<unsigned long long, LightBatchQueue>::Iterator i = vertexLightQueues_.Find(hash);
                            if (i == vertexLightQueues_.End())
                            {
                                i = vertexLightQueues_.Insert(MakePair(hash, LightBatchQueue()));
                                i->second_.light_ = 0;
                                i->second_.shadowMap_ = 0;
                                i->second_.vertexLights_ = vertexLights;
                            }
                            
                            destBatch.lightQueue_ = &(i->second_);
                        }
                    }
                    else
                        destBatch.lightQueue_ = 0;
                    
                    bool allowInstancing = info.allowInstancing_;
                    if (allowInstancing && info.markToStencil_ && destBatch.lightMask_ != (zone->GetLightMask() & 0xff))
                        allowInstancing = false;
                    
                    AddBatchToQueue(*info.batchQueue_, destBatch, tech, allowInstancing);
                }
            }
        }
    }
}

void View::UpdateGeometries()
{
    PROFILE(SortAndUpdateGeometry);
    
    WorkQueue* queue = GetSubsystem<WorkQueue>();
    
    // Sort batches
    {
        WorkItem item;
        
        for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
        {
            const RenderPathCommand& command = renderPath_->commands_[i];
            if (!command.enabled_)
                continue;
            
            if (command.type_ == CMD_SCENEPASS)
            {
                item.workFunction_ = command.sortMode_ == SORT_FRONTTOBACK ? SortBatchQueueFrontToBackWork :
                    SortBatchQueueBackToFrontWork;
                item.start_ = &batchQueues_[command.pass_];
                queue->AddWorkItem(item);
            }
        }
        
        for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
        {
            item.workFunction_ = SortLightQueueWork;
            item.start_ = &(*i);
            queue->AddWorkItem(item);
            if (i->shadowSplits_.Size())
            {
                item.workFunction_ = SortShadowQueueWork;
                queue->AddWorkItem(item);
            }
        }
    }
    
    // Update geometries. Split into threaded and non-threaded updates.
    {
        nonThreadedGeometries_.Clear();
        threadedGeometries_.Clear();
        
        for (PODVector<Drawable*>::Iterator i = geometries_.Begin(); i != geometries_.End(); ++i)
        {
            UpdateGeometryType type = (*i)->GetUpdateGeometryType();
            if (type == UPDATE_MAIN_THREAD)
                nonThreadedGeometries_.Push(*i);
            else if (type == UPDATE_WORKER_THREAD)
                threadedGeometries_.Push(*i);
        }
        for (PODVector<Drawable*>::Iterator i = shadowGeometries_.Begin(); i != shadowGeometries_.End(); ++i)
        {
            UpdateGeometryType type = (*i)->GetUpdateGeometryType();
            if (type == UPDATE_MAIN_THREAD)
                nonThreadedGeometries_.Push(*i);
            else if (type == UPDATE_WORKER_THREAD)
                threadedGeometries_.Push(*i);
        }
        
        if (threadedGeometries_.Size())
        {
            WorkItem item;
            item.workFunction_ = UpdateDrawableGeometriesWork;
            item.aux_ = const_cast<FrameInfo*>(&frame_);
            
            PODVector<Drawable*>::Iterator start = threadedGeometries_.Begin();
            while (start != threadedGeometries_.End())
            {
                PODVector<Drawable*>::Iterator end = threadedGeometries_.End();
                if (end - start > DRAWABLES_PER_WORK_ITEM)
                    end = start + DRAWABLES_PER_WORK_ITEM;
                
                item.start_ = &(*start);
                item.end_ = &(*end);
                queue->AddWorkItem(item);
                
                start = end;
            }
        }
        
        // While the work queue is processed, update non-threaded geometries
        for (PODVector<Drawable*>::ConstIterator i = nonThreadedGeometries_.Begin(); i != nonThreadedGeometries_.End(); ++i)
            (*i)->UpdateGeometry(frame_);
    }
    
    // Finally ensure all threaded work has completed
    queue->Complete(M_MAX_UNSIGNED);
}

void View::GetLitBatches(Drawable* drawable, LightBatchQueue& lightQueue, BatchQueue* alphaQueue, bool useLitBase)
{
    Light* light = lightQueue.light_;
    Zone* zone = GetZone(drawable);
    const Vector<SourceBatch>& batches = drawable->GetBatches();
    
    bool hasAmbientGradient = zone->GetAmbientGradient() && zone->GetAmbientStartColor() != zone->GetAmbientEndColor();
    // Shadows on transparencies can only be rendered if shadow maps are not reused
    bool allowTransparentShadows = !renderer_->GetReuseShadowMaps();
    bool allowLitBase = useLitBase && light == drawable->GetFirstLight() && drawable->GetVertexLights().Empty() && !hasAmbientGradient;
    
    for (unsigned i = 0; i < batches.Size(); ++i)
    {
        const SourceBatch& srcBatch = batches[i];
        
        Technique* tech = GetTechnique(drawable, srcBatch.material_);
        if (!srcBatch.geometry_ || !tech)
            continue;
        
        // Do not create pixel lit forward passes for materials that render into the G-buffer
        if (gBufferPassName_.Value() && tech->HasPass(gBufferPassName_))
            continue;
        
        Batch destBatch(srcBatch);
        bool isLitAlpha = false;
        
        // Check for lit base pass. Because it uses the replace blend mode, it must be ensured to be the first light
        // Also vertex lighting or ambient gradient require the non-lit base pass, so skip in those cases
        if (i < 32 && allowLitBase)
        {
            destBatch.pass_ = tech->GetPass(litBasePassName_);
            if (destBatch.pass_)
            {
                destBatch.isBase_ = true;
                drawable->SetBasePass(i);
            }
            else
                destBatch.pass_ = tech->GetPass(lightPassName_);
        }
        else
            destBatch.pass_ = tech->GetPass(lightPassName_);
        
        // If no lit pass, check for lit alpha
        if (!destBatch.pass_)
        {
            destBatch.pass_ = tech->GetPass(litAlphaPassName_);
            isLitAlpha = true;
        }
        
        // Skip if material does not receive light at all
        if (!destBatch.pass_)
            continue;
        
        destBatch.camera_ = camera_;
        destBatch.lightQueue_ = &lightQueue;
        destBatch.zone_ = zone;
        
        if (!isLitAlpha)
            AddBatchToQueue(lightQueue.litBatches_, destBatch, tech);
        else if (alphaQueue)
        {
            // Transparent batches can not be instanced
            AddBatchToQueue(*alphaQueue, destBatch, tech, false, allowTransparentShadows);
        }
    }
}

void View::ExecuteRenderPathCommands()
{
    // If not reusing shadowmaps, render all of them first
    if (!renderer_->GetReuseShadowMaps() && renderer_->GetDrawShadows() && !lightQueues_.Empty())
    {
        PROFILE(RenderShadowMaps);
        
        for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
        {
            if (i->shadowMap_)
                RenderShadowMap(*i);
        }
    }
    
    // Check if forward rendering needs to resolve the multisampled backbuffer to a texture
    bool needResolve = !deferred_ && !renderTarget_ && graphics_->GetMultiSample() > 1 && screenBuffers_.Size();
    
    {
        PROFILE(RenderCommands);
        
        unsigned lastCommandIndex = 0;
        for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
        {
            if (!renderPath_->commands_[i].enabled_)
                continue;
            lastCommandIndex = i;
        }
        
        for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
        {
            RenderPathCommand& command = renderPath_->commands_[i];
            if (!command.enabled_)
                continue;
            
            // If command writes and reads the target at same time, pingpong automatically
            if (CheckViewportRead(command))
            {
                readBuffer_ = writeBuffer_;
                if (!command.outputNames_[0].Compare("viewport", false))
                {
                    ++writeBuffer_;
                    if (writeBuffer_ >= screenBuffers_.Size())
                        writeBuffer_ = 0;
                    
                    // If this is a scene render pass, must copy the previous viewport contents now
                    if (command.type_ == CMD_SCENEPASS && !needResolve)
                    {
                        BlitFramebuffer(screenBuffers_[readBuffer_], screenBuffers_[writeBuffer_]->GetRenderSurface(), false);
                    }
                    
                }
                
                // Resolve multisampled framebuffer now if necessary
                /// \todo Does not copy the depth buffer
                if (needResolve)
                {
                    graphics_->ResolveToTexture(screenBuffers_[readBuffer_], viewRect_);
                    needResolve = false;
                }
            }
            
            // Check which rendertarget will be used on this pass
            if (screenBuffers_.Size() && !needResolve)
                currentRenderTarget_ = screenBuffers_[writeBuffer_]->GetRenderSurface();
            else
                currentRenderTarget_ = renderTarget_;
            
            // Optimization: if the last command is a quad with output to the viewport, do not use the screenbuffers,
            // but the viewport directly. This saves the extra copy
            if (screenBuffers_.Size() && i == lastCommandIndex && command.type_ == CMD_QUAD && command.outputNames_.Size() == 1 &&
                !command.outputNames_[0].Compare("viewport", false))
                currentRenderTarget_ = renderTarget_;
            
            switch (command.type_)
            {
            case CMD_CLEAR:
                {
                    PROFILE(ClearRenderTarget);
                    
                    Color clearColor = command.clearColor_;
                    if (command.useFogColor_)
                        clearColor = farClipZone_->GetFogColor();
                    
                    SetRenderTargets(command);
                    graphics_->Clear(command.clearFlags_, clearColor, command.clearDepth_, command.clearStencil_);
                }
                break;
                
            case CMD_SCENEPASS:
                if (!batchQueues_[command.pass_].IsEmpty())
                {
                    PROFILE(RenderScenePass);
                    
                    SetRenderTargets(command);
                    SetTextures(command);
                    graphics_->SetFillMode(camera_->GetFillMode());
                    batchQueues_[command.pass_].Draw(this, command.useScissor_, command.markToStencil_);
                }
                break;
                
            case CMD_QUAD:
                {
                    PROFILE(RenderQuad);
                    
                    SetRenderTargets(command);
                    SetTextures(command);
                    RenderQuad(command);
                }
                break;
                
            case CMD_FORWARDLIGHTS:
                // Render shadow maps + opaque objects' additive lighting
                if (!lightQueues_.Empty())
                {
                    PROFILE(RenderLights);
                    
                    SetRenderTargets(command);
                    
                    for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
                    {
                        // If reusing shadowmaps, render each of them before the lit batches
                        if (renderer_->GetReuseShadowMaps() && i->shadowMap_)
                        {
                            RenderShadowMap(*i);
                            SetRenderTargets(command);
                        }
                        
                        SetTextures(command);
                        graphics_->SetFillMode(camera_->GetFillMode());
                        i->litBatches_.Draw(i->light_, this);
                    }
                    
                    graphics_->SetScissorTest(false);
                    graphics_->SetStencilTest(false);
                }
                break;
                
            case CMD_LIGHTVOLUMES:
                // Render shadow maps + light volumes
                if (!lightQueues_.Empty())
                {
                    PROFILE(RenderLightVolumes);
                    
                    SetRenderTargets(command);
                    
                    for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
                    {
                        // If reusing shadowmaps, render each of them before the lit batches
                        if (renderer_->GetReuseShadowMaps() && i->shadowMap_)
                        {
                            RenderShadowMap(*i);
                            SetRenderTargets(command);
                        }
                        
                        SetTextures(command);
                        
                        for (unsigned j = 0; j < i->volumeBatches_.Size(); ++j)
                        {
                            SetupLightVolumeBatch(i->volumeBatches_[j]);
                            i->volumeBatches_[j].Draw(this);
                        }
                    }
                    
                    graphics_->SetScissorTest(false);
                    graphics_->SetStencilTest(false);
                }
                break;
            
            default:
                break;
            }
        }
    }
    
    // After executing all commands, reset rendertarget for debug geometry rendering
    graphics_->SetRenderTarget(0, renderTarget_);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->SetRenderTarget(i, (RenderSurface*)0);
    graphics_->SetDepthStencil(GetDepthStencil(renderTarget_));
    graphics_->SetViewport(viewRect_);
    graphics_->SetFillMode(FILL_SOLID);
}

void View::SetRenderTargets(RenderPathCommand& command)
{
    unsigned index = 0;
    IntRect viewPort = viewRect_;
    
    while (index < command.outputNames_.Size())
    {
        if (!command.outputNames_[index].Compare("viewport", false))
            graphics_->SetRenderTarget(index, currentRenderTarget_);
        else
        {
            StringHash nameHash(command.outputNames_[index]);
            if (renderTargets_.Contains(nameHash))
            {
                Texture2D* texture = renderTargets_[nameHash];
                graphics_->SetRenderTarget(index, texture);
                if (!index)
                {
                    // Determine viewport size from rendertarget info
                    for (unsigned i = 0; i < renderPath_->renderTargets_.Size(); ++i)
                    {
                        const RenderTargetInfo& info = renderPath_->renderTargets_[i];
                        if (!info.name_.Compare(command.outputNames_[index], false))
                        {
                            switch (info.sizeMode_)
                            {
                                // If absolute or a divided viewport size, use the full texture
                            case SIZE_ABSOLUTE:
                            case SIZE_VIEWPORTDIVISOR:
                                viewPort = IntRect(0, 0, texture->GetWidth(), texture->GetHeight());
                                break;
                            
                                // If a divided rendertarget size, retain the same viewport, but scaled
                            case SIZE_RENDERTARGETDIVISOR:
                                if (info.size_.x_ && info.size_.y_)
                                {
                                    viewPort = IntRect(viewRect_.left_ / info.size_.x_, viewRect_.top_ / info.size_.y_,
                                        viewRect_.right_ / info.size_.x_, viewRect_.bottom_ / info.size_.y_);
                                }
                                break;
                            }
                            break;
                        }
                    }
                }
                
            }
            else
                graphics_->SetRenderTarget(0, (RenderSurface*)0);
        }
        
        ++index;
    }
    
    while (index < MAX_RENDERTARGETS)
    {
        graphics_->SetRenderTarget(index, (RenderSurface*)0);
        ++index;
    }
    
    graphics_->SetDepthStencil(GetDepthStencil(graphics_->GetRenderTarget(0)));
    graphics_->SetViewport(viewPort);
    graphics_->SetColorWrite(true);
}

void View::SetTextures(RenderPathCommand& command)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (command.textureNames_[i].Empty())
            continue;
        
        // Bind the rendered output
        if (!command.textureNames_[i].Compare("viewport", false))
        {
            graphics_->SetTexture(i, screenBuffers_[readBuffer_]);
            continue;
        }
        
        // Bind a rendertarget
        HashMap<StringHash, Texture2D*>::ConstIterator j = renderTargets_.Find(command.textureNames_[i]);
        if (j != renderTargets_.End())
        {
            graphics_->SetTexture(i, j->second_);
            continue;
        }
        
        // Bind a texture from the resource system
        Texture2D* texture = cache->GetResource<Texture2D>(command.textureNames_[i]);
        if (texture)
            graphics_->SetTexture(i, texture);
        else
        {
            // If requesting a texture fails, clear the texture name to prevent redundant attempts
            command.textureNames_[i] = String::EMPTY;
        }
    }
}

void View::RenderQuad(RenderPathCommand& command)
{
    // If shader can not be found, clear it from the command to prevent redundant attempts
    ShaderVariation* vs = renderer_->GetVertexShader(command.vertexShaderName_);
    if (!vs)
        command.vertexShaderName_ = String::EMPTY;
    ShaderVariation* ps = renderer_->GetPixelShader(command.pixelShaderName_);
    if (!ps)
        command.pixelShaderName_ = String::EMPTY;
    
    // Set shaders & shader parameters and textures
    graphics_->SetShaders(vs, ps);
    
    const HashMap<StringHash, Vector4>& parameters = command.shaderParameters_;
    for (HashMap<StringHash, Vector4>::ConstIterator k = parameters.Begin(); k != parameters.End(); ++k)
        graphics_->SetShaderParameter(k->first_, k->second_);
    
    float rtWidth = (float)rtSize_.x_;
    float rtHeight = (float)rtSize_.y_;
    float widthRange = 0.5f * viewSize_.x_ / rtWidth;
    float heightRange = 0.5f * viewSize_.y_ / rtHeight;
    
    #ifdef USE_OPENGL
    Vector4 bufferUVOffset(((float)viewRect_.left_) / rtWidth + widthRange,
        1.0f - (((float)viewRect_.top_) / rtHeight + heightRange), widthRange, heightRange);
    #else
    Vector4 bufferUVOffset((0.5f + (float)viewRect_.left_) / rtWidth + widthRange,
        (0.5f + (float)viewRect_.top_) / rtHeight + heightRange, widthRange, heightRange);
    #endif
    
    graphics_->SetShaderParameter(VSP_GBUFFEROFFSETS, bufferUVOffset);
    graphics_->SetShaderParameter(PSP_GBUFFERINVSIZE, Vector4(1.0f / rtWidth, 1.0f / rtHeight, 0.0f, 0.0f));
    
    // Set per-rendertarget inverse size / offset shader parameters as necessary
    for (unsigned i = 0; i < renderPath_->renderTargets_.Size(); ++i)
    {
        const RenderTargetInfo& rtInfo = renderPath_->renderTargets_[i];
        if (!rtInfo.enabled_)
            continue;
        
        StringHash nameHash(rtInfo.name_);
        if (!renderTargets_.Contains(nameHash))
            continue;
        
        String invSizeName = rtInfo.name_ + "InvSize";
        String offsetsName = rtInfo.name_ + "Offsets";
        float width = (float)renderTargets_[nameHash]->GetWidth();
        float height = (float)renderTargets_[nameHash]->GetHeight();
        
        graphics_->SetShaderParameter(invSizeName, Vector4(1.0f / width, 1.0f / height, 0.0f, 0.0f));
        #ifdef USE_OPENGL
        graphics_->SetShaderParameter(offsetsName, Vector4::ZERO);
        #else
        graphics_->SetShaderParameter(offsetsName, Vector4(0.5f / width, 0.5f / height, 0.0f, 0.0f));
        #endif
    }
    
    graphics_->SetBlendMode(BLEND_REPLACE);
    graphics_->SetDepthTest(CMP_ALWAYS);
    graphics_->SetDepthWrite(false);
    graphics_->SetFillMode(FILL_SOLID);
    graphics_->SetScissorTest(false);
    graphics_->SetStencilTest(false);
    
    DrawFullscreenQuad(false);
}

bool View::CheckViewportRead(const RenderPathCommand& command)
{
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (!command.textureNames_[i].Empty() && !command.textureNames_[i].Compare("viewport", false))
            return true;
    }
    
    return false;
}

void View::AllocateScreenBuffers()
{
    unsigned neededBuffers = 0;
    #ifdef USE_OPENGL
    // Due to FBO limitations, in OpenGL deferred modes need to render to texture first and then blit to the backbuffer
    // Also, if rendering to a texture with deferred rendering, it must be RGBA to comply with the rest of the buffers.
    if (deferred_ && (!renderTarget_ || (deferred_ && renderTarget_->GetParentTexture()->GetFormat() != 
        Graphics::GetRGBAFormat())))
        neededBuffers = 1;
    #endif
    // If backbuffer is antialiased when using deferred rendering, need to reserve a buffer
    if (deferred_ && !renderTarget_ && graphics_->GetMultiSample() > 1)
        neededBuffers = 1;
    
    unsigned format = Graphics::GetRGBFormat();
    #ifdef USE_OPENGL
    if (deferred_)
        format = Graphics::GetRGBAFormat();
    #endif
    
    // Check for commands which read the rendered scene and allocate a buffer for each, up to 2 maximum for pingpong
    /// \todo If the last copy is optimized away, this allocates an extra buffer unnecessarily
    bool hasViewportRead = false;
    bool hasViewportReadWrite = false;
    
    for (unsigned i = 0; i < renderPath_->commands_.Size(); ++i)
    {
        const RenderPathCommand& command = renderPath_->commands_[i];
        if (!command.enabled_)
            continue;
        if (CheckViewportRead(command))
        {
            hasViewportRead = true;
            if (!command.outputNames_[0].Compare("viewport", false))
                hasViewportReadWrite = true;
        }
    }
    if (hasViewportRead && !neededBuffers)
        neededBuffers = 1;
    if (hasViewportReadWrite)
        neededBuffers = 2;
    
    // Allocate screen buffers with filtering active in case the quad commands need that
    // Follow the sRGB mode of the destination rendertarget
    bool sRGB = renderTarget_ ? renderTarget_->GetParentTexture()->GetSRGB() : graphics_->GetSRGB();
    for (unsigned i = 0; i < neededBuffers; ++i)
        screenBuffers_.Push(renderer_->GetScreenBuffer(rtSize_.x_, rtSize_.y_, format, true, sRGB));
    
    // Allocate extra render targets defined by the rendering path
    for (unsigned i = 0; i < renderPath_->renderTargets_.Size(); ++i)
    {
        const RenderTargetInfo& rtInfo = renderPath_->renderTargets_[i];
        if (!rtInfo.enabled_)
            continue;
        
        unsigned width = rtInfo.size_.x_;
        unsigned height = rtInfo.size_.y_;
        
        if (rtInfo.sizeMode_ == SIZE_VIEWPORTDIVISOR)
        {
            width = viewSize_.x_ / (width ? width : 1);
            height = viewSize_.y_ / (height ? height : 1);
        }
        if (rtInfo.sizeMode_ == SIZE_RENDERTARGETDIVISOR)
        {
            width = rtSize_.x_ / (width ? width : 1);
            height = rtSize_.y_ / (height ? height : 1);
        }
        
        renderTargets_[rtInfo.name_] = renderer_->GetScreenBuffer(width, height, rtInfo.format_, rtInfo.filtered_, rtInfo.sRGB_);
    }
}

void View::BlitFramebuffer(Texture2D* source, RenderSurface* destination, bool depthWrite)
{
    PROFILE(BlitFramebuffer);
    
    graphics_->SetBlendMode(BLEND_REPLACE);
    graphics_->SetDepthTest(CMP_ALWAYS);
    graphics_->SetDepthWrite(true);
    graphics_->SetFillMode(FILL_SOLID);
    graphics_->SetScissorTest(false);
    graphics_->SetStencilTest(false);
    graphics_->SetRenderTarget(0, destination);
    for (unsigned i = 1; i < MAX_RENDERTARGETS; ++i)
        graphics_->SetRenderTarget(i, (RenderSurface*)0);
    graphics_->SetDepthStencil(GetDepthStencil(destination));
    graphics_->SetViewport(viewRect_);
    
    String shaderName = "CopyFramebuffer";
    graphics_->SetShaders(renderer_->GetVertexShader(shaderName), renderer_->GetPixelShader(shaderName));
    
    float rtWidth = (float)rtSize_.x_;
    float rtHeight = (float)rtSize_.y_;
    float widthRange = 0.5f * viewSize_.x_ / rtWidth;
    float heightRange = 0.5f * viewSize_.y_ / rtHeight;
    
    #ifdef USE_OPENGL
    Vector4 bufferUVOffset(((float)viewRect_.left_) / rtWidth + widthRange,
        1.0f - (((float)viewRect_.top_) / rtHeight + heightRange), widthRange, heightRange);
    #else
    Vector4 bufferUVOffset((0.5f + (float)viewRect_.left_) / rtWidth + widthRange,
        (0.5f + (float)viewRect_.top_) / rtHeight + heightRange, widthRange, heightRange);
    #endif
    
    graphics_->SetShaderParameter(VSP_GBUFFEROFFSETS, bufferUVOffset);
    graphics_->SetTexture(TU_DIFFUSE, source);
    DrawFullscreenQuad(false);
}

void View::DrawFullscreenQuad(bool nearQuad)
{
    Light* quadDirLight = renderer_->GetQuadDirLight();
    Geometry* geometry = renderer_->GetLightGeometry(quadDirLight);
    
    Matrix3x4 model = Matrix3x4::IDENTITY;
    Matrix4 projection = Matrix4::IDENTITY;
    
    #ifdef USE_OPENGL
    model.m23_ = nearQuad ? -1.0f : 1.0f;
    #else
    model.m23_ = nearQuad ? 0.0f : 1.0f;
    #endif
    
    graphics_->SetCullMode(CULL_NONE);
    graphics_->SetShaderParameter(VSP_MODEL, model);
    graphics_->SetShaderParameter(VSP_VIEWPROJ, projection);
    graphics_->ClearTransformSources();
    
    geometry->Draw(graphics_);
}

void View::UpdateOccluders(PODVector<Drawable*>& occluders, Camera* camera)
{
    float occluderSizeThreshold_ = renderer_->GetOccluderSizeThreshold();
    float halfViewSize = camera->GetHalfViewSize();
    float invOrthoSize = 1.0f / camera->GetOrthoSize();

    for (PODVector<Drawable*>::Iterator i = occluders.Begin(); i != occluders.End();)
    {
        Drawable* occluder = *i;
        bool erase = false;
        
        if (!occluder->IsInView(frame_, false))
            occluder->UpdateBatches(frame_);
        
        // Check occluder's draw distance (in main camera view)
        float maxDistance = occluder->GetDrawDistance();
        if (maxDistance <= 0.0f || occluder->GetDistance() <= maxDistance)
        {
            // Check that occluder is big enough on the screen
            const BoundingBox& box = occluder->GetWorldBoundingBox();
            float diagonal = box.Size().Length();
            float compare;
            if (!camera->IsOrthographic())
                compare = diagonal * halfViewSize / occluder->GetDistance();
            else
                compare = diagonal * invOrthoSize;
            
            if (compare < occluderSizeThreshold_)
                erase = true;
            else
            {
                // Store amount of triangles divided by screen size as a sorting key
                // (best occluders are big and have few triangles)
                occluder->SetSortValue((float)occluder->GetNumOccluderTriangles() / compare);
            }
        }
        else
            erase = true;
        
        if (erase)
            i = occluders.Erase(i);
        else
            ++i;
    }
    
    // Sort occluders so that if triangle budget is exceeded, best occluders have been drawn
    if (occluders.Size())
        Sort(occluders.Begin(), occluders.End(), CompareDrawables);
}

void View::DrawOccluders(OcclusionBuffer* buffer, const PODVector<Drawable*>& occluders)
{
    buffer->SetMaxTriangles(maxOccluderTriangles_);
    buffer->Clear();
    
    for (unsigned i = 0; i < occluders.Size(); ++i)
    {
        Drawable* occluder = occluders[i];
        if (i > 0)
        {
            // For subsequent occluders, do a test against the pixel-level occlusion buffer to see if rendering is necessary
            if (!buffer->IsVisible(occluder->GetWorldBoundingBox()))
                continue;
        }
        
        // Check for running out of triangles
        if (!occluder->DrawOcclusion(buffer))
            break;
    }
    
    buffer->BuildDepthHierarchy();
}

void View::ProcessLight(LightQueryResult& query, unsigned threadIndex)
{
    Light* light = query.light_;
    LightType type = light->GetLightType();
    const Frustum& frustum = camera_->GetFrustum();
    
    // Check if light should be shadowed
    bool isShadowed = drawShadows_ && light->GetCastShadows() && !light->GetPerVertex() && light->GetShadowIntensity() < 1.0f;
    // If shadow distance non-zero, check it
    if (isShadowed && light->GetShadowDistance() > 0.0f && light->GetDistance() > light->GetShadowDistance())
        isShadowed = false;
    // OpenGL ES can not support point light shadows
    #ifdef GL_ES_VERSION_2_0
    if (isShadowed && type == LIGHT_POINT)
        isShadowed = false;
    #endif
    // Get lit geometries. They must match the light mask and be inside the main camera frustum to be considered
    PODVector<Drawable*>& tempDrawables = tempDrawables_[threadIndex];
    query.litGeometries_.Clear();
    
    switch (type)
    {
    case LIGHT_DIRECTIONAL:
        for (unsigned i = 0; i < geometries_.Size(); ++i)
        {
            if (GetLightMask(geometries_[i]) & light->GetLightMask())
                query.litGeometries_.Push(geometries_[i]);
        }
        break;
        
    case LIGHT_SPOT:
        {
            FrustumOctreeQuery octreeQuery(tempDrawables, light->GetFrustum(), DRAWABLE_GEOMETRY, camera_->GetViewMask());
            octree_->GetDrawables(octreeQuery);
            for (unsigned i = 0; i < tempDrawables.Size(); ++i)
            {
                if (tempDrawables[i]->IsInView(frame_) && (GetLightMask(tempDrawables[i]) & light->GetLightMask()))
                    query.litGeometries_.Push(tempDrawables[i]);
            }
        }
        break;
        
    case LIGHT_POINT:
        {
            SphereOctreeQuery octreeQuery(tempDrawables, Sphere(light->GetNode()->GetWorldPosition(), light->GetRange()),
                DRAWABLE_GEOMETRY, camera_->GetViewMask());
            octree_->GetDrawables(octreeQuery);
            for (unsigned i = 0; i < tempDrawables.Size(); ++i)
            {
                if (tempDrawables[i]->IsInView(frame_) && (GetLightMask(tempDrawables[i]) & light->GetLightMask()))
                    query.litGeometries_.Push(tempDrawables[i]);
            }
        }
        break;
    }
    
    // If no lit geometries or not shadowed, no need to process shadow cameras
    if (query.litGeometries_.Empty() || !isShadowed)
    {
        query.numSplits_ = 0;
        return;
    }
    
    // Determine number of shadow cameras and setup their initial positions
    SetupShadowCameras(query);
    
    // Process each split for shadow casters
    query.shadowCasters_.Clear();
    for (unsigned i = 0; i < query.numSplits_; ++i)
    {
        Camera* shadowCamera = query.shadowCameras_[i];
        const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
        query.shadowCasterBegin_[i] = query.shadowCasterEnd_[i] = query.shadowCasters_.Size();
        
        // For point light check that the face is visible: if not, can skip the split
        if (type == LIGHT_POINT && frustum.IsInsideFast(BoundingBox(shadowCameraFrustum)) == OUTSIDE)
            continue;
        
        // For directional light check that the split is inside the visible scene: if not, can skip the split
        if (type == LIGHT_DIRECTIONAL)
        {
            if (minZ_ > query.shadowFarSplits_[i])
                continue;
            if (maxZ_ < query.shadowNearSplits_[i])
                continue;
        
            // Reuse lit geometry query for all except directional lights
            ShadowCasterOctreeQuery query(tempDrawables, shadowCameraFrustum, DRAWABLE_GEOMETRY,
                camera_->GetViewMask());
            octree_->GetDrawables(query);
        }
        
        // Check which shadow casters actually contribute to the shadowing
        ProcessShadowCasters(query, tempDrawables, i);
    }
    
    // If no shadow casters, the light can be rendered unshadowed. At this point we have not allocated a shadow map yet, so the
    // only cost has been the shadow camera setup & queries
    if (query.shadowCasters_.Empty())
        query.numSplits_ = 0;
}

void View::ProcessShadowCasters(LightQueryResult& query, const PODVector<Drawable*>& drawables, unsigned splitIndex)
{
    Light* light = query.light_;
    
    Camera* shadowCamera = query.shadowCameras_[splitIndex];
    const Frustum& shadowCameraFrustum = shadowCamera->GetFrustum();
    const Matrix3x4& lightView = shadowCamera->GetInverseWorldTransform();
    const Matrix4& lightProj = shadowCamera->GetProjection();
    LightType type = light->GetLightType();
    
    query.shadowCasterBox_[splitIndex].defined_ = false;
    
    // Transform scene frustum into shadow camera's view space for shadow caster visibility check. For point & spot lights,
    // we can use the whole scene frustum. For directional lights, use the intersection of the scene frustum and the split
    // frustum, so that shadow casters do not get rendered into unnecessary splits
    Frustum lightViewFrustum;
    if (type != LIGHT_DIRECTIONAL)
        lightViewFrustum = camera_->GetSplitFrustum(minZ_, maxZ_).Transformed(lightView);
    else
        lightViewFrustum = camera_->GetSplitFrustum(Max(minZ_, query.shadowNearSplits_[splitIndex]),
            Min(maxZ_, query.shadowFarSplits_[splitIndex])).Transformed(lightView);
    
    BoundingBox lightViewFrustumBox(lightViewFrustum);
     
    // Check for degenerate split frustum: in that case there is no need to get shadow casters
    if (lightViewFrustum.vertices_[0] == lightViewFrustum.vertices_[4])
        return;
    
    BoundingBox lightViewBox;
    BoundingBox lightProjBox;
    
    for (PODVector<Drawable*>::ConstIterator i = drawables.Begin(); i != drawables.End(); ++i)
    {
        Drawable* drawable = *i;
        // In case this is a point or spot light query result reused for optimization, we may have non-shadowcasters included.
        // Check for that first
        if (!drawable->GetCastShadows())
            continue;
        // Check shadow mask
        if (!(GetShadowMask(drawable) & light->GetLightMask()))
            continue;
       // For point light, check that this drawable is inside the split shadow camera frustum
        if (type == LIGHT_POINT && shadowCameraFrustum.IsInsideFast(drawable->GetWorldBoundingBox()) == OUTSIDE)
            continue;
        
        // Note: as lights are processed threaded, it is possible a drawable's UpdateBatches() function is called several
        // times. However, this should not cause problems as no scene modification happens at this point.
        if (!drawable->IsInView(frame_, false))
            drawable->UpdateBatches(frame_);
        
        // Check shadow distance
        float maxShadowDistance = drawable->GetShadowDistance();
        float drawDistance = drawable->GetDrawDistance();
        if (drawDistance > 0.0f && (maxShadowDistance <= 0.0f || drawDistance < maxShadowDistance))
            maxShadowDistance = drawDistance;
        
        if (maxShadowDistance > 0.0f && drawable->GetDistance() > maxShadowDistance)
            continue;
        
        // Project shadow caster bounding box to light view space for visibility check
        lightViewBox = drawable->GetWorldBoundingBox().Transformed(lightView);
        
        if (IsShadowCasterVisible(drawable, lightViewBox, shadowCamera, lightView, lightViewFrustum, lightViewFrustumBox))
        {
            // Merge to shadow caster bounding box and add to the list
            if (type == LIGHT_DIRECTIONAL)
                query.shadowCasterBox_[splitIndex].Merge(lightViewBox);
            else
            {
                lightProjBox = lightViewBox.Projected(lightProj);
                query.shadowCasterBox_[splitIndex].Merge(lightProjBox);
            }
            query.shadowCasters_.Push(drawable);
        }
    }
    
    query.shadowCasterEnd_[splitIndex] = query.shadowCasters_.Size();
}

bool View::IsShadowCasterVisible(Drawable* drawable, BoundingBox lightViewBox, Camera* shadowCamera, const Matrix3x4& lightView,
    const Frustum& lightViewFrustum, const BoundingBox& lightViewFrustumBox)
{
    if (shadowCamera->IsOrthographic())
    {
        // Extrude the light space bounding box up to the far edge of the frustum's light space bounding box
        lightViewBox.max_.z_ = Max(lightViewBox.max_.z_,lightViewFrustumBox.max_.z_);
        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
    else
    {
        // If light is not directional, can do a simple check: if object is visible, its shadow is too
        if (drawable->IsInView(frame_))
            return true;
        
        // For perspective lights, extrusion direction depends on the position of the shadow caster
        Vector3 center = lightViewBox.Center();
        Ray extrusionRay(center, center.Normalized());
        
        float extrusionDistance = shadowCamera->GetFarClip();
        float originalDistance = Clamp(center.Length(), M_EPSILON, extrusionDistance);
        
        // Because of the perspective, the bounding box must also grow when it is extruded to the distance
        float sizeFactor = extrusionDistance / originalDistance;
        
        // Calculate the endpoint box and merge it to the original. Because it's axis-aligned, it will be larger
        // than necessary, so the test will be conservative
        Vector3 newCenter = extrusionDistance * extrusionRay.direction_;
        Vector3 newHalfSize = lightViewBox.Size() * sizeFactor * 0.5f;
        BoundingBox extrudedBox(newCenter - newHalfSize, newCenter + newHalfSize);
        lightViewBox.Merge(extrudedBox);
        
        return lightViewFrustum.IsInsideFast(lightViewBox) != OUTSIDE;
    }
}

IntRect View::GetShadowMapViewport(Light* light, unsigned splitIndex, Texture2D* shadowMap)
{
    unsigned width = shadowMap->GetWidth();
    unsigned height = shadowMap->GetHeight();
    int maxCascades = renderer_->GetMaxShadowCascades();
    
    switch (light->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
        if (maxCascades == 1)
            return IntRect(0, 0, width, height);
        else if (maxCascades == 2)
            return IntRect(splitIndex * width / 2, 0, (splitIndex + 1) * width / 2, height);
        else
            return IntRect((splitIndex & 1) * width / 2, (splitIndex / 2) * height / 2, ((splitIndex & 1) + 1) * width / 2,
                (splitIndex / 2 + 1) * height / 2);
        
    case LIGHT_SPOT:
        return IntRect(0, 0, width, height);
        
    case LIGHT_POINT:
        return IntRect((splitIndex & 1) * width / 2, (splitIndex / 2) * height / 3, ((splitIndex & 1) + 1) * width / 2,
            (splitIndex / 2 + 1) * height / 3);
    }
    
    return IntRect();
}

void View::SetupShadowCameras(LightQueryResult& query)
{
    Light* light = query.light_;
    
    int splits = 0;
    
    switch (light->GetLightType())
    {
    case LIGHT_DIRECTIONAL:
        {
            const CascadeParameters& cascade = light->GetShadowCascade();
            
            float nearSplit = camera_->GetNearClip();
            float farSplit;
            
            while (splits < renderer_->GetMaxShadowCascades())
            {
                // If split is completely beyond camera far clip, we are done
                if (nearSplit > camera_->GetFarClip())
                    break;
                
                farSplit = Min(camera_->GetFarClip(), cascade.splits_[splits]);
                if (farSplit <= nearSplit)
                    break;
                
                // Setup the shadow camera for the split
                Camera* shadowCamera = renderer_->GetShadowCamera();
                query.shadowCameras_[splits] = shadowCamera;
                query.shadowNearSplits_[splits] = nearSplit;
                query.shadowFarSplits_[splits] = farSplit;
                SetupDirLightShadowCamera(shadowCamera, light, nearSplit, farSplit);
                
                nearSplit = farSplit;
                ++splits;
            }
        }
        break;
    
    case LIGHT_SPOT:
        {
            Camera* shadowCamera = renderer_->GetShadowCamera();
            query.shadowCameras_[0] = shadowCamera;
            Node* cameraNode = shadowCamera->GetNode();
            Node* lightNode = light->GetNode();
            
            cameraNode->SetTransform(lightNode->GetWorldPosition(), lightNode->GetWorldRotation());
            shadowCamera->SetNearClip(light->GetShadowNearFarRatio() * light->GetRange());
            shadowCamera->SetFarClip(light->GetRange());
            shadowCamera->SetFov(light->GetFov());
            shadowCamera->SetAspectRatio(light->GetAspectRatio());
            
            splits = 1;
        }
        break;
    
    case LIGHT_POINT:
        {
            for (unsigned i = 0; i < MAX_CUBEMAP_FACES; ++i)
            {
                Camera* shadowCamera = renderer_->GetShadowCamera();
                query.shadowCameras_[i] = shadowCamera;
                Node* cameraNode = shadowCamera->GetNode();
                
                // When making a shadowed point light, align the splits along X, Y and Z axes regardless of light rotation
                cameraNode->SetPosition(light->GetNode()->GetWorldPosition());
                cameraNode->SetDirection(*directions[i]);
                shadowCamera->SetNearClip(light->GetShadowNearFarRatio() * light->GetRange());
                shadowCamera->SetFarClip(light->GetRange());
                shadowCamera->SetFov(90.0f);
                shadowCamera->SetAspectRatio(1.0f);
            }
            
            splits = MAX_CUBEMAP_FACES;
        }
        break;
    }
    
    query.numSplits_ = splits;
}

void View::SetupDirLightShadowCamera(Camera* shadowCamera, Light* light, float nearSplit, float farSplit)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    Node* lightNode = light->GetNode();
    float extrusionDistance = camera_->GetFarClip();
    const FocusParameters& parameters = light->GetShadowFocus();
    
    // Calculate initial position & rotation
    Vector3 pos = cameraNode_->GetWorldPosition() - extrusionDistance * lightNode->GetWorldDirection();
    shadowCameraNode->SetTransform(pos, lightNode->GetWorldRotation());
    
    // Calculate main camera shadowed frustum in light's view space
    farSplit = Min(farSplit, camera_->GetFarClip());
    // Use the scene Z bounds to limit frustum size if applicable
    if (parameters.focus_)
    {
        nearSplit = Max(minZ_, nearSplit);
        farSplit = Min(maxZ_, farSplit);
    }
    
    Frustum splitFrustum = camera_->GetSplitFrustum(nearSplit, farSplit);
    Polyhedron frustumVolume;
    frustumVolume.Define(splitFrustum);
    // If focusing enabled, clip the frustum volume by the combined bounding box of the lit geometries within the frustum
    if (parameters.focus_)
    {
        BoundingBox litGeometriesBox;
        for (unsigned i = 0; i < geometries_.Size(); ++i)
        {
            Drawable* drawable = geometries_[i];
            
            // Skip skyboxes as they have undefinedly large bounding box size
            if (drawable->GetType() == Skybox::GetTypeStatic())
                continue;
            
            if (drawable->GetMinZ() <= farSplit && drawable->GetMaxZ() >= nearSplit &&
                (GetLightMask(drawable) & light->GetLightMask()))
                litGeometriesBox.Merge(drawable->GetWorldBoundingBox());
        }
        if (litGeometriesBox.defined_)
        {
            frustumVolume.Clip(litGeometriesBox);
            // If volume became empty, restore it to avoid zero size
            if (frustumVolume.Empty())
                frustumVolume.Define(splitFrustum);
        }
    }
    
    // Transform frustum volume to light space
    const Matrix3x4& lightView = shadowCamera->GetInverseWorldTransform();
    frustumVolume.Transform(lightView);
    
    // Fit the frustum volume inside a bounding box. If uniform size, use a sphere instead
    BoundingBox shadowBox;
    if (!parameters.nonUniform_)
        shadowBox.Define(Sphere(frustumVolume));
    else
        shadowBox.Define(frustumVolume);
    
    shadowCamera->SetOrthographic(true);
    shadowCamera->SetAspectRatio(1.0f);
    shadowCamera->SetNearClip(0.0f);
    shadowCamera->SetFarClip(shadowBox.max_.z_);
    
    // Center shadow camera on the bounding box. Can not snap to texels yet as the shadow map viewport is unknown
    QuantizeDirLightShadowCamera(shadowCamera, light, IntRect(0, 0, 0, 0), shadowBox);
}

void View::FinalizeShadowCamera(Camera* shadowCamera, Light* light, const IntRect& shadowViewport,
    const BoundingBox& shadowCasterBox)
{
    const FocusParameters& parameters = light->GetShadowFocus();
    float shadowMapWidth = (float)(shadowViewport.Width());
    LightType type = light->GetLightType();
    
    if (type == LIGHT_DIRECTIONAL)
    {
        BoundingBox shadowBox;
        shadowBox.max_.y_ = shadowCamera->GetOrthoSize() * 0.5f;
        shadowBox.max_.x_ = shadowCamera->GetAspectRatio() * shadowBox.max_.y_;
        shadowBox.min_.y_ = -shadowBox.max_.y_;
        shadowBox.min_.x_ = -shadowBox.max_.x_;
        
        // Requantize and snap to shadow map texels
        QuantizeDirLightShadowCamera(shadowCamera, light, shadowViewport, shadowBox);
    }
    
    if (type == LIGHT_SPOT)
    {
        if (parameters.focus_)
        {
            float viewSizeX = Max(Abs(shadowCasterBox.min_.x_), Abs(shadowCasterBox.max_.x_));
            float viewSizeY = Max(Abs(shadowCasterBox.min_.y_), Abs(shadowCasterBox.max_.y_));
            float viewSize = Max(viewSizeX, viewSizeY);
            // Scale the quantization parameters, because view size is in projection space (-1.0 - 1.0)
            float invOrthoSize = 1.0f / shadowCamera->GetOrthoSize();
            float quantize = parameters.quantize_ * invOrthoSize;
            float minView = parameters.minView_ * invOrthoSize;
            
            viewSize = Max(ceilf(viewSize / quantize) * quantize, minView);
            if (viewSize < 1.0f)
                shadowCamera->SetZoom(1.0f / viewSize);
        }
    }
    
    // Perform a finalization step for all lights: ensure zoom out of 2 pixels to eliminate border filtering issues
    // For point lights use 4 pixels, as they must not cross sides of the virtual cube map (maximum 3x3 PCF)
    if (shadowCamera->GetZoom() >= 1.0f)
    {
        if (light->GetLightType() != LIGHT_POINT)
            shadowCamera->SetZoom(shadowCamera->GetZoom() * ((shadowMapWidth - 2.0f) / shadowMapWidth));
        else
        {
            #ifdef USE_OPENGL
                shadowCamera->SetZoom(shadowCamera->GetZoom() * ((shadowMapWidth - 3.0f) / shadowMapWidth));
            #else
                shadowCamera->SetZoom(shadowCamera->GetZoom() * ((shadowMapWidth - 4.0f) / shadowMapWidth));
            #endif
        }
    }
}

void View::QuantizeDirLightShadowCamera(Camera* shadowCamera, Light* light, const IntRect& shadowViewport,
    const BoundingBox& viewBox)
{
    Node* shadowCameraNode = shadowCamera->GetNode();
    const FocusParameters& parameters = light->GetShadowFocus();
    float shadowMapWidth = (float)(shadowViewport.Width());
    
    float minX = viewBox.min_.x_;
    float minY = viewBox.min_.y_;
    float maxX = viewBox.max_.x_;
    float maxY = viewBox.max_.y_;
    
    Vector2 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    Vector2 viewSize(maxX - minX, maxY - minY);
    
    // Quantize size to reduce swimming
    // Note: if size is uniform and there is no focusing, quantization is unnecessary
    if (parameters.nonUniform_)
    {
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.y_ = ceilf(sqrtf(viewSize.y_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = Max(viewSize.y_ * viewSize.y_ * parameters.quantize_, parameters.minView_);
    }
    else if (parameters.focus_)
    {
        viewSize.x_ = Max(viewSize.x_, viewSize.y_);
        viewSize.x_ = ceilf(sqrtf(viewSize.x_ / parameters.quantize_));
        viewSize.x_ = Max(viewSize.x_ * viewSize.x_ * parameters.quantize_, parameters.minView_);
        viewSize.y_ = viewSize.x_;
    }
    
    shadowCamera->SetOrthoSize(viewSize);
    
    // Center shadow camera to the view space bounding box
    Quaternion rot(shadowCameraNode->GetWorldRotation());
    Vector3 adjust(center.x_, center.y_, 0.0f);
    shadowCameraNode->Translate(rot * adjust);
    
    // If the shadow map viewport is known, snap to whole texels
    if (shadowMapWidth > 0.0f)
    {
        Vector3 viewPos(rot.Inverse() * shadowCameraNode->GetWorldPosition());
        // Take into account that shadow map border will not be used
        float invActualSize = 1.0f / (shadowMapWidth - 2.0f);
        Vector2 texelSize(viewSize.x_ * invActualSize, viewSize.y_ * invActualSize);
        Vector3 snap(-fmodf(viewPos.x_, texelSize.x_), -fmodf(viewPos.y_, texelSize.y_), 0.0f);
        shadowCameraNode->Translate(rot * snap);
    }
}

void View::FindZone(Drawable* drawable)
{
    Vector3 center = drawable->GetWorldBoundingBox().Center();
    int bestPriority = M_MIN_INT;
    Zone* newZone = 0;
    
    // If bounding box center is in view, the zone assignment is conclusive also for next frames. Otherwise it is temporary
    // (possibly incorrect) and must be re-evaluated on the next frame
    bool temporary = !camera_->GetFrustum().IsInside(center);
    
    // First check if the last zone remains a conclusive result
    Zone* lastZone = drawable->GetLastZone();
    
    if (lastZone && (lastZone->GetViewMask() & camera_->GetViewMask()) && lastZone->GetPriority() >= highestZonePriority_ &&
        (drawable->GetZoneMask() & lastZone->GetZoneMask()) && lastZone->IsInside(center))
        newZone = lastZone;
    else
    {
        for (PODVector<Zone*>::Iterator i = zones_.Begin(); i != zones_.End(); ++i)
        {
            Zone* zone = *i;
            int priority = zone->GetPriority();
            if (priority > bestPriority && (drawable->GetZoneMask() & zone->GetZoneMask()) && zone->IsInside(center))
            {
                newZone = zone;
                bestPriority = priority;
            }
        }
    }
    
    drawable->SetZone(newZone, temporary);
}

Zone* View::GetZone(Drawable* drawable)
{
    if (cameraZoneOverride_)
        return cameraZone_;
    Zone* drawableZone = drawable->GetZone();
    return drawableZone ? drawableZone : cameraZone_;
}

unsigned View::GetLightMask(Drawable* drawable)
{
    return drawable->GetLightMask() & GetZone(drawable)->GetLightMask();
}

unsigned View::GetShadowMask(Drawable* drawable)
{
    return drawable->GetShadowMask() & GetZone(drawable)->GetShadowMask();
}

unsigned long long View::GetVertexLightQueueHash(const PODVector<Light*>& vertexLights)
{
    unsigned long long hash = 0;
    for (PODVector<Light*>::ConstIterator i = vertexLights.Begin(); i != vertexLights.End(); ++i)
        hash += (unsigned long long)(*i);
    return hash;
}

Technique* View::GetTechnique(Drawable* drawable, Material* material)
{
    if (!material)
    {
        const Vector<TechniqueEntry>& techniques = renderer_->GetDefaultMaterial()->GetTechniques();
        return techniques.Size() ? techniques[0].technique_ : (Technique*)0;
    }
    
    const Vector<TechniqueEntry>& techniques = material->GetTechniques();
    // If only one technique, no choice
    if (techniques.Size() == 1)
        return techniques[0].technique_;
    else
    {
        float lodDistance = drawable->GetLodDistance();
        
        // Check for suitable technique. Techniques should be ordered like this:
        // Most distant & highest quality
        // Most distant & lowest quality
        // Second most distant & highest quality
        // ...
        for (unsigned i = 0; i < techniques.Size(); ++i)
        {
            const TechniqueEntry& entry = techniques[i];
            Technique* tech = entry.technique_;
            if (!tech || (tech->IsSM3() && !graphics_->GetSM3Support()) || materialQuality_ < entry.qualityLevel_)
                continue;
            if (lodDistance >= entry.lodDistance_)
                return tech;
        }
        
        // If no suitable technique found, fallback to the last
        return techniques.Size() ? techniques.Back().technique_ : (Technique*)0;
    }
}

void View::CheckMaterialForAuxView(Material* material)
{
    const SharedPtr<Texture>* textures = material->GetTextures();
    
    for (unsigned i = 0; i < MAX_MATERIAL_TEXTURE_UNITS; ++i)
    {
        Texture* texture = textures[i];
        if (texture && texture->GetUsage() == TEXTURE_RENDERTARGET)
        {
            // Have to check cube & 2D textures separately
            if (texture->GetType() == Texture2D::GetTypeStatic())
            {
                Texture2D* tex2D = static_cast<Texture2D*>(texture);
                RenderSurface* target = tex2D->GetRenderSurface();
                if (target && target->GetUpdateMode() == SURFACE_UPDATEVISIBLE)
                    target->QueueUpdate();
            }
            else if (texture->GetType() == TextureCube::GetTypeStatic())
            {
                TextureCube* texCube = static_cast<TextureCube*>(texture);
                for (unsigned j = 0; j < MAX_CUBEMAP_FACES; ++j)
                {
                    RenderSurface* target = texCube->GetRenderSurface((CubeMapFace)j);
                    if (target && target->GetUpdateMode() == SURFACE_UPDATEVISIBLE)
                        target->QueueUpdate();
                }
            }
        }
    }
    
    // Flag as processed so we can early-out next time we come across this material on the same frame
    material->MarkForAuxView(frame_.frameNumber_);
}

void View::AddBatchToQueue(BatchQueue& batchQueue, Batch& batch, Technique* tech, bool allowInstancing, bool allowShadows)
{
    if (!batch.material_)
        batch.material_ = renderer_->GetDefaultMaterial();
    
    // Convert to instanced if possible
    if (allowInstancing && batch.geometryType_ == GEOM_STATIC && batch.geometry_->GetIndexBuffer() && !batch.shaderData_ &&
        !batch.overrideView_)
        batch.geometryType_ = GEOM_INSTANCED;
    
    if (batch.geometryType_ == GEOM_INSTANCED)
    {
        HashMap<BatchGroupKey, BatchGroup>* groups = batch.isBase_ ? &batchQueue.baseBatchGroups_ : &batchQueue.batchGroups_;
        BatchGroupKey key(batch);
        
        HashMap<BatchGroupKey, BatchGroup>::Iterator i = groups->Find(key);
        if (i == groups->End())
        {
            // Create a new group based on the batch
            renderer_->SetBatchShaders(batch, tech, allowShadows);
            BatchGroup newGroup(batch);
            newGroup.CalculateSortKey();
            newGroup.instances_.Push(InstanceData(batch.worldTransform_, batch.distance_));
            groups->Insert(MakePair(key, newGroup));
        }
        else
            i->second_.instances_.Push(InstanceData(batch.worldTransform_, batch.distance_));
    }
    else
    {
        renderer_->SetBatchShaders(batch, tech, allowShadows);
        batch.CalculateSortKey();
        batchQueue.batches_.Push(batch);
    }
}

void View::PrepareInstancingBuffer()
{
    PROFILE(PrepareInstancingBuffer);
    
    unsigned totalInstances = 0;
    
    for (HashMap<StringHash, BatchQueue>::Iterator i = batchQueues_.Begin(); i != batchQueues_.End(); ++i)
        totalInstances += i->second_.GetNumInstances();
    
    for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
    {
        for (unsigned j = 0; j < i->shadowSplits_.Size(); ++j)
            totalInstances += i->shadowSplits_[j].shadowBatches_.GetNumInstances();
        totalInstances += i->litBatches_.GetNumInstances();
    }
    
    // If fail to set buffer size, fall back to per-group locking
    if (totalInstances && renderer_->ResizeInstancingBuffer(totalInstances))
    {
        VertexBuffer* instancingBuffer = renderer_->GetInstancingBuffer();
        unsigned freeIndex = 0;
        void* dest = instancingBuffer->Lock(0, totalInstances, true);
        if (!dest)
            return;
        
        for (HashMap<StringHash, BatchQueue>::Iterator i = batchQueues_.Begin(); i != batchQueues_.End(); ++i)
            i->second_.SetTransforms(this, dest, freeIndex);
        
        for (Vector<LightBatchQueue>::Iterator i = lightQueues_.Begin(); i != lightQueues_.End(); ++i)
        {
            for (unsigned j = 0; j < i->shadowSplits_.Size(); ++j)
                i->shadowSplits_[j].shadowBatches_.SetTransforms(this, dest, freeIndex);
            i->litBatches_.SetTransforms(this, dest, freeIndex);
        }
        
        instancingBuffer->Unlock();
    }
}

void View::SetupLightVolumeBatch(Batch& batch)
{
    Light* light = batch.lightQueue_->light_;
    LightType type = light->GetLightType();
    Vector3 cameraPos = cameraNode_->GetWorldPosition();
    float lightDist;
    
    graphics_->SetBlendMode(BLEND_ADD);
    graphics_->SetDepthBias(0.0f, 0.0f);
    graphics_->SetDepthWrite(false);
    graphics_->SetFillMode(FILL_SOLID);
    
    if (type != LIGHT_DIRECTIONAL)
    {
        if (type == LIGHT_POINT)
            lightDist = Sphere(light->GetNode()->GetWorldPosition(), light->GetRange() * 1.25f).Distance(cameraPos);
        else
            lightDist = light->GetFrustum().Distance(cameraPos);
        
        // Draw front faces if not inside light volume
        if (lightDist < camera_->GetNearClip() * 2.0f)
        {
            renderer_->SetCullMode(CULL_CW, camera_);
            graphics_->SetDepthTest(CMP_GREATER);
        }
        else
        {
            renderer_->SetCullMode(CULL_CCW, camera_);
            graphics_->SetDepthTest(CMP_LESSEQUAL);
        }
    }
    else
    {
        // In case the same camera is used for multiple views with differing aspect ratios (not recommended)
        // refresh the directional light's model transform before rendering
        light->GetVolumeTransform(camera_);
        graphics_->SetCullMode(CULL_NONE);
        graphics_->SetDepthTest(CMP_ALWAYS);
    }
    
    graphics_->SetScissorTest(false);
    graphics_->SetStencilTest(true, CMP_NOTEQUAL, OP_KEEP, OP_KEEP, OP_KEEP, 0, light->GetLightMask());
}

void View::RenderShadowMap(const LightBatchQueue& queue)
{
    PROFILE(RenderShadowMap);
    
    Texture2D* shadowMap = queue.shadowMap_;
    graphics_->SetTexture(TU_SHADOWMAP, 0);
    
    graphics_->SetColorWrite(false);
    graphics_->SetFillMode(FILL_SOLID);
    graphics_->SetStencilTest(false);
    graphics_->SetRenderTarget(0, shadowMap->GetRenderSurface()->GetLinkedRenderTarget());
    graphics_->SetDepthStencil(shadowMap);
    graphics_->SetViewport(IntRect(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight()));
    graphics_->Clear(CLEAR_DEPTH);
    
    // Set shadow depth bias
    const BiasParameters& parameters = queue.light_->GetShadowBias();
    
    // Render each of the splits
    for (unsigned i = 0; i < queue.shadowSplits_.Size(); ++i)
    {
        float multiplier = 1.0f;
        // For directional light cascade splits, adjust depth bias according to the far clip ratio of the splits
        if (i > 0 && queue.light_->GetLightType() == LIGHT_DIRECTIONAL)
        {
            multiplier = Max(queue.shadowSplits_[i].shadowCamera_->GetFarClip() / queue.shadowSplits_[0].shadowCamera_->GetFarClip(), 1.0f);
            multiplier = 1.0f + (multiplier - 1.0f) * queue.light_->GetShadowCascade().biasAutoAdjust_;
        }
        
        graphics_->SetDepthBias(multiplier * parameters.constantBias_, multiplier * parameters.slopeScaledBias_);
        
        const ShadowBatchQueue& shadowQueue = queue.shadowSplits_[i];
        if (!shadowQueue.shadowBatches_.IsEmpty())
        {
            graphics_->SetViewport(shadowQueue.shadowViewport_);
            shadowQueue.shadowBatches_.Draw(this);
        }
    }
    
    graphics_->SetColorWrite(true);
    graphics_->SetDepthBias(0.0f, 0.0f);
}

RenderSurface* View::GetDepthStencil(RenderSurface* renderTarget)
{
    // If using the backbuffer, return the backbuffer depth-stencil
    if (!renderTarget)
        return 0;
    // Then check for linked depth-stencil
    RenderSurface* depthStencil = renderTarget->GetLinkedDepthStencil();
    // Finally get one from Renderer
    if (!depthStencil)
        depthStencil = renderer_->GetDepthStencil(renderTarget->GetWidth(), renderTarget->GetHeight());
    return depthStencil;
}

}
