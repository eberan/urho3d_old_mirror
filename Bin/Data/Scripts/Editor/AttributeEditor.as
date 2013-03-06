// Attribute editor
//
// Functions that you must implement:
// - void SetAttributeEditorID(UIElement@ attrEdit, Array<Serializable@>@ serializables);
// - void PostEditAttribute(Array<Serializable@>@ serializables, uint index);
// - Array<Serializable@>@ GetAttributeEditorTargets(UIElement@ attrEdit);

const uint MIN_NODE_ATTRIBUTES = 4;
const uint MAX_NODE_ATTRIBUTES = 8;
const int ATTRNAME_WIDTH = 150;
const int ATTR_HEIGHT = 19;
bool inLoadAttributeEditor = false;

// If you want to show AM_NOEDIT in attribute editor set this flag = true. This will show attribute as String.
bool showNonEditAttribute = false;

const ShortStringHash textType("Text");
const ShortStringHash containerType("UIElement");
const StringHash textChangedEventType("TextChanged");

Color normalTextColor(1.0f, 1.0f, 1.0f);
Color modifiedTextColor(1.0f, 0.8f, 0.5f);

String sceneResourcePath;

UIElement@ CreateAttributeEditorParentWithSeparatedLabel(ListView@ list, String name, uint index, uint subIndex, bool suppressedSeparatedLabel = false)
{
    UIElement@ editorParent = UIElement("Edit" + String(index) + "_" + String(subIndex));
    editorParent.vars["Index"] = index;
    editorParent.vars["SubIndex"] = subIndex;
    editorParent.SetLayout(LM_VERTICAL, 2);
    list.AddItem(editorParent);

    if (suppressedSeparatedLabel)
    {
        UIElement@ placeHolder = UIElement();
        editorParent.AddChild(placeHolder);
    }
    else
    {
        Text@ attrNameText = Text();
        attrNameText.SetStyle(uiStyle, "EditorAttributeText");
        attrNameText.text = name;
        editorParent.AddChild(attrNameText);
    }

    return editorParent;
}

UIElement@ CreateAttributeEditorParent(ListView@ list, String name, uint index, uint subIndex)
{
    UIElement@ editorParent = UIElement("Edit" + String(index) + "_" + String(subIndex));
    editorParent.vars["Index"] = index;
    editorParent.vars["SubIndex"] = subIndex;
    editorParent.SetLayout(LM_HORIZONTAL);
    editorParent.SetFixedHeight(ATTR_HEIGHT);
    list.AddItem(editorParent);

    Text@ attrNameText = Text();
    attrNameText.SetStyle(uiStyle, "EditorAttributeText");
    attrNameText.text = name;
    attrNameText.SetFixedWidth(ATTRNAME_WIDTH);
    editorParent.AddChild(attrNameText);

    return editorParent;
}

LineEdit@ CreateAttributeLineEdit(UIElement@ parent, Array<Serializable@>@ serializables, uint index, uint subIndex)
{
    LineEdit@ attrEdit = LineEdit();
    attrEdit.SetStyle(uiStyle, "EditorAttributeEdit");
    attrEdit.SetFixedHeight(ATTR_HEIGHT - 2);
    attrEdit.vars["Index"] = index;
    attrEdit.vars["SubIndex"] = subIndex;
    SetAttributeEditorID(attrEdit, serializables);
    parent.AddChild(attrEdit);
    return attrEdit;
}

void CreateAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, uint index)
{
    AttributeInfo info = serializables[0].attributeInfos[index];
    CreateAttributeEditor(list, serializables, info.name, info.type, info.enumNames, index, 0);
}

UIElement@ CreateStringAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name,
                                       VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent = CreateAttributeEditorParent(list, name, index, subIndex);
    LineEdit@ attrEdit = CreateAttributeLineEdit(parent, serializables, index, subIndex);
    attrEdit.dragDropMode = DD_TARGET;
    SubscribeToEvent(attrEdit, "TextChanged", "EditAttribute");
    SubscribeToEvent(attrEdit, "TextFinished", "EditAttribute");
    return parent;
}

UIElement@ CreateNonEditAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, 
                                        VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent = CreateAttributeEditorParent(list, name, index, subIndex);
    Text@ text = Text();
    text.style = uiStyle;
    parent.AddChild(text);
    text.vars["Index"] = index;
    text.vars["SubIndex"] = subIndex;
    SetAttributeEditorID(text, serializables);
    return parent;
}

UIElement@ CreateBoolAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, 
                                     VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent = CreateAttributeEditorParent(list, name, index, subIndex);
    CheckBox@ attrEdit = CheckBox();
    attrEdit.style = uiStyle;
    attrEdit.SetFixedSize(16, 16);
    attrEdit.vars["Index"] = index;
    attrEdit.vars["SubIndex"] = subIndex;
    SetAttributeEditorID(attrEdit, serializables);
    parent.AddChild(attrEdit);
    SubscribeToEvent(attrEdit, "Toggled", "EditAttribute");
    return parent;
}

