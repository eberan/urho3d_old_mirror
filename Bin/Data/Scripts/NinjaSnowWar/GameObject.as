const int CTRL_UP = 1;
const int CTRL_DOWN = 2;
const int CTRL_LEFT = 4;
const int CTRL_RIGHT = 8;
const int CTRL_FIRE = 16;
const int CTRL_JUMP = 32;
const int CTRL_ALL = 63;

const int SIDE_NEUTRAL = 0;
const int SIDE_PLAYER = 1;
const int SIDE_ENEMY = 2;

class GameObject : ScriptObject
{
    bool onGround;
    bool isSliding;
    float duration;
    int health;
    int maxHealth;
    int side;
    int lastDamageSide;
    uint lastDamageCreatorID;
    uint creatorID;

    GameObject()
    {
        onGround = false;
        isSliding = false;
        duration = -1; // Infinite
        health = 0;
        maxHealth = 0;
        side = SIDE_NEUTRAL;
        lastDamageSide = SIDE_NEUTRAL;
        lastDamageCreatorID = 0;
        creatorID = 0;

        if (runClient)
            Print("Warning! Logic object created on client!");
    }

    void FixedUpdate(float timeStep)
    {
        // Disappear when duration expired
        if (duration >= 0)
        {
            duration -= timeStep;
            if (duration <= 0)
                node.Remove();
        }
    }

    bool Damage(GameObject@ origin, int amount)
    {
        if ((origin.side == side) || (health == 0))
            return false;

        lastDamageSide = origin.side;
        lastDamageCreatorID = origin.creatorID;
        health -= amount;
        if (health < 0)
            health = 0;
        return true;
    }

    bool Heal(int amount)
    {
        // By default do not heal
        return false;
    }

    void PlaySound(const String&in soundName)
    {
        // Create the sound channel
        SoundSource3D@ source = node.CreateComponent("SoundSource3D");
        Sound@ sound = cache.GetResource("Sound", soundName);

        source.SetDistanceAttenuation(200, 5000, 1);
        source.Play(sound);
        source.autoRemove = true;
    }

    Node@ SpawnObject(const Vector3&in position, const Quaternion&in rotation, const String&in className)
    {
        XMLFile@ xml = cache.GetResource("XMLFile", "Objects/" + className + ".xml");
        return scene.InstantiateXML(xml, position, rotation);
    }

    Node@ SpawnParticleEffect(const Vector3&in position, const String&in effectName, float duration)
    {
        Node@ newNode = scene.CreateChild();
        newNode.position = position;

        // Create the particle emitter
        ParticleEmitter@ emitter = newNode.CreateComponent("ParticleEmitter");
        emitter.parameters = cache.GetResource("XMLFile", effectName);

        // Create a GameObject for managing the effect lifetime
        GameObject@ object = cast<GameObject>(newNode.CreateScriptObject(scriptFile, "GameObject", LOCAL));
        object.duration = duration;

        return newNode;
    }

    Node@ SpawnSound(const Vector3&in position, const String&in soundName, float duration)
    {
        Node@ newNode = scene.CreateChild();
        newNode.position = position;

        // Create the sound source
        SoundSource3D@ source = newNode.CreateComponent("SoundSource3D");
        Sound@ sound = cache.GetResource("Sound", soundName);
        source.SetDistanceAttenuation(200, 5000, 1);
        source.Play(sound);

        // Create a GameObject for managing the sound lifetime
        GameObject@ object = cast<GameObject>(newNode.CreateScriptObject(scriptFile, "GameObject", LOCAL));
        object.duration = duration;

        return newNode;
    }

    void HandleNodeCollision(StringHash eventType, VariantMap& eventData)
    {
        Node@ otherNode = eventData["OtherNode"].GetNode();
        RigidBody@ otherBody = eventData["OtherBody"].GetRigidBody();

        // If the other collision shape belongs to static geometry, perform world collision
        if (otherBody.collisionLayer == 2)
            WorldCollision(eventData);

        // If the other node is scripted, perform object-to-object collision
        GameObject@ otherObject = cast<GameObject>(otherNode.scriptObject);
        if (otherObject !is null)
            ObjectCollision(otherObject, eventData);
    }

    void WorldCollision(VariantMap& eventData)
    {
        VectorBuffer contacts = eventData["Contacts"].GetBuffer();
        while (!contacts.eof)
        {
            Vector3 contactPosition = contacts.ReadVector3();
            Vector3 contactNormal = contacts.ReadVector3();
            float contactDistance = contacts.ReadFloat();
            float contactImpulse = contacts.ReadFloat();

            // If contact is below node center and mostly vertical, assume it's ground contact
            if (contactPosition.y < node.position.y)
            {
                float level = Abs(contactNormal.y);
                if (level > 0.75)
                    onGround = true;
                else
                {
                    // If contact is somewhere inbetween vertical/horizontal, is sliding a slope
                    if (level > 0.1)
                        isSliding = true;
                }
            }
        }

        // Ground contact has priority over sliding contact
        if (onGround == true)
            isSliding = false;
    }

    void ObjectCollision(GameObject@ otherObject, VariantMap& eventData)
    {
    }

    void ResetWorldCollision()
    {
        RigidBody@ body = node.GetComponent("RigidBody");
        if (body.active)
        {
            onGround = false;
            isSliding = false;
        }
        else
        {
            // If body is not active, assume it rests on the ground
            onGround = true;
            isSliding = false;
        }
    }
}
