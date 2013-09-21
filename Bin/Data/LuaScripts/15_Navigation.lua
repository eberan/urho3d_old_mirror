-- Navigation example.
-- This sample demonstrates:
--     - Generating a navigation mesh into the scene
--     - Performing path queries to the navigation mesh
--     - Rebuilding the navigation mesh partially when adding or removing objects
--     - Visualizing custom debug geometry

require "LuaScripts/Utilities/Sample"

local scene_ = nil
local cameraNode = nil
local startPos = nil
local endPos = nil
local currentPath = nil
local yaw = 0.0
local pitch = 0.0
local drawDebug = false
local startPosDefined = false
local endPosDefined = false

local context = GetContext()

local cache = GetCache()
local input = GetInput()
local graphics = GetGraphics()
local renderer = GetRenderer()
local ui = GetUI()

function Start()
    -- Execute the common startup for samples
    SampleStart()

    -- Create the scene content
    CreateScene()
    
    -- Create the UI content
    CreateUI()
    
    -- Setup the viewport for displaying the scene
    SetupViewport()

    -- Hook up to the frame update and render post-update events
    SubscribeToEvents()
end

function CreateScene()
    scene_ = Scene(context)
    -- Create octree, use default volume (-1000, -1000, -1000) to (1000, 1000, 1000)
    -- Also create a DebugRenderer component so that we can draw debug geometry
    scene_:CreateComponent("Octree")
    scene_:CreateComponent("DebugRenderer")
    
    -- Create scene node & StaticModel component for showing a static plane
    local planeNode = scene_:CreateChild("Plane")
    planeNode.scale = Vector3(100.0, 1.0, 100.0)
    local planeObject = planeNode:CreateComponent("StaticModel")
    planeObject.model = cache:GetResource("Model", "Models/Plane.mdl")
    planeObject.material = cache:GetResource("Material", "Materials/StoneTiled.xml")
    
    -- Create a Zone component for ambient lighting & fog control
    local zoneNode = scene_:CreateChild("Zone")
    local zone = zoneNode:CreateComponent("Zone")
    zone.boundingBox = BoundingBox(-1000.0, 1000.0)
    zone.ambientColor = Color(0.15, 0.15, 0.15)
    zone.fogColor = Color(0.5, 0.5, 0.7)
    zone.fogStart = 100.0
    zone.fogEnd = 300.0

    -- Create a directional light to the world. Enable cascaded shadows on it
    local lightNode = scene_:CreateChild("DirectionalLight")
    lightNode.direction = Vector3(0.6, -1.0, 0.8)
    local light = lightNode:CreateComponent("Light")
    light.lightType = LIGHT_DIRECTIONAL
    light.castShadows = true
    light.shadowBias = BiasParameters(0.0001, 0.5)
    -- Set cascade splits at 10, 50 and 200 world units, fade shadows out at 80% of maximum shadow distance
    light.shadowCascade = CascadeParameters(10.0, 50.0, 200.0, 0.0, 0.8)

    -- Create some mushrooms
    local NUM_MUSHROOMS = 100
    for i = 1, NUM_MUSHROOMS do
        CreateMushroom(Vector3(Random(90.0) - 45.0, 0.0, Random(90.0) - 45.0))
    end
    
    -- Create randomly sized boxes. If boxes are big enough, make them occluders. Occluders will be software rasterized before
    -- rendering to a low-resolution depth-only buffer to test the objects in the view frustum for visibility
    local NUM_BOXES = 20
    for i = 1, NUM_BOXES do
        local boxNode = scene_:CreateChild("Box")
        local size = 1.0 + Random(10.0)
        boxNode.position = Vector3(Random(80.0) - 40.0, size * 0.5, Random(80.0) - 40.0)
        boxNode:SetScale(size)
        local boxObject = boxNode:CreateComponent("StaticModel")
        boxObject.model = cache:GetResource("Model", "Models/Box.mdl")
        boxObject.material = cache:GetResource("Material", "Materials/Stone.xml")
        boxObject.castShadows = true
        if size >= 3.0 then
            boxObject.occluder = true
        end
    end
    
    -- Create a NavigationMesh component to the scene root
    local navMesh = scene_:CreateComponent("NavigationMesh")
    -- Create a Navigable component to the scene root. This tags all of the geometry in the scene as being part of the
    -- navigation mesh. By default this is recursive, but the recursion could be turned off from Navigable
    scene_:CreateComponent("Navigable")
    -- Add padding to the navigation mesh in Y-direction so that we can add objects on top of the tallest boxes
    -- in the scene and still update the mesh correctly
    navMesh.padding = Vector3(0.0, 10.0, 0.0)
    -- Now build the navigation geometry. This will take some time. Note that the navigation mesh will prefer to use
    -- physics geometry from the scene nodes, as it often is simpler, but if it can not find any (like in this example)
    -- it will use renderable geometry instead
    navMesh:Build()
    
    -- Create the camera. Limit far clip distance to match the fog
    cameraNode = scene_:CreateChild("Camera")
    local camera = cameraNode:CreateComponent("Camera")
    camera.farClip = 300.0
    
    -- Set an initial position for the camera scene node above the plane
    cameraNode.position = Vector3(0.0, 5.0, 0.0)