UIElement@ CreateNumAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, 
                                     VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent = CreateAttributeEditorParent(list, name, index, subIndex);
    uint numCoords = type - VAR_FLOAT + 1;
    if (type == VAR_QUATERNION)
        numCoords = 3;
    if (type == VAR_COLOR)
        numCoords = 4;
    if(type == VAR_INTVECTOR2)
        numCoords = 2;
    if(type == VAR_INTRECT)
        numCoords = 4;
    
    for (uint i = 0; i < numCoords; ++i)
    {
        LineEdit@ attrEdit = CreateAttributeLineEdit(parent, serializables, index, subIndex);
        SubscribeToEvent(attrEdit, "TextChanged", "EditAttribute");
        SubscribeToEvent(attrEdit, "TextFinished", "EditAttribute");
    }     
    return parent;    
}

UIElement@ CreateIntAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, 
                                     VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent = CreateAttributeEditorParent(list, name, index, subIndex);
    // Check for enums
    if (enumNames is null || enumNames.empty)
    {
        // No enums, create a numeric editor
        LineEdit@ attrEdit = CreateAttributeLineEdit(parent, serializables, index, subIndex);
        SubscribeToEvent(attrEdit, "TextChanged", "EditAttribute");
        SubscribeToEvent(attrEdit, "TextFinished", "EditAttribute");
    }
    else
    {
        DropDownList@ attrEdit = DropDownList();
        attrEdit.style = uiStyle;
        attrEdit.SetFixedHeight(ATTR_HEIGHT - 2);
        attrEdit.resizePopup = true;
        attrEdit.vars["Index"] = index;
        attrEdit.vars["SubIndex"] = subIndex;
        attrEdit.SetLayout(LM_HORIZONTAL, 0, IntRect(4, 1, 4, 1));
        SetAttributeEditorID(attrEdit, serializables);

        for (uint i = 0; i < enumNames.length; ++i)
        {
            Text@ choice = Text();
            choice.SetStyle(uiStyle, "EditorEnumAttributeText");
            choice.text = enumNames[i];
            attrEdit.AddItem(choice);
        }
        parent.AddChild(attrEdit);
        SubscribeToEvent(attrEdit, "ItemSelected", "EditAttribute");
    }

    if(parent is null)
    {
        Print("ERROR");
    }
    return parent;
}

UIElement@ CreateResourceRefAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, 
                                            VariantType type, Array<String>@ enumNames, uint index, uint subIndex,
                                            bool suppressedSeparatedLabel = false)
{
    UIElement@ parent;
    ShortStringHash resourceType;
    VariantType attrType = serializables[0].attributeInfos[index].type;
    if (attrType == VAR_RESOURCEREF)
        resourceType = serializables[0].attributes[index].GetResourceRef().type;
    else if (attrType == VAR_RESOURCEREFLIST)
        resourceType = serializables[0].attributes[index].GetResourceRefList().type;
    else if (attrType == VAR_VARIANTVECTOR)
        resourceType = serializables[0].attributes[index].GetVariantVector()[subIndex].GetResourceRef().type;

     // Create the resource name on a separate non-interactive line to allow for more space
    parent = CreateAttributeEditorParentWithSeparatedLabel(list, name, index, subIndex, suppressedSeparatedLabel);

    UIElement@ container = UIElement();
    container.SetLayout(LM_HORIZONTAL, 4, IntRect(name.StartsWith("   ") ? 20 : 10, 0, 4, 0));    // Left margin is indented more when the name is so
    container.SetFixedHeight(ATTR_HEIGHT);
    parent.AddChild(container);
        
    LineEdit@ attrEdit = CreateAttributeLineEdit(container, serializables, index, subIndex);
    attrEdit.vars["Type"] = resourceType.value;
    SubscribeToEvent(attrEdit, "TextFinished", "EditAttribute");

    Button@ pickButton = Button();
    pickButton.style = uiStyle;
    pickButton.SetFixedSize(36, ATTR_HEIGHT - 2);
    pickButton.vars["Index"] = index;
    pickButton.vars["SubIndex"] = subIndex;
    SetAttributeEditorID(pickButton, serializables);

    Text@ pickButtonText = Text();
    pickButtonText.SetStyle(uiStyle, "EditorAttributeText");
    pickButtonText.SetAlignment(HA_CENTER, VA_CENTER);
    pickButtonText.text = "Pick";
    pickButton.AddChild(pickButtonText);
    container.AddChild(pickButton);
    SubscribeToEvent(pickButton, "Released", "PickResource");

    Button@ openButton = Button();
    openButton.style = uiStyle;
    openButton.SetFixedSize(36, ATTR_HEIGHT - 2);
    openButton.vars["Index"] = index;
    openButton.vars["SubIndex"] = subIndex;
    SetAttributeEditorID(openButton, serializables);

    Text@ openButtonText = Text();
    openButtonText.SetStyle(uiStyle, "EditorAttributeText");
    openButtonText.SetAlignment(HA_CENTER, VA_CENTER);
    openButtonText.text = "Open";
    openButton.AddChild(openButtonText);
    container.AddChild(openButton);
    SubscribeToEvent(openButton, "Released", "OpenResource");
    return parent;
}

