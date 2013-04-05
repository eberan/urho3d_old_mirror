// Urho3D editor settings dialog

bool subscribedToEditorSettings = false;
Window@ settingsDialog;

void CreateEditorSettingsDialog()
{
    if (settingsDialog !is null)
        return;
    
    settingsDialog = ui.LoadLayout(cache.GetResource("XMLFile", "UI/EditorSettingsDialog.xml"));
    ui.root.AddChild(settingsDialog);
    settingsDialog.opacity = uiMaxOpacity;
    CenterDialog(settingsDialog);
    UpdateEditorSettingsDialog();
    HideEditorSettingsDialog();
}

void UpdateEditorSettingsDialog()
{
    if (settingsDialog is null)
        return;

    LineEdit@ nearClipEdit = settingsDialog.GetChild("NearClipEdit", true);
    nearClipEdit.text = String(camera.nearClip);
    
    LineEdit@ farClipEdit = settingsDialog.GetChild("FarClipEdit", true);
    farClipEdit.text = String(camera.farClip);
    
    LineEdit@ fovEdit = settingsDialog.GetChild("FOVEdit", true);
    fovEdit.text = String(camera.fov);

    LineEdit@ speedEdit = settingsDialog.GetChild("SpeedEdit", true);
    speedEdit.text = String(cameraBaseSpeed);

    LineEdit@ distanceEdit = settingsDialog.GetChild("DistanceEdit", true);
    distanceEdit.text = String(newNodeDistance);

    LineEdit@ moveStepEdit = settingsDialog.GetChild("MoveStepEdit", true);
    moveStepEdit.text = String(moveStep);
    CheckBox@ moveSnapToggle = settingsDialog.GetChild("MoveSnapToggle", true);
    moveSnapToggle.checked = moveSnap;

    LineEdit@ rotateStepEdit = settingsDialog.GetChild("RotateStepEdit", true);
    rotateStepEdit.text = String(rotateStep);
    CheckBox@ rotateSnapToggle = settingsDialog.GetChild("RotateSnapToggle", true);
    rotateSnapToggle.checked = rotateSnap;

    LineEdit@ scaleStepEdit = settingsDialog.GetChild("ScaleStepEdit", true);
    scaleStepEdit.text = String(scaleStep);
    CheckBox@ scaleSnapToggle = settingsDialog.GetChild("ScaleSnapToggle", true);
    scaleSnapToggle.checked = scaleSnap;

    CheckBox@ localIDToggle = settingsDialog.GetChild("LocalIDToggle", true);
    localIDToggle.checked = useLocalIDs;

    CheckBox@ applyMaterialListToggle = settingsDialog.GetChild("ApplyMaterialListToggle", true);
    applyMaterialListToggle.checked = applyMaterialList;

    LineEdit@ importOptionsEdit = settingsDialog.GetChild("ImportOptionsEdit", true);
    importOptionsEdit.text = importOptions;

    DropDownList@ pickModeEdit = settingsDialog.GetChild("PickModeEdit", true);
    pickModeEdit.selection = pickMode;

    DropDownList@ textureQualityEdit = settingsDialog.GetChild("TextureQualityEdit", true);
    textureQualityEdit.selection = renderer.textureQuality;

    DropDownList@ materialQualityEdit = settingsDialog.GetChild("MaterialQualityEdit", true);
    materialQualityEdit.selection = renderer.materialQuality;

    DropDownList@ shadowResolutionEdit = settingsDialog.GetChild("ShadowResolutionEdit", true);
    shadowResolutionEdit.selection = GetShadowResolution();

    DropDownList@ shadowQualityEdit = settingsDialog.GetChild("ShadowQualityEdit", true);
    shadowQualityEdit.selection = renderer.shadowQuality;

    LineEdit@ maxOccluderTrianglesEdit = settingsDialog.GetChild("MaxOccluderTrianglesEdit", true);
    maxOccluderTrianglesEdit.text = String(renderer.maxOccluderTriangles);

    CheckBox@ specularLightingToggle = settingsDialog.GetChild("SpecularLightingToggle", true);
    specularLightingToggle.checked = renderer.specularLighting;

    CheckBox@ dynamicInstancingToggle = settingsDialog.GetChild("DynamicInstancingToggle", true);
    dynamicInstancingToggle.checked = renderer.dynamicInstancing;

    CheckBox@ frameLimiterToggle = settingsDialog.GetChild("FrameLimiterToggle", true);
    frameLimiterToggle.checked = engine.maxFps > 0;

    if (!subscribedToEditorSettings)
    {
        SubscribeToEvent(nearClipEdit, "TextChanged", "EditCameraNearClip");
        SubscribeToEvent(nearClipEdit, "TextFinished", "EditCameraNearClip");
        SubscribeToEvent(farClipEdit, "TextChanged", "EditCameraFarClip");
        SubscribeToEvent(farClipEdit, "TextFinished", "EditCameraFarClip");
        SubscribeToEvent(fovEdit, "TextChanged", "EditCameraFOV");
        SubscribeToEvent(fovEdit, "TextFinished", "EditCameraFOV");
        SubscribeToEvent(speedEdit, "TextChanged", "EditCameraSpeed");
        SubscribeToEvent(speedEdit, "TextFinished", "EditCameraSpeed");
        SubscribeToEvent(distanceEdit, "TextChanged", "EditNewNodeDistance");
        SubscribeToEvent(distanceEdit, "TextFinished", "EditNewNodeDistance");
        SubscribeToEvent(moveStepEdit, "TextChanged", "EditMoveStep");
        SubscribeToEvent(moveStepEdit, "TextFinished", "EditMoveStep");
        SubscribeToEvent(rotateStepEdit, "TextChanged", "EditRotateStep");
        SubscribeToEvent(rotateStepEdit, "TextFinished", "EditRotateStep");
        SubscribeToEvent(scaleStepEdit, "TextChanged", "EditScaleStep");
        SubscribeToEvent(scaleStepEdit, "TextFinished", "EditScaleStep");
        SubscribeToEvent(moveSnapToggle, "Toggled", "EditMoveSnap");
        SubscribeToEvent(rotateSnapToggle, "Toggled", "EditRotateSnap");
        SubscribeToEvent(scaleSnapToggle, "Toggled", "EditScaleSnap");
        SubscribeToEvent(localIDToggle, "Toggled", "EditUseLocalIDs");
        SubscribeToEvent(applyMaterialListToggle, "Toggled", "EditApplyMaterialList");
        SubscribeToEvent(importOptionsEdit, "TextChanged", "EditImportOptions");
        SubscribeToEvent(importOptionsEdit, "TextFinished", "EditImportOptions");
        SubscribeToEvent(pickModeEdit, "ItemSelected", "EditPickMode");
        SubscribeToEvent(textureQualityEdit, "ItemSelected", "EditTextureQuality");
        SubscribeToEvent(materialQualityEdit, "ItemSelected", "EditMaterialQuality");
        SubscribeToEvent(shadowResolutionEdit, "ItemSelected", "EditShadowResolution");
        SubscribeToEvent(shadowQualityEdit, "ItemSelected", "EditShadowQuality");
        SubscribeToEvent(maxOccluderTrianglesEdit, "TextChanged", "EditMaxOccluderTriangles");
        SubscribeToEvent(maxOccluderTrianglesEdit, "TextFinished", "EditMaxOccluderTriangles");
        SubscribeToEvent(specularLightingToggle, "Toggled", "EditSpecularLighting");
        SubscribeToEvent(dynamicInstancingToggle, "Toggled", "EditDynamicInstancing");
        SubscribeToEvent(frameLimiterToggle, "Toggled", "EditFrameLimiter");
        SubscribeToEvent(settingsDialog.GetChild("CloseButton", true), "Released", "HideEditorSettingsDialog");
        subscribedToEditorSettings = true;
    }
}

