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

#include "Sample.h"
#include "Scene.h"

// All Urho3D classes reside in namespace Urho3D
using namespace Urho3D;

/// Billboard example.
/// This sample demonstrates:
///     - Populating a 3D scene with billboard sets and several shadow casting spotlights;
///     - Parenting scene nodes to allow more intuitive creation of groups of objects;
///     - Examining rendering performance with a somewhat large object and light count;
class Billboards : public Sample
{
    // Mandatory when deriving from Object, enables type information
    OBJECT(Billboards)

public:
    /// Construct.
    Billboards(Context* context);

    /// Setup after engine initialization and before running the main loop.
    virtual void Start();

private:
    /// Constructs the scene content.
    void CreateScene();
    /// Constructs an instruction text to the UI.
    void CreateInstructions();
    /// Sets up a viewport for displaying the scene.
    void SetupViewport();
    /// Reads input and moves the camera.
    void MoveCamera(float timeStep);
    /// Animates the scene.
    void AnimateScene(float timeStep);
    /// Subscribes to application-wide logic update events.
    void SubscribeToEvents();
    /// Callback method invoked when a logic update event is dispatched.
    void HandleUpdate(StringHash eventType, VariantMap& eventData);
    /// Callback method invoked when the post-render update event is dispatched.
    void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData);
    
    /// Scene.
    SharedPtr<Scene> scene_;
    /// Camera scene node.
    SharedPtr<Node> cameraNode_;
    /// Camera yaw angle.
    float yaw_;
    /// Camera pitch angle.
    float pitch_;
    /// Flag for drawing debug geometry.
    bool drawDebug_;
};