UIElement@ CreateAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, const String&in name, VariantType type, Array<String>@ enumNames, uint index, uint subIndex)
{
    UIElement@ parent;

    AttributeInfo info = serializables[0].attributeInfos[index];
    if((info.mode & AM_NOEDIT != 0 && showNonEditAttribute))
    {
        parent = CreateNonEditAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
        return parent;
    }

    if (type == VAR_STRING)
    {
        parent = CreateStringAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
    }
    if (type == VAR_BOOL)
    {
        parent = CreateBoolAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
    }
    if ((type >= VAR_FLOAT && type <= VAR_VECTOR4) || type == VAR_QUATERNION || type == VAR_COLOR  
        || type == VAR_INTVECTOR2 || type == VAR_INTRECT)
    {
        parent = CreateNumAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
    }
    if (type == VAR_INT)
    {
        parent = CreateIntAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
    }
    if (type == VAR_RESOURCEREF)
    {
        parent = CreateResourceRefAttributeEditor(list, serializables, name, type, enumNames, index, subIndex);
    }
    if (type == VAR_RESOURCEREFLIST)
    {
        uint numRefs = serializables[0].attributes[index].GetResourceRefList().length;
        for (uint i = 0; i < numRefs; ++i)
            CreateAttributeEditor(list, serializables, name, VAR_RESOURCEREF, null, index, i);
    }
    if (type == VAR_VARIANTVECTOR)
    {
        VectorStruct@ vectorStruct = GetVectorStruct(serializables, index);
        if (vectorStruct is null)
            return null;
        uint nameIndex = 0;

        Array<Variant>@ vector = serializables[0].attributes[index].GetVariantVector();
        for (uint i = 0; i < vector.length; ++i)
        {
            CreateAttributeEditor(list, serializables, vectorStruct.variableNames[nameIndex], vector[i].type, null, index, i);
            ++nameIndex;
            if (nameIndex >= vectorStruct.variableNames.length)
                nameIndex = vectorStruct.restartIndex;
        }
    }
    if (type == VAR_VARIANTMAP)
    {
        VariantMap map = serializables[0].attributes[index].GetVariantMap();
        Array<ShortStringHash>@ keys = map.keys;
        for (uint i = 0; i < keys.length; ++i)
        {
            Variant value = map[keys[i]];
            parent = CreateAttributeEditor(list, serializables, scene.GetVarName(keys[i]) + " (Var)", value.type, null, index, i);
           if(parent is null)
            {
                Print("error parent is null!!!! + " + scene.GetVarName(keys[i]));
            }
            else
            {
                // Add the variant key to the parent
                parent.vars["Key"] = keys[i].value;
            }
        }
    }

    return parent;
}

uint GetAttributeEditorCount(Array<Serializable@>@ serializables)
{
    uint count = 0;

    if (!serializables.empty)
    {
        /// \todo When multi-editing, this only counts the editor count of the first serializable
        for (uint i = 0; i < serializables[0].numAttributes; ++i)
        {
            AttributeInfo info = serializables[0].attributeInfos[i];
            if(info.mode & AM_NOEDIT != 0)
            {
                if(showNonEditAttribute)
                {
                    ++count;
                }
                else
                {
                    continue;
                }
            }
            else
            {
                if (info.type == VAR_RESOURCEREFLIST)
                    count += serializables[0].attributes[i].GetResourceRefList().length;
                else if (info.type == VAR_VARIANTVECTOR && GetVectorStruct(serializables, i) !is null)
                    count += serializables[0].attributes[i].GetVariantVector().length;
                else if (info.type == VAR_VARIANTMAP)
                    count += serializables[0].attributes[i].GetVariantMap().length;
                else
                    ++count;
            }
        }
    }

    return count;
}

UIElement@ GetAttributeEditorParent(UIElement@ parent, uint index, uint subIndex)
{
    return parent.GetChild("Edit" + String(index) + "_" + String(subIndex), true);
}

void LoadAttributeEditor(ListView@ list, Array<Serializable@>@ serializables, uint index)
{
    UIElement@ parent = GetAttributeEditorParent(list, index, 0);
    if (parent is null)
        return;

    inLoadAttributeEditor = true;

    AttributeInfo info = serializables[0].attributeInfos[index];
    bool sameValue = true;
    Variant value = serializables[0].attributes[index];
    for (uint i = 1; i < serializables.length; ++i)
    {
        if (serializables[i].attributes[index] != value)
        {
            sameValue = false;
            break;
        }
    }

    if (sameValue) {
        if((info.mode & AM_NOEDIT != 0 && showNonEditAttribute))
        {
            Text@ text = parent.children[1];
            text.text = value.GetString();
        }
        else {
            LoadAttributeEditor(parent, value, value.type, info.enumNames, info.defaultValue);
        }
    }
        

    inLoadAttributeEditor = false;
}