bool ShowEditorSettingsDialog()
{
    UpdateEditorSettingsDialog();
    settingsDialog.visible = true;
    settingsDialog.BringToFront();
    return true;
}

void HideEditorSettingsDialog()
{
    settingsDialog.visible = false;
}

void EditCameraNearClip(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    camera.nearClip = edit.text.ToFloat();
    if (eventType == StringHash("TextFinished"))
        edit.text = String(camera.nearClip);
}

void EditCameraFarClip(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    camera.farClip = edit.text.ToFloat();
    if (eventType == StringHash("TextFinished"))
        edit.text = String(camera.farClip);
}

void EditCameraFOV(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    camera.fov = edit.text.ToFloat();
    if (eventType == StringHash("TextFinished"))
        edit.text = String(camera.fov);
}

void EditCameraSpeed(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    cameraBaseSpeed = Max(edit.text.ToFloat(), 1.0);
    if (eventType == StringHash("TextFinished"))
        edit.text = String(cameraBaseSpeed);
}

void EditNewNodeDistance(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    newNodeDistance = Max(edit.text.ToFloat(), 0.0);
    if (eventType == StringHash("TextFinished"))
        edit.text = String(newNodeDistance);
}

void EditMoveStep(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    moveStep = Max(edit.text.ToFloat(), 0.0);
    if (eventType == StringHash("TextFinished"))
        edit.text = String(moveStep);
}

