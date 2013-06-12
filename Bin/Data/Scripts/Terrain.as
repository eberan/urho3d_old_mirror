#include "Scripts/Utilities/Network.as"

Scene@ testScene;
Camera@ camera;
Node@ cameraNode;

float yaw = 0.0;
float pitch = 0.0;
int drawDebug = 0;

void Start()
{
    if (!engine.headless)
    {
        InitConsole();
        InitUI();
    }
    else
        OpenConsoleWindow();

    ParseNetworkArguments();

    InitScene();

    SubscribeToEvent("Update", "HandleUpdate");
    SubscribeToEvent("KeyDown", "HandleKeyDown");
    SubscribeToEvent("MouseMove", "HandleMouseMove");
    SubscribeToEvent("MouseButtonDown", "HandleMouseButtonDown");
    SubscribeToEvent("MouseButtonUp", "HandleMouseButtonUp");
    SubscribeToEvent("PostRenderUpdate", "HandlePostRenderUpdate");
    SubscribeToEvent("SpawnBox", "HandleSpawnBox");

    network.RegisterRemoteEvent("SpawnBox");

    if (runServer)
    {
        network.StartServer(serverPort);
        SubscribeToEvent("ClientConnected", "HandleClientConnected");

        // Disable physics interpolation to ensure clients get sent physically correct transforms
        testScene.physicsWorld.interpolation = false;
    }
    if (runClient)
    {
        network.Connect(serverAddress, serverPort, testScene);
    }
}

void InitConsole()
{
    XMLFile@ uiStyle = cache.GetResource("XMLFile", "UI/DefaultStyle.xml");

    engine.CreateDebugHud();
    debugHud.defaultStyle = uiStyle;
    debugHud.mode = DEBUGHUD_SHOW_ALL;

    engine.CreateConsole();
    console.defaultStyle = uiStyle;
}

void InitUI()
{
    XMLFile@ uiStyle = cache.GetResource("XMLFile", "UI/DefaultStyle.xml");

    Cursor@ newCursor = Cursor("Cursor");
    newCursor.SetStyleAuto(uiStyle);
    newCursor.position = IntVector2(graphics.width / 2, graphics.height / 2);
    ui.cursor = newCursor;
    if (GetPlatform() == "Android" || GetPlatform() == "iOS")
        ui.cursor.visible = false;
}

void InitScene()
{
    testScene = Scene("TestScene");

    // Enable access to this script file & scene from the console
    script.defaultScene = testScene;
    script.defaultScriptFile = scriptFile;

    // Create the camera outside the scene so it is unaffected by scene load/save
    cameraNode = Node();
    camera = cameraNode.CreateComponent("Camera");
    camera.farClip = 1000;
    camera.nearClip = 0.5;
    cameraNode.position = Vector3(0, 20, 0);

    if (!engine.headless)
    {
        renderer.viewports[0] = Viewport(testScene, camera);

        // Add bloom & FXAA effects to the renderpath. Clone the default renderpath so that we don't affect it
        RenderPath@ newRenderPath = renderer.viewports[0].renderPath.Clone();
        newRenderPath.Append(cache.GetResource("XMLFile", "PostProcess/Bloom.xml"));
        newRenderPath.Append(cache.GetResource("XMLFile", "PostProcess/EdgeFilter.xml"));
        newRenderPath.SetEnabled("Bloom", false);
        newRenderPath.SetEnabled("EdgeFilter", false);
        renderer.viewports[0].renderPath = newRenderPath;

        audio.listener = cameraNode.CreateComponent("SoundListener");
    }

    if (runClient)
        return;

    PhysicsWorld@ world = testScene.CreateComponent("PhysicsWorld");
    testScene.CreateComponent("Octree");
    testScene.CreateComponent("DebugRenderer");

    Node@ zoneNode = testScene.CreateChild("Zone");
    Zone@ zone = zoneNode.CreateComponent("Zone");
    zone.ambientColor = Color(0.15, 0.15, 0.15);
    zone.fogColor = Color(0.5, 0.5, 0.7);
    zone.fogStart = 500.0;
    zone.fogEnd = 1000.0;
    zone.boundingBox = BoundingBox(-2000, 2000);

    {
        Node@ lightNode = testScene.CreateChild("GlobalLight");
        lightNode.direction = Vector3(0.5, -0.5, 0.5);

        Light@ light = lightNode.CreateComponent("Light");
        light.lightType = LIGHT_DIRECTIONAL;
        light.castShadows = true;
        light.shadowBias = BiasParameters(0.0001, 0.5);
        light.shadowCascade = CascadeParameters(10.0, 50.0, 200.0, 0.0, 0.8);
        light.specularIntensity = 0.5;
    }

    Terrain@ terrain;

    {
        Node@ terrainNode = testScene.CreateChild("Terrain");
        terrainNode.position = Vector3(0, 0, 0);
        terrain = terrainNode.CreateComponent("Terrain");
        terrain.patchSize = 64;
        terrain.spacing = Vector3(2, 0.5, 2);
        terrain.smoothing = true;
        terrain.heightMap = cache.GetResource("Image", "Textures/HeightMap.png");
        terrain.material = cache.GetResource("Material", "Materials/Terrain.xml");
        terrain.occluder = true;

        RigidBody@ body = terrainNode.CreateComponent("RigidBody");
        CollisionShape@ shape = terrainNode.CreateComponent("CollisionShape");
        shape.SetTerrain();
        shape.margin = 0.01;
    }

    for (uint i = 0; i < 1000; ++i)
    {
        Node@ objectNode = testScene.CreateChild("Mushroom");
        Vector3 position(Random() * 2000 - 1000, 0, Random() * 2000 - 1000);
        position.y = terrain.GetHeight(position) - 0.1;

        objectNode.position = position;
        objectNode.rotation = Quaternion(Vector3(0, 1, 0), terrain.GetNormal(position));
        objectNode.SetScale(3);

        StaticModel@ object = objectNode.CreateComponent("StaticModel");
        object.model = cache.GetResource("Model", "Models/Mushroom.mdl");
        object.material = cache.GetResource("Material", "Materials/Mushroom.xml");
        object.castShadows = true;

        RigidBody@ body = objectNode.CreateComponent("RigidBody");
        CollisionShape@ shape = objectNode.CreateComponent("CollisionShape");
        shape.SetTriangleMesh(cache.GetResource("Model", "Models/Mushroom.mdl"));
    }
}

void HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    float timeStep = eventData["TimeStep"].GetFloat();

    if (ui.focusElement is null)
    {
        float speedMultiplier = 1.0;
        if (input.keyDown[KEY_LSHIFT])
            speedMultiplier = 5.0;
        if (input.keyDown[KEY_LCTRL])
            speedMultiplier = 0.1;

        if (input.keyDown['W'])
            cameraNode.TranslateRelative(Vector3(0, 0, 10) * timeStep * speedMultiplier);
        if (input.keyDown['S'])
            cameraNode.TranslateRelative(Vector3(0, 0, -10) * timeStep * speedMultiplier);
        if (input.keyDown['A'])
            cameraNode.TranslateRelative(Vector3(-10, 0, 0) * timeStep * speedMultiplier);
        if (input.keyDown['D'])
            cameraNode.TranslateRelative(Vector3(10, 0, 0) * timeStep * speedMultiplier);
    }
}

void HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
    int key = eventData["Key"].GetInt();

    if (key == KEY_ESC)
    {
        if (ui.focusElement is null)
            engine.Exit();
        else
            console.visible = false;
    }

    if (key == KEY_F1)
        console.Toggle();

    if (ui.focusElement is null)
    {
        if (key == '1')
        {
            int quality = renderer.textureQuality;
            ++quality;
            if (quality > 2)
                quality = 0;
            renderer.textureQuality = quality;
        }

        if (key == '2')
        {
            int quality = renderer.materialQuality;
            ++quality;
            if (quality > 2)
                quality = 0;
            renderer.materialQuality = quality;
        }

        if (key == '3')
            renderer.specularLighting = !renderer.specularLighting;

        if (key == '4')
            renderer.drawShadows = !renderer.drawShadows;

        if (key == '5')
        {
            int size = renderer.shadowMapSize;
            size *= 2;
            if (size > 2048)
                size = 512;
            renderer.shadowMapSize = size;
        }

        if (key == '6')
            renderer.shadowQuality = renderer.shadowQuality + 1;

        if (key == '7')
        {
            bool occlusion = renderer.maxOccluderTriangles > 0;
            occlusion = !occlusion;
            renderer.maxOccluderTriangles = occlusion ? 5000 : 0;
        }

        if (key == '8')
            renderer.dynamicInstancing = !renderer.dynamicInstancing;

        if (key == ' ')
        {
            drawDebug++;
            if (drawDebug > 2)
                drawDebug = 0;
        }

        if (key == 'B')
            renderer.viewports[0].renderPath.ToggleEnabled("Bloom");

        if (key == 'F')
            renderer.viewports[0].renderPath.ToggleEnabled("EdgeFilter");

        if (key == 'O')
            camera.orthographic = !camera.orthographic;

        if (key == 'T')
            debugHud.Toggle(DEBUGHUD_SHOW_PROFILER);

        if (key == 'L')
            ToggleLiquid();

        if (key == KEY_F5)
        {
            File@ xmlFile = File(fileSystem.programDir + "Data/Scenes/Terrain.xml", FILE_WRITE);
            testScene.SaveXML(xmlFile);
        }

        if (key == KEY_F7)
        {
            File@ xmlFile = File(fileSystem.programDir + "Data/Scenes/Terrain.xml", FILE_READ);
            if (xmlFile.open)
                testScene.LoadXML(xmlFile);
        }
    }
}