void LoadAttributeEditor(UIElement@ parent, Variant value, VariantType type, Array<String>@ enumNames, Variant defaultValue)
{
    uint index = parent.vars["Index"].GetUInt();

    // Assume the first child is always a text label element or a container that containing a text label element
    UIElement@ label = parent.children[0];
    if (label.type == containerType)
        label = label.children[0];
    if (label.type == textType)
    {
        bool modified;
        if (defaultValue.type == VAR_NONE || defaultValue.type == VAR_RESOURCEREFLIST)
            modified = !value.IsZero();
        else
            modified = value != defaultValue;
        cast<Text>(label).color = (modified ? modifiedTextColor : normalTextColor);
    }
    
    if (type == VAR_STRING)
    {
        LineEdit@ attrEdit = parent.children[1];
        attrEdit.text = value.GetString();
    }
    if (type == VAR_BOOL)
    {
        CheckBox@ attrEdit = parent.children[1];
        attrEdit.checked = value.GetBool();
    }
    if (type == VAR_FLOAT)
    {
        LineEdit@ attrEdit = parent.children[1];
        attrEdit.text = String(value.GetFloat());
    }
    if (type == VAR_VECTOR2)
    {
        Vector2 vec = value.GetVector2();
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        attrEditX.text = String(vec.x);
        attrEditY.text = String(vec.y);
        attrEditX.cursorPosition = 0;
        attrEditY.cursorPosition = 0;
    }
    if (type == VAR_INTVECTOR2)
    {
        IntVector2 vec = value.GetIntVector2();
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        attrEditX.text = String(vec.x);
        attrEditY.text = String(vec.y);
        attrEditX.cursorPosition = 0;
        attrEditY.cursorPosition = 0;
    }
    if(type == VAR_INTRECT)
    {
        IntRect rect = value.GetIntRect();
        LineEdit@ attrEditLeft = parent.children[1];
        LineEdit@ attrEditTop = parent.children[2];
        LineEdit@ attrEditRight = parent.children[3];
        LineEdit@ attrEditBottom = parent.children[4];
        attrEditLeft.text = String(rect.left);
        attrEditTop.text = String(rect.top);
        attrEditRight.text = String(rect.right);
        attrEditBottom.text = String(rect.bottom);
        attrEditLeft.cursorPosition = 0;
        attrEditTop.cursorPosition = 0;
        attrEditRight.cursorPosition = 0;
        attrEditBottom.cursorPosition = 0;
    }
    if (type == VAR_VECTOR3)
    {
        Vector3 vec = value.GetVector3();
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        attrEditX.text = String(vec.x);
        attrEditY.text = String(vec.y);
        attrEditZ.text = String(vec.z);
        attrEditX.cursorPosition = 0;
        attrEditY.cursorPosition = 0;
        attrEditZ.cursorPosition = 0;     
    }
    if (type == VAR_VECTOR4)
    {
        Vector4 vec = value.GetVector4();
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        LineEdit@ attrEditW = parent.children[4];
        attrEditX.text = String(vec.x);
        attrEditY.text = String(vec.y);
        attrEditZ.text = String(vec.z);
        attrEditW.text = String(vec.w);
        attrEditX.cursorPosition = 0;
        attrEditY.cursorPosition = 0;
        attrEditZ.cursorPosition = 0;
        attrEditW.cursorPosition = 0;
    }
    if (type == VAR_COLOR)
    {
        Color col = value.GetColor();
        LineEdit@ attrEditR = parent.children[1];
        LineEdit@ attrEditG = parent.children[2];
        LineEdit@ attrEditB = parent.children[3];
        LineEdit@ attrEditA = parent.children[4];
        attrEditR.text = String(col.r);
        attrEditG.text = String(col.g);
        attrEditB.text = String(col.b);
        attrEditA.text = String(col.a);
    }
    if (type == VAR_QUATERNION)
    {
        Vector3 vec = value.GetQuaternion().eulerAngles;
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        attrEditX.text = String(vec.x);
        attrEditY.text = String(vec.y);
        attrEditZ.text = String(vec.z);
        attrEditX.cursorPosition = 0;
        attrEditY.cursorPosition = 0;
        attrEditZ.cursorPosition = 0;
    }
    if (type == VAR_INT)
    {
        if (enumNames is null || enumNames.empty)
        {
            LineEdit@ attrEdit = parent.children[1];
            attrEdit.text = String(value.GetInt());
        }
        else
        {
            DropDownList@ attrEdit = parent.children[1];
            attrEdit.selection = value.GetInt();
        }
    }
    if (type == VAR_RESOURCEREF)
    {
        LineEdit@ attrEdit = parent.children[1].children[0];
        attrEdit.text = cache.GetResourceName(value.GetResourceRef().id);
        attrEdit.cursorPosition = 0;
    }
    if (type == VAR_RESOURCEREFLIST)
    {
        UIElement@ list = parent.parent;
        ResourceRefList refList = value.GetResourceRefList();
        for (uint subIndex = 0; subIndex < refList.length; ++subIndex)
        {
            parent = GetAttributeEditorParent(list, index, subIndex);
            if (parent is null)
                break;
            LineEdit@ attrEdit = parent.children[1].children[0];
            attrEdit.text = cache.GetResourceName(refList.ids[subIndex]);
            attrEdit.cursorPosition = 0;
        }
    }
    if (type == VAR_VARIANTVECTOR)
    {
        UIElement@ list = parent.parent;
        Array<Variant>@ vector = value.GetVariantVector();
        for (uint i = 0; i < vector.length; ++i)
        {
            parent = GetAttributeEditorParent(list, index, i);
            if (parent is null)
                break;
            LoadAttributeEditor(parent, vector[i], vector[i].type, null, Variant());
        }
    }
    if (type == VAR_VARIANTMAP)
    {
        UIElement@ list = parent.parent;
        VariantMap map = value.GetVariantMap();
        Array<ShortStringHash>@ keys = map.keys;
        for (uint i = 0; i < keys.length; ++i)
        {
            parent = GetAttributeEditorParent(list, index, i);
            if (parent is null)
                break;
            Variant value = map[keys[i]];
            LoadAttributeEditor(parent, value, value.type, null, Variant());
        }
    }
}