void EditRotateStep(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    rotateStep = Max(edit.text.ToFloat(), 0.0);
    if (eventType == StringHash("TextFinished"))
        edit.text = String(rotateStep);
}

void EditScaleStep(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    scaleStep = Max(edit.text.ToFloat(), 0.0);
    if (eventType == StringHash("TextFinished"))
        edit.text = String(scaleStep);
}

void EditMoveSnap(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    moveSnap = edit.checked;
}

void EditRotateSnap(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    rotateSnap = edit.checked;
}

void EditScaleSnap(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    scaleSnap = edit.checked;
}

void EditUseLocalIDs(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    useLocalIDs = edit.checked;
}

void EditApplyMaterialList(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    applyMaterialList = edit.checked;
}

void EditImportOptions(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    importOptions = edit.text.Trimmed();
}

void EditPickMode(StringHash eventType, VariantMap& eventData)
{
    DropDownList@ edit = eventData["Element"].GetUIElement();
    pickMode = edit.selection;
}

void EditTextureQuality(StringHash eventType, VariantMap& eventData)
{
    DropDownList@ edit = eventData["Element"].GetUIElement();
    renderer.textureQuality = edit.selection;
}

void EditMaterialQuality(StringHash eventType, VariantMap& eventData)
{
    DropDownList@ edit = eventData["Element"].GetUIElement();
    renderer.materialQuality = edit.selection;
}

void EditShadowResolution(StringHash eventType, VariantMap& eventData)
{
    DropDownList@ edit = eventData["Element"].GetUIElement();
    SetShadowResolution(edit.selection);
}

void EditShadowQuality(StringHash eventType, VariantMap& eventData)
{
    DropDownList@ edit = eventData["Element"].GetUIElement();
    renderer.shadowQuality = edit.selection;
}

void EditMaxOccluderTriangles(StringHash eventType, VariantMap& eventData)
{
    LineEdit@ edit = eventData["Element"].GetUIElement();
    renderer.maxOccluderTriangles = edit.text.ToInt();
    if (eventType == StringHash("TextFinished"))
        edit.text = String(renderer.maxOccluderTriangles);
}

void EditSpecularLighting(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    renderer.specularLighting = edit.checked;
}

void EditDynamicInstancing(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    renderer.dynamicInstancing = edit.checked;
}

void EditFrameLimiter(StringHash eventType, VariantMap& eventData)
{
    CheckBox@ edit = eventData["Element"].GetUIElement();
    engine.maxFps = edit.checked ? 200 : 0;
}
