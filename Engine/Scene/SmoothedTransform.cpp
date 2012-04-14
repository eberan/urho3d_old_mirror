//
// Urho3D Engine
// Copyright (c) 2008-2012 Lasse ��rni
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
#include "Scene.h"
#include "SceneEvents.h"
#include "SmoothedTransform.h"

static const float DEFAULT_SMOOTHING_CONSTANT = 50.0f;
static const float DEFAULT_SNAP_THRESHOLD = 5.0f;

OBJECTTYPESTATIC(SmoothedTransform);

SmoothedTransform::SmoothedTransform(Context* context) :
    Component(context),
    targetPosition_(Vector3::ZERO),
    targetRotation_(Quaternion::IDENTITY),
    smoothingConstant_(DEFAULT_SMOOTHING_CONSTANT),
    snapThreshold_(DEFAULT_SNAP_THRESHOLD),
    smoothingMask_(SMOOTH_NONE)
{
}

SmoothedTransform::~SmoothedTransform()
{
}

void SmoothedTransform::RegisterObject(Context* context)
{
    context->RegisterFactory<SmoothedTransform>();
}

void SmoothedTransform::Update(float constant, float squaredSnapThreshold)
{
    if (smoothingMask_ && node_)
    {
        Vector3 position = node_->GetPosition();
        Quaternion rotation = node_->GetRotation();
        
        if (smoothingMask_ & SMOOTH_POSITION)
        {
            // If position snaps, snap everything to the end
            float delta = (position - targetPosition_).LengthSquared();
            if (delta > squaredSnapThreshold)
                constant = 1.0f;
            
            if (delta < M_EPSILON || constant >= 1.0f)
            {
                position = targetPosition_;
                smoothingMask_ &= ~SMOOTH_POSITION;
            }
            else
                position = position.Lerp(targetPosition_, constant);
            
            node_->SetPosition(position);
        }
        
        if (smoothingMask_ & SMOOTH_ROTATION)
        {
            float delta = (rotation - targetRotation_).LengthSquared();
            if (delta < M_EPSILON || constant >= 1.0f)
            {
                rotation = targetRotation_;
                smoothingMask_ &= ~SMOOTH_ROTATION;
            }
            else
                rotation = rotation.Slerp(targetRotation_, constant);
            
            node_->SetRotation(rotation);
        }
    }
}

void SmoothedTransform::SetTargetPosition(const Vector3& position)
{
    targetPosition_ = position;
    smoothingMask_ |= SMOOTH_POSITION;
}

void SmoothedTransform::SetTargetRotation(const Quaternion& rotation)
{
    targetRotation_ = rotation;
    smoothingMask_ |= SMOOTH_ROTATION;
}

void SmoothedTransform::OnNodeSet(Node* node)
{
    if (node)
    {
        // Copy initial target transform
        targetPosition_ = node->GetPosition();
        targetRotation_ = node->GetRotation();
        
        // Subscribe to smoothing update
        Scene* scene = node_->GetScene();
        if (scene)
            SubscribeToEvent(scene, E_UPDATESMOOTHING, HANDLER(SmoothedTransform, HandleUpdateSmoothing));
    }
}

void SmoothedTransform::HandleUpdateSmoothing(StringHash eventType, VariantMap& eventData)
{
    using namespace UpdateSmoothing;
    
    float constant = eventData[P_CONSTANT].GetFloat();
    float squaredSnapThreshold = eventData[P_SQUAREDSNAPTHRESHOLD].GetFloat();
    Update(constant, squaredSnapThreshold);
}