void StoreAttributeEditor(UIElement@ parent, Array<Serializable@>@ serializables, uint index, uint subIndex)
{
    AttributeInfo info = serializables[0].attributeInfos[index];

    if (info.type == VAR_RESOURCEREFLIST)
    {
        for (uint i = 0; i < serializables.length; ++i)
        {
            ResourceRefList refList = serializables[i].attributes[index].GetResourceRefList();
            Variant newValue = GetEditorValue(parent, VAR_RESOURCEREF, null);
            ResourceRef ref = newValue.GetResourceRef();
            refList.ids[subIndex] = ref.id;
            serializables[i].attributes[index] = Variant(refList);
        }
    }
    else if (info.type == VAR_VARIANTVECTOR)
    {
        for (uint i = 0; i < serializables.length; ++i)
        {
            Array<Variant>@ vector = serializables[i].attributes[index].GetVariantVector();
            Variant newValue = GetEditorValue(parent, vector[subIndex].type, null);
            vector[subIndex] = newValue;
            serializables[i].attributes[index] = Variant(vector);
        }
    }
    else if (info.type == VAR_VARIANTMAP)
    {
        for (uint i = 0; i < serializables.length; ++i)
        {
            VariantMap map = serializables[0].attributes[index].GetVariantMap();
            ShortStringHash key(parent.vars["Key"].GetUInt());
            Variant newValue = GetEditorValue(parent, map[key].type, null);
            map[key] = newValue;
            serializables[i].attributes[index] = Variant(map);
        }
    }
    else
    {
        Variant newValue = GetEditorValue(parent, info.type, info.enumNames);
        for (uint i = 0; i < serializables.length; ++i) {
            serializables[i].attributes[index] = newValue;
        }
    }
}