end

function CreateUI()
    -- Create a Cursor UI element because we want to be able to hide and show it at will. When hidden, the mouse cursor will
    -- control the camera, and when visible, it will point the raycast target
    local style = cache:GetResource("XMLFile", "UI/DefaultStyle.xml")
    local cursor = Cursor:new(context)
    cursor:SetStyleAuto(style)
    ui.cursor = cursor
    -- Set starting position of the cursor at the rendering window center
    cursor:SetPosition(graphics.width / 2, graphics.height / 2)
    
    -- Construct new Text object, set string to display and font to use
    local instructionText = ui.root:CreateChild("Text")
    instructionText.text = "Use WASD keys to move, RMB to rotate view\n"..
        "Shift+LMB to set path start, LMB to set path end\n"..
        "MMB to add or remove obstacles\n"..
        "Space to toggle debug geometry"
    instructionText:SetFont(cache:GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15)
    -- The text has multiple rows. Center them in relation to each other
    instructionText.textAlignment = HA_CENTER

    -- Position the text relative to the screen center
    instructionText.horizontalAlignment = HA_CENTER
    instructionText.verticalAlignment = VA_CENTER
    instructionText:SetPosition(0, ui.root.height / 4)
end

function SetupViewport()
    -- Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
    local viewport = Viewport:new(context, scene_, cameraNode:GetComponent("Camera"))
    renderer:SetViewport(0, viewport)
end

function SubscribeToEvents()
    -- Subscribe HandleUpdate() function for processing update events
    SubscribeToEvent("Update", "HandleUpdate")

    -- Subscribe HandlePostRenderUpdate() function for processing the post-render update event, during which we request
    -- debug geometry
    SubscribeToEvent("PostRenderUpdate", "HandlePostRenderUpdate")
end