void HandleMouseMove(StringHash eventType, VariantMap& eventData)
{
    if (eventData["Buttons"].GetInt() & MOUSEB_RIGHT != 0)
    {
        int mousedx = eventData["DX"].GetInt();
        int mousedy = eventData["DY"].GetInt();
        yaw += mousedx / 10.0;
        pitch += mousedy / 10.0;
        if (pitch < -90.0)
            pitch = -90.0;
        if (pitch > 90.0)
            pitch = 90.0;

        cameraNode.rotation = Quaternion(pitch, yaw, 0);
    }
}

void HandleMouseButtonDown(StringHash eventType, VariantMap& eventData)
{
    int button = eventData["Button"].GetInt();
    if (button == MOUSEB_RIGHT)
        ui.cursor.visible = false;

    // Test either creating a new physics object or painting a decal (SHIFT down)
    if (button == MOUSEB_LEFT && ui.GetElementAt(ui.cursorPosition, true) is null && ui.focusElement is null)
    {
        if (!input.qualifierDown[QUAL_SHIFT])
        {
            VariantMap eventData;
            eventData["Pos"] = cameraNode.position;
            eventData["Rot"] = cameraNode.rotation;

            // If we are the client, send the spawn command as a remote event, else send locally
            if (runClient)
            {
                if (network.serverConnection !is null)
                    network.serverConnection.SendRemoteEvent("SpawnBox", true, eventData);
            }
            else
                SendEvent("SpawnBox", eventData);
        }
        else
        {
            IntVector2 pos = ui.cursorPosition;
            if (ui.GetElementAt(pos, true) is null && testScene.octree !is null)
            {
                Ray cameraRay = camera.GetScreenRay(float(pos.x) / graphics.width, float(pos.y) / graphics.height);
                RayQueryResult result = testScene.octree.RaycastSingle(cameraRay, RAY_TRIANGLE, 250.0, DRAWABLE_GEOMETRY);
                if (result.drawable !is null)
                {
                    Vector3 rayHitPos = cameraRay.origin + cameraRay.direction * result.distance;
                    DecalSet@ decal = result.drawable.node.GetComponent("DecalSet");
                    if (decal is null)
                    {
                        decal = result.drawable.node.CreateComponent("DecalSet");
                        decal.material = cache.GetResource("Material", "Materials/UrhoDecal.xml");
                        // Increase max. vertices/indices if the target is skinned
                        if (result.drawable.typeName == "AnimatedModel")
                        {
                            decal.maxVertices = 2048;
                            decal.maxIndices = 4096;
                        }
                    }
                    decal.AddDecal(result.drawable, rayHitPos, cameraNode.worldRotation, 0.5, 1.0, 1.0, Vector2(0, 0),
                        Vector2(1, 1));
                }
            }
        }
    }
}

void HandleSpawnBox(StringHash eventType, VariantMap& eventData)
{
    Vector3 position = eventData["Pos"].GetVector3();
    Quaternion rotation = eventData["Rot"].GetQuaternion();

    Node@ newNode = testScene.CreateChild("Box");
    newNode.position = position;
    newNode.rotation = rotation;
    newNode.SetScale(0.2);

    RigidBody@ body = newNode.CreateComponent("RigidBody");
    body.mass = 1.0;
    body.friction = 1.0;
    body.linearVelocity = rotation * Vector3(0.0, 1.0, 10.0);
    body.ccdRadius = 0.15;
    body.ccdMotionThreshold = 0.2;

    CollisionShape@ shape = newNode.CreateComponent("CollisionShape");
    shape.SetBox(Vector3(1, 1, 1));

    StaticModel@ object = newNode.CreateComponent("StaticModel");
    object.model = cache.GetResource("Model", "Models/Box.mdl");
    object.material = cache.GetResource("Material", "Materials/StoneSmall.xml");
    object.castShadows = true;
    object.shadowDistance = 150.0;
    object.drawDistance = 200.0;
}

void HandleMouseButtonUp(StringHash eventType, VariantMap& eventData)
{
    if (eventData["Button"].GetInt() == MOUSEB_RIGHT)
        ui.cursor.visible = true;
}