Variant GetEditorValue(UIElement@ parent, VariantType type, Array<String>@ enumNames)
{
    if (type == VAR_STRING)
    {
        LineEdit@ attrEdit = parent.children[1];
        return Variant(attrEdit.text.Trimmed());
    }
    if (type == VAR_BOOL)
    {
        CheckBox@ attrEdit = parent.children[1];
        return Variant(attrEdit.checked);
    }
    if (type == VAR_FLOAT)
    {
        LineEdit@ attrEdit = parent.children[1];
        return Variant(attrEdit.text.ToFloat());
    }
    if (type == VAR_VECTOR2)
    {
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        Vector2 vec(attrEditX.text.ToFloat(), attrEditY.text.ToFloat());
        return Variant(vec);
    }
    if (type == VAR_VECTOR3)
    {
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        Vector3 vec(attrEditX.text.ToFloat(), attrEditY.text.ToFloat(), attrEditZ.text.ToFloat());
        return Variant(vec);
    }
    if (type == VAR_VECTOR4)
    {
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        LineEdit@ attrEditW = parent.children[4];
        Vector4 vec(attrEditX.text.ToFloat(), attrEditY.text.ToFloat(), attrEditZ.text.ToFloat(), attrEditW.text.ToFloat());
        return Variant(vec);
    }
    if (type == VAR_COLOR)
    {
        LineEdit@ attrEditR = parent.children[1];
        LineEdit@ attrEditG = parent.children[2];
        LineEdit@ attrEditB = parent.children[3];
        LineEdit@ attrEditA = parent.children[4];
        Color col(attrEditR.text.ToFloat(), attrEditG.text.ToFloat(), attrEditB.text.ToFloat(), attrEditA.text.ToFloat());
        return Variant(col);
    }
    if (type == VAR_QUATERNION)
    {
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        LineEdit@ attrEditZ = parent.children[3];
        Vector3 vec(attrEditX.text.ToFloat(), attrEditY.text.ToFloat(), attrEditZ.text.ToFloat());
        return Variant(Quaternion(vec));
    }
    if (type == VAR_INT)
    {
        if (enumNames is null || enumNames.empty)
        {
            LineEdit@ attrEdit = parent.children[1];
            return Variant(attrEdit.text.ToInt());
        }
        else
        {
            DropDownList@ attrEdit = parent.children[1];
            return Variant(attrEdit.selection);
        }
    }
    if (type == VAR_RESOURCEREF)
    {
        LineEdit@ attrEdit = parent.children[0];
        ResourceRef ref;
        ref.id = StringHash(attrEdit.text.Trimmed());
        ref.type = ShortStringHash(attrEdit.vars["Type"].GetUInt());
        return Variant(ref);
    }
    if (type == VAR_INTVECTOR2)
    {
        LineEdit@ attrEditX = parent.children[1];
        LineEdit@ attrEditY = parent.children[2];
        IntVector2 vec(attrEditX.text.ToInt(), attrEditY.text.ToInt());
        return Variant(vec);
    }
    if (type == VAR_INTRECT)
    {
        LineEdit@ attrEditLeft = parent.children[1];
        LineEdit@ attrEditTop = parent.children[2];
        LineEdit@ attrEditRight = parent.children[3];
        LineEdit@ attrEditBottom = parent.children[3];
        IntRect vec(attrEditLeft.text.ToInt(), attrEditTop.text.ToInt(),
                       attrEditRight.text.ToInt(), attrEditBottom.text.ToInt());
        return Variant(vec);
    }
    return Variant();
}

void UpdateAttributes(Array<Serializable@>@ serializables, ListView@ list, bool fullUpdate)
{
    // If attributes have changed structurally, do a full update
    uint count = GetAttributeEditorCount(serializables);
    if (fullUpdate == false)
    {
        uint oldCount = list.contentElement.numChildren;
        //Print("oldCount:"+oldCount+" count:"+count);
        if (oldCount != count)
            fullUpdate = true;
    }

    // Remember the old scroll position so that a full update does not feel as jarring
    IntVector2 oldViewPos = list.viewPosition;

    if (fullUpdate)
    {
        list.RemoveAllItems();

        // Resize the node editor according to the number of variables, up to a certain maximum
        if (list.name == "NodeAttributeList")
        {
            uint maxAttrs = Clamp(count, MIN_NODE_ATTRIBUTES, MAX_NODE_ATTRIBUTES);
            list.SetFixedHeight(maxAttrs * ATTR_HEIGHT + 2);
        }
    }

    if (serializables.empty)
        return;

    // If there are many serializables, they must share same attribute structure
    for (uint i = 0; i < serializables[0].numAttributes; ++i)
    {
        AttributeInfo info = serializables[0].attributeInfos[i];
        if (info.mode & AM_NOEDIT != 0 && !showNonEditAttribute)
            continue;

        if (fullUpdate)
            CreateAttributeEditor(list, serializables, i);

        LoadAttributeEditor(list, serializables, i);
    }
    
    if (fullUpdate)
        list.viewPosition = oldViewPos;
}

void EditAttribute(StringHash eventType, VariantMap& eventData)
{
    // Changing elements programmatically may cause events to be sent. Stop possible infinite loop in that case.
    if (inLoadAttributeEditor)
    {
        return;
    }

    UIElement@ attrEdit = eventData["Element"].GetUIElement();
    UIElement@ parent = attrEdit.parent;
    Array<Serializable@>@ serializables = GetAttributeEditorTargets(attrEdit);
    if (serializables.empty)
        return;

    uint index = attrEdit.vars["Index"].GetUInt();
    uint subIndex = attrEdit.vars["SubIndex"].GetUInt();
    bool intermediateEdit = eventType == textChangedEventType;

    StoreAttributeEditor(parent, serializables, index, subIndex);
    for (uint i = 0; i < serializables.length; ++i)
        serializables[i].ApplyAttributes();

    // Do the editor logic after attribute has been edited.
    PostEditAttribute(serializables, index);

    // If not an intermediate edit, reload the editor fields with validated values
    // (attributes may have interactions; therefore we load everything, not just the value being edited)
    if (!intermediateEdit)
        UpdateAttributes(false);
}

// Resource picker functionality

