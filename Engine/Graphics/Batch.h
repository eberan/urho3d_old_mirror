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

#include "GraphicsDefs.h"
#include "HashMap.h"
#include "Map.h"
#include "MathDefs.h"
#include "Ptr.h"
#include "Vector4.h"

class Camera;
class Drawable;
class Geometry;
class Graphics;
class Light;
class Material;
class Pass;
class Matrix3x4;
class ShaderVariation;
class VertexBuffer;

/// Description of a 3D geometry draw call.
struct Batch
{
    /// Construct with defaults.
    Batch() :
        light_(0),
        shaderData_(0),
        shaderDataSize_(0),
        geometryType_(GEOM_STATIC),
        overrideView_(false),
        hasPriority_(false)
    {
    }
    
    /// Calculate sort key, which consists of priority flag, light, pass and geometry.
    void CalculateSortKey();
    /// Prepare for rendering.
    void Prepare(Graphics* graphics, const HashMap<StringHash, Vector4>& shaderParameters, bool setModelTransform = true) const;
    /// Prepare and draw.
    void Draw(Graphics* graphics, const HashMap<StringHash, Vector4>& shaderParameters) const;
    
    /// Geometry.
    Geometry* geometry_;
    /// Model world transform.
    const Matrix3x4* worldTransform_;
    /// Camera.
    Camera* camera_;
    /// Light that affects the geometry, if any.
    Light* light_;
    /// Material.
    Material* material_;
    /// Material pass.
    Pass* pass_;
    /// Vertex shader.
    ShaderVariation* vertexShader_;
    /// Pixel shader.
    ShaderVariation* pixelShader_;
    /// Vertex shader data.
    const float* shaderData_;
    /// Vertex shader data size in floats.
    unsigned shaderDataSize_;
    /// Distance from camera.
    float distance_;
    /// State sorting key.
    unsigned long long sortKey_;
    /// Geometry type.
    GeometryType geometryType_;
    /// Vertex shader index.
    unsigned char vertexShaderIndex_;
    /// Override view transform flag.
    bool overrideView_;
    /// Priority flag.
    bool hasPriority_;
};

/// Data for one geometry instance.
struct InstanceData
{
    /// Construct undefined.
    InstanceData()
    {
    }
    
    /// Construct with transform and distance.
    InstanceData(const Matrix3x4* worldTransform, float distance) :
        worldTransform_(worldTransform),
        distance_(distance)
    {
    }
    
    /// World transform.
    const Matrix3x4* worldTransform_;
    /// Distance from camera.
    float distance_;
};

/// Instanced 3D geometry draw call.
struct BatchGroup
{
    /// Construct with defaults.
    BatchGroup() :
        startIndex_(M_MAX_UNSIGNED)
    {
    }
    
    /// Destruct.
    ~BatchGroup()
    {
    }
    
    /// Pre-set the instance transforms. Buffer must be big enough to hold all transforms.
    void SetTransforms(void* lockedData, unsigned& freeIndex);
    /// Prepare and draw.
    void Draw(Graphics* graphics, VertexBuffer* instanceBuffer, const HashMap<StringHash, Vector4>& shaderParameters) const;
    
    /// Geometry.
    Geometry* geometry_;
    /// Instance data.
    PODVector<InstanceData> instances_;
    /// Camera.
    Camera* camera_;
    /// Light that affects the geometry, if any.
    Light* light_;
    /// Material.
    Material* material_;
    /// Material pass.
    Pass* pass_;
    /// Vertex shader.
    ShaderVariation* vertexShader_;
    /// Pixel shader.
    ShaderVariation* pixelShader_;
    /// Vertex shader index.
    unsigned char vertexShaderIndex_;
    /// Instance stream start index, or M_MAX_UNSIGNED if transforms not pre-set.
    unsigned startIndex_;
};

/// Instanced draw call key.
struct BatchGroupKey
{
    /// Light that affects the geometry, if any.
    Light* light_;
    /// Material pass.
    Pass* pass_;
    /// Material.
    Material* material_;
    /// Geometry.
    Geometry* geometry_;
    
    /// Test for equality with another batch group key.
    bool operator == (const BatchGroupKey& rhs) const { return light_ == rhs.light_ && pass_ == rhs.pass_ && material_ == rhs.material_ && geometry_ == rhs.geometry_; }
    /// Test for inequality with another batch group key.
    bool operator != (const BatchGroupKey& rhs) const { return light_ != rhs.light_ || pass_ != rhs.pass_ || material_ != rhs.material_ || geometry_ != rhs.geometry_; }
    
    /// Test if less than another batch group key.
    bool operator < (const BatchGroupKey& rhs) const
    {
        if (light_ == rhs.light_)
        {
            if (pass_ == rhs.pass_)
            {
                if (material_ == rhs.material_)
                    return geometry_ < rhs.geometry_;
                else
                    return material_ < rhs.material_;
            }
            else
                return pass_ < rhs.pass_;
        }
        else
            return light_ < rhs.light_;
    }
    
    /// Test if greater than another batch group key.
    bool operator > (const BatchGroupKey& rhs) const
    {
        if (light_ == rhs.light_)
        {
            if (pass_ == rhs.pass_)
            {
                if (material_ == rhs.material_)
                    return geometry_ > rhs.geometry_;
                else
                    return material_ > rhs.material_;
            }
            else
                return pass_ > rhs.pass_;
        }
        else
            return light_ > rhs.light_;
    }
};

/// Queue that contains both instanced and non-instanced draw calls.
struct BatchQueue
{
public:
    /// Clear everything.
    void Clear();
    /// Add a batch, with instancing if possible.
    void AddBatch(const Batch& batch, bool noInstancing = false);
    /// Sort non-instanced draw calls back to front.
    void SortBackToFront();
    /// Sort instanced and non-instanced draw calls front to back.
    void SortFrontToBack();
    /// Pre-set instance transforms of all groups. The vertex buffer must be big enough to hold all transforms.
    void SetTransforms(void* lockedData, unsigned& freeIndex);
    
    /// Return the combined amount of instances.
    unsigned GetNumInstances() const;
    /// Return whether the batch group is empty.
    bool IsEmpty() const { return batches_.Empty() && priorityBatchGroups_.Empty() && batchGroups_.Empty(); }
    /// Unsorted non-instanced draw calls.
    PODVector<Batch> batches_;
    /// Sorted non-instanced draw calls with priority flag.
    PODVector<Batch*> sortedPriorityBatches_;
    /// Sorted non-instanced draw calls.
    PODVector<Batch*> sortedBatches_;
    /// Instanced draw calls with priority flag.
    Map<BatchGroupKey, BatchGroup> priorityBatchGroups_;
    /// Instanced draw calls.
    Map<BatchGroupKey, BatchGroup> batchGroups_;
};

/// Queue for light related draw calls
struct LightBatchQueue
{
    /// Light drawable.
    Light* light_;
    /// Shadowcaster draw calls.
    BatchQueue shadowBatches_;
    /// Lit geometry draw calls.
    BatchQueue litBatches_;
    /// Light volume draw calls, should be only one.
    PODVector<Batch> volumeBatches_;
    /// First split flag for clearing the stencil buffer.
    bool firstSplit_;
};