void HandlePostRenderUpdate()
{
    if (engine.headless)
        return;

    // Draw rendering debug geometry without depth test to see the effect of occlusion
    if (drawDebug == 1)
        renderer.DrawDebugGeometry(false);
    if (drawDebug == 2)
        testScene.physicsWorld.DrawDebugGeometry(true);

    IntVector2 pos = ui.cursorPosition;
    if (ui.GetElementAt(pos, true) is null && testScene.octree !is null)
    {
        Ray cameraRay = camera.GetScreenRay(float(pos.x) / graphics.width, float(pos.y) / graphics.height);
        RayQueryResult result = testScene.octree.RaycastSingle(cameraRay, RAY_TRIANGLE, 250.0, DRAWABLE_GEOMETRY);
        if (result.drawable !is null)
        {
            Vector3 rayHitPos = cameraRay.origin + cameraRay.direction * result.distance;
            testScene.debugRenderer.AddBoundingBox(BoundingBox(rayHitPos + Vector3(-0.01, -0.01, -0.01), rayHitPos +
                Vector3(0.01, 0.01, 0.01)), Color(1.0, 1.0, 1.0), true);
        }
    }
}

void HandleClientConnected(StringHash eventType, VariantMap& eventData)
{
    Connection@ connection = eventData["Connection"].GetConnection();
    connection.scene = testScene; // Begin scene replication to the client
    connection.logStatistics = true;
}

void ToggleLiquid()
{
    Node@ liquidNode = testScene.GetChild("Liquid");

    if (liquidNode is null)
    {
        liquidNode = testScene.CreateChild("Liquid");
        liquidNode.position = Vector3(0, -48.75, 0);
        liquidNode.scale = Vector3(2000, 100, 2000);

        RigidBody@ body = liquidNode.CreateComponent("RigidBody");
        body.phantom = true;

        CollisionShape@ shape = liquidNode.CreateComponent("CollisionShape");
        shape.SetBox(Vector3(1, 1, 1));

        StaticModel@ object = liquidNode.CreateComponent("StaticModel");
        object.model = cache.GetResource("Model", "Models/Box.mdl");
        object.material = cache.GetResource("Material", "Materials/GreenTransparent.xml");

        liquidNode.CreateScriptObject(scriptFile, "BuoyancyVolume");
    }
    else
        liquidNode.Remove();
}

class BuoyancyVolume : ScriptObject
{
    void Start()
    {
        SubscribeToEvent(node, "NodeCollisionStart", "HandleCollisionStart");
        SubscribeToEvent(node, "NodeCollisionEnd", "HandleCollisionEnd");
    }

    void HandleCollisionStart(StringHash eventType, VariantMap& eventData)
    {
        Node@ otherNode = eventData["OtherNode"].GetNode();
        Print("Object " + otherNode.name + " entered the volume");
    }

    void HandleCollisionEnd(StringHash eventType, VariantMap& eventData)
    {
        Node@ otherNode = eventData["OtherNode"].GetNode();
        Print("Object " + otherNode.name + " left the volume");
    }

    void FixedUpdate(float timeStep)
    {
        RigidBody@ body = node.GetComponent("RigidBody");
        CollisionShape@ shape = node.GetComponent("CollisionShape");
        Array<RigidBody@>@ bodiesInside = body.collidingBodies;

        // The liquid volume should be a box
        float liquidLevel = node.worldPosition.y + node.worldScale.y * 0.5 * shape.size.y;

        for (uint i = 0; i < bodiesInside.length; ++i)
        {
            RigidBody@ otherBody = bodiesInside[i];
            if (otherBody.phantom == false && otherBody.mass > 0)
            {
                Node@ otherNode = otherBody.node;
                CollisionShape@ otherShape = otherNode.GetComponent("CollisionShape");

                // Assume the colliding shape is also a box
                float topLevel = otherNode.worldPosition.y + otherNode.worldScale.y * 0.5 * shape.size.y;
                float bottomLevel = otherNode.worldPosition.y - otherNode.worldScale.y * 0.5 * shape.size.y;
                float insideFraction = Clamp((liquidLevel - bottomLevel) / (topLevel - bottomLevel), 0.0, 1.0);

                // Apply buoyancy
                otherBody.ApplyForce(Vector3(0, 20.0, 0) * insideFraction * otherBody.mass);

                // Apply damping to linear & angular velocity
                float damping = 1.0 - insideFraction * 0.05;
                otherBody.linearVelocity = otherBody.linearVelocity * damping;
                otherBody.angularVelocity = otherBody.angularVelocity * damping;
            }
        }
    }
}