class ResourcePicker
{
    String resourceType;
    String lastPath;
    uint lastFilter;
    Array<String> filters;

    ResourcePicker(const String&in resourceType_, const String&in filter_)
    {
        resourceType = resourceType_;
        filters.Push(filter_);
        filters.Push("*.*");
        lastFilter = 0;
    }

    ResourcePicker(const String&in resourceType_, const Array<String>@ filters_)
    {
        resourceType = resourceType_;
        filters = filters_;
        filters.Push("*.*");
        lastFilter = 0;
    }
};

Array<ResourcePicker@> resourcePickers;
Array<Serializable@> resourceTargets;
uint resourcePickIndex = 0;
uint resourcePickSubIndex = 0;
ResourcePicker@ resourcePicker = null;

void InitResourcePicker()
{
    // Fill resource picker data
    Array<String> imageFilters = {"*.png", "*.jpg"};
    Array<String> textureFilters = {"*.dds", "*.png", "*.jpg", "*.bmp", "*.ktx", "*.pvr"};
    Array<String> soundFilters = {"*.wav","*.ogg"};
    resourcePickers.Push(ResourcePicker("Animation", "*.ani"));
    resourcePickers.Push(ResourcePicker("Image", imageFilters));
    resourcePickers.Push(ResourcePicker("Model", "*.mdl"));
    resourcePickers.Push(ResourcePicker("Material", "*.xml"));
    resourcePickers.Push(ResourcePicker("Texture2D", textureFilters));
    resourcePickers.Push(ResourcePicker("TextureCube", "*.xml"));
    resourcePickers.Push(ResourcePicker("ScriptFile", "*.as"));
    resourcePickers.Push(ResourcePicker("XMLFile", "*.xml"));
    resourcePickers.Push(ResourcePicker("Sound", soundFilters));
    sceneResourcePath = AddTrailingSlash(fileSystem.programDir + "Data");
}

ResourcePicker@ GetResourcePicker(const String&in resourceType)
{
    for (uint i = 0; i < resourcePickers.length; ++i)
    {
        if (resourcePickers[i].resourceType.Compare(resourceType, false) == 0)
            return resourcePickers[i];
    }
    return null;
}

void PickResource(StringHash eventType, VariantMap& eventData)
{
    if (uiFileSelector !is null)
        return;

    UIElement@ button = eventData["Element"].GetUIElement();
    LineEdit@ attrEdit = button.parent.children[0];

    Array<Serializable@>@ targets = GetAttributeEditorTargets(attrEdit);
    if (targets.empty)
        return;

    resourcePickIndex = attrEdit.vars["Index"].GetUInt();
    resourcePickSubIndex = attrEdit.vars["SubIndex"].GetUInt();
    AttributeInfo info = targets[0].attributeInfos[resourcePickIndex];

    if (info.type == VAR_RESOURCEREF)
    {
        String resourceType = GetTypeName(targets[0].attributes[resourcePickIndex].GetResourceRef().type);
        // Hack: if the resource is a light's shape texture, change resource type according to light type
        // (TextureCube for point light)
        if (info.name == "Light Shape Texture" && cast<Light>(targets[0]).lightType == LIGHT_POINT)
            resourceType = "TextureCube";
        @resourcePicker = GetResourcePicker(resourceType);
    }
    else if (info.type == VAR_RESOURCEREFLIST)
    {
        String resourceType = GetTypeName(targets[0].attributes[resourcePickIndex].GetResourceRefList().type);
        @resourcePicker = GetResourcePicker(resourceType);
    }
    else if (info.type == VAR_VARIANTVECTOR)
    {
        String resourceType = GetTypeName(targets[0].attributes[resourcePickIndex].GetVariantVector()
            [resourcePickSubIndex].GetResourceRef().type);
        @resourcePicker = GetResourcePicker(resourceType);
    }
    else
        @resourcePicker = null;

    if (resourcePicker is null)
        return;

    resourceTargets.Clear();
    for (uint i = 0; i < targets.length; ++i)
        resourceTargets.Push(targets[i]);

    String lastPath = resourcePicker.lastPath;
    if (lastPath.empty)
        lastPath = sceneResourcePath;
    CreateFileSelector("Pick " + resourcePicker.resourceType, "OK", "Cancel", lastPath, resourcePicker.filters, resourcePicker.lastFilter);
    SubscribeToEvent(uiFileSelector, "FileSelected", "PickResourceDone");
}