function MoveCamera(timeStep)
    -- Right mouse button controls mouse cursor visibility: hide when pressed
    ui.cursor.visible = not input:GetMouseButtonDown(MOUSEB_RIGHT)

    -- Do not move if the UI has a focused element (the console)
    if ui.focusElement ~= nil then
        return
    end
    
    -- Movement speed as world units per second
    local MOVE_SPEED = 20.0
    -- Mouse sensitivity as degrees per pixel
    local MOUSE_SENSITIVITY = 0.1
    
    -- Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch between -90 and 90 degrees
    -- Only move the camera when the cursor is hidden
    if not ui.cursor.visible then
        local mouseMove = input.mouseMove
        yaw = yaw + MOUSE_SENSITIVITY * mouseMove.x
        pitch = pitch + MOUSE_SENSITIVITY * mouseMove.y
        pitch = Clamp(pitch, -90.0, 90.0)

        -- Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
        cameraNode.rotation = Quaternion(pitch, yaw, 0.0)
    end

    -- Read WASD keys and move the camera scene node to the corresponding direction if they are pressed
    if input:GetKeyDown(KEY_W) then
        cameraNode:TranslateRelative(Vector3(0.0, 0.0, 1.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY_S) then
        cameraNode:TranslateRelative(Vector3(0.0, 0.0, -1.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY_A) then
        cameraNode:TranslateRelative(Vector3(-1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
    if input:GetKeyDown(KEY_D) then
        cameraNode:TranslateRelative(Vector3(1.0, 0.0, 0.0) * MOVE_SPEED * timeStep)
    end
    -- Set route start/endpoint with left mouse button, recalculate route if applicable
    if input:GetMouseButtonPress(MOUSEB_LEFT) then
        SetPathPoint()
    end
    -- Add or remove objects with middle mouse button, then rebuild navigation mesh partially
    if input:GetMouseButtonPress(MOUSEB_MIDDLE) then
        AddOrRemoveObject()
    end
    -- Toggle debug geometry with space
    if input:GetKeyPress(KEY_SPACE) then
        drawDebug = not drawDebug
    end
end

function SetPathPoint()
    local result, hitPos, hitDrawable = Raycast(250.0, hitPos, hitDrawable)
    if result then
        local setStart = input:GetQualifierDown(QUAL_SHIFT)
        if setStart then
            startPos = hitPos
            startPosDefined = true
        else
            endPos = hitPos
            endPosDefined = true
        end
        
        if startPosDefined and endPosDefined then
            RecalculatePath()
        end
    end
end

function AddOrRemoveObject()
    -- Raycast and check if we hit a mushroom node. If yes, remove it, if no, create a new one
    local result, hitPos, hitDrawable = Raycast(250.0, hitPos, hitDrawable)
    if result then
        -- The part of the navigation mesh we must update, which is the world bounding box of the associated
        -- drawable component
        local updateBox = nil
        
        local hitNode = hitDrawable:GetNode()
        if hitNode.name == "Mushroom" then
            updateBox = hitDrawable.worldBoundingBox
            hitNode:Remove()
        else
            local newNode = CreateMushroom(hitPos)
            local newObject = newNode:GetComponent("StaticModel")
            updateBox = newObject.worldBoundingBox
        end
        
        -- Rebuild part of the navigation mesh, then recalculate path if applicable
        local navMesh = scene_:GetComponent("NavigationMesh")
        navMesh:Build(updateBox)
        RecalculatePath()
    end
end

function CreateMushroom(pos)
    local mushroomNode = scene_:CreateChild("Mushroom")
    mushroomNode.position = pos
    mushroomNode.rotation = Quaternion(0.0, Random(360.0), 0.0)
    mushroomNode:SetScale(2.0 + Random(0.5))
    local mushroomObject = mushroomNode:CreateComponent("StaticModel")
    mushroomObject.model = cache:GetResource("Model", "Models/Mushroom.mdl")
    mushroomObject.material = cache:GetResource("Material", "Materials/Mushroom.xml")
    mushroomObject.castShadows = true
    return mushroomNode
end

function RecalculatePath()
    local navMesh = scene_:GetComponent("NavigationMesh")
    currentPath = navMesh:FindPath(startPos, endPos)
end

function Raycast(maxDistance)
    local hitPos = nil
    local hitDrawable = nil

    local pos = ui.cursorPosition
    -- Check the cursor is visible and there is no UI element in front of the cursor
    if (not ui.cursor.visible) or (ui:GetElementAt(pos, true) ~= nil) then
        return false, nil, nil
    end

    local camera = cameraNode:GetComponent("Camera")
    local cameraRay = camera:GetScreenRay(pos.x / graphics.width, pos.y / graphics.height)
    -- Pick only geometry objects, not eg. zones or lights, only get the first (closest) hit
    -- Note the convenience accessor to scene's Octree component
    local octree = scene_:GetComponent("Octree")
    local result = octree:RaycastSingle(cameraRay, RAY_TRIANGLE, maxDistance, DRAWABLE_GEOMETRY)
    if result.drawable ~= nil then
        -- Calculate hit position in world space
        hitPos = cameraRay.origin + cameraRay.direction * result.distance
        hitDrawable = result.drawable
        return true, hitPos, hitDrawable
    end
    
    return false, nil, nil
end

function HandleUpdate(eventType, eventData)
    -- Take the frame time step, which is stored as a float
    local timeStep = eventData:GetFloat("TimeStep")

    -- Move the camera, scale movement with time step
    MoveCamera(timeStep)
end

function HandlePostRenderUpdate(eventType, eventData)
    -- If draw debug mode is enabled, draw navigation mesh debug geometry
    if drawDebug then
        local navMesh = scene_:GetComponent("NavigationMesh")
        navMesh:DrawDebugGeometry(true)
    end
    
    -- Visualize the start and end points and the last calculated path
    -- Note the convenience accessor to the DebugRenderer component
    local debug = scene_:GetComponent("DebugRenderer")
    if startPosDefined then
        debug:AddBoundingBox(BoundingBox(startPos - Vector3(0.1, 0.1, 0.1), startPos + Vector3(0.1, 0.1, 0.1)), Color(1.0, 1.0, 1.0))
    end
    
    if endPosDefined then
        debug:AddBoundingBox(BoundingBox(endPos - Vector3(0.1, 0.1, 0.1), endPos + Vector3(0.1, 0.1, 0.1)), Color(1.0, 1.0, 1.0))
    end
    
    if currentPath ~= nil and currentPath:Size() > 0 then
        -- Draw the path with a small upward bias so that it does not clip into the surfaces
        local bias = Vector3(0.0, 0.05, 0.0)
        for i = 0, currentPath:Size() - 2 do
            debug:AddLine(currentPath[i] + bias, currentPath[i + 1] + bias, Color(1.0, 1.0, 1.0))
        end
    end
end