void PickResourceDone(StringHash eventType, VariantMap& eventData)
{
    // Store filter and directory for next time
    if (resourcePicker !is null)
    {
        resourcePicker.lastPath = uiFileSelector.path;
        resourcePicker.lastFilter = uiFileSelector.filterIndex;
    }

    CloseFileSelector();

    if (!eventData["OK"].GetBool())
    {
        resourceTargets.Clear();
        @resourcePicker = null;
        return;
    }

    if (resourcePicker is null)
        return;

    // Validate the resource. It must come from within a registered resource directory, and be loaded successfully
    String resourceName = eventData["FileName"].GetString();
    Array<String>@ resourceDirs = cache.resourceDirs;
    Resource@ res;

    for (uint i = 0; i < resourceDirs.length; ++i)
    {
        if (!resourceName.ToLower().StartsWith(resourceDirs[i].ToLower()))
            continue;
        resourceName = resourceName.Substring(resourceDirs[i].length);
        res = cache.GetResource(resourcePicker.resourceType, resourceName);
        break;
    }
    if (res is null) {
        Print("Can not find resource type: " + resourcePicker.resourceType + " Name:" +resourceName);
        return;
    }

    for (uint i = 0; i < resourceTargets.length; ++i)
    {
        Serializable@ target = resourceTargets[i];

        AttributeInfo info = target.attributeInfos[resourcePickIndex];
        if (info.type == VAR_RESOURCEREF)
        {
            ResourceRef ref = target.attributes[resourcePickIndex].GetResourceRef();
            ref.type = ShortStringHash(resourcePicker.resourceType);
            ref.id = StringHash(resourceName);
            //Print("Set type: " + resourcePicker.resourceType + " Name:" +resourceName);
            target.attributes[resourcePickIndex] = Variant(ref);
            target.ApplyAttributes();
        }
        else if (info.type == VAR_RESOURCEREFLIST)
        {
            ResourceRefList refList = target.attributes[resourcePickIndex].GetResourceRefList();
            if (resourcePickSubIndex < refList.length)
            {
                refList.ids[resourcePickSubIndex] = StringHash(resourceName);
                target.attributes[resourcePickIndex] = Variant(refList);
                target.ApplyAttributes();
            }
        }
        else if (info.type == VAR_VARIANTVECTOR)
        {
            Array<Variant>@ attrs = target.attributes[resourcePickIndex].GetVariantVector();
            ResourceRef ref = attrs[resourcePickSubIndex].GetResourceRef();
            ref.type = ShortStringHash(resourcePicker.resourceType);
            ref.id = StringHash(resourceName);
            attrs[resourcePickSubIndex] = ref;
            target.attributes[resourcePickIndex] = Variant(attrs);
            target.ApplyAttributes();
        }
    }

    PostEditAttribute(resourceTargets, resourcePickIndex);
    UpdateAttributes(false);

    resourceTargets.Clear();
    @resourcePicker = null;
}

void PickResourceCanceled()
{
    if (!resourceTargets.empty)
    {
        resourceTargets.Clear();
        @resourcePicker = null;
        CloseFileSelector();
    }
}

void OpenResource(StringHash eventType, VariantMap& eventData)
{
    UIElement@ button = eventData["Element"].GetUIElement();
    LineEdit@ attrEdit = button.parent.children[0];
    
    String fileName = attrEdit.text.Trimmed();
    if (fileName.empty)
        return;

    Array<String>@ resourceDirs = cache.resourceDirs;
    for (uint i = 0; i < resourceDirs.length; ++i)
    {
        String fullPath = resourceDirs[i] + fileName;
        if (fileSystem.FileExists(fullPath))
        {
            fileSystem.SystemOpen(fullPath, "");
            return;
        }
    }
}

// VariantVector decoding & editing for certain components

class VectorStruct
{
    String componentTypeName;
    String attributeName;
    Array<String> variableNames;
    uint restartIndex;

    VectorStruct(const String&in componentTypeName_, const String&in attributeName_, const Array<String>@ variableNames_, uint restartIndex_)
    {
        componentTypeName = componentTypeName_;
        attributeName = attributeName_;
        variableNames = variableNames_;
        restartIndex = restartIndex_;
    }
};

Array<VectorStruct@> vectorStructs;

void InitVectorStructs()
{
    // Fill vector structure data
    Array<String> billboardVariables = {
        "Billboard Count",
        "   Position",
        "   Size",
        "   UV Coordinates",
        "   Color",
        "   Rotation",
        "   Is Enabled"
    };
    vectorStructs.Push(VectorStruct("BillboardSet", "Billboards", billboardVariables, 1));

    Array<String> animationStateVariables = {
        "Anim State Count",
        "   Animation",
        "   Start Bone",
        "   Is Looped",
        "   Weight",
        "   Time",
        "   Layer"
    };
    vectorStructs.Push(VectorStruct("AnimatedModel", "Animation States", animationStateVariables, 1));
}

VectorStruct@ GetVectorStruct(Array<Serializable@>@ serializables, uint index)
{
    AttributeInfo info = serializables[0].attributeInfos[index];
    for (uint i = 0; i < vectorStructs.length; ++i)
    {
        if (vectorStructs[i].componentTypeName == serializables[0].typeName && vectorStructs[i].attributeName == info.name)
            return vectorStructs[i];
    }
    return null;
}