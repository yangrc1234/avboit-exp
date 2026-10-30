// SPDX-License-Identifier: MIT
// NativeViewer's editor bridge. SceneActor owns authoring state; GPU object IDs
// remain local to their opaque/transparent draw ranges.
std::vector<avboit::SceneActor> actors;
std::vector<F4> actorRestVertices;
std::vector<unsigned> opaqueActors;
avboit::TransformGizmo actorGizmo;
bool showActorGizmo = true, rotateActorGizmo = false, actorGeometryDirty = false;
ImGuiTextFilter actorFilter;

void BuildSceneActors(unsigned emissiveFirst, unsigned emissiveEnd)
{
    using Type = avboit::ActorType;
    actors.clear();
    opaqueActors.clear();
    const auto imported = materials.entries;
    materials.entries.clear();
    auto mesh = [&](Type type, unsigned first, unsigned end, unsigned index)
    {
        avboit::SceneActor a;
        a.type = type;
        a.firstRecord = first;
        a.endRecord = end;
        a.renderIndex = index;
        a.material = unsigned(materials.entries.size());
        auto material = imported.at(unsigned(vertices[first + 1][3]));
        const auto kind = material.kind;
        bool emissive = first >= emissiveFirst && first < emissiveEnd;
        a.name = type == Type::Distortion ? "VFX distortion"
                 : type == Type::Opaque   ? (emissive ? "Emissive" : "Opaque fixture")
                 : kind == 6              ? "Volume smoke"
                 : kind == 5              ? "Homogeneous volume"
                 : kind == 1              ? "Smoke card"
                 : kind == 4              ? "Glass sphere"
                                          : "Glass";
        if (emissive)
            for (unsigned c = 0; c < 3; ++c)
                material.color[c] *= emissiveGain;
        if (type == Type::Distortion)
        {
            material.inheritOptics = false;
            material.gain = vfxStrength;
            a.enabled = vfx && (vfxLayers & (1u << index));
        }
        materials.entries.push_back(material);
        a.name += " / " + std::to_string(actors.size());
        a.lower = {1e30f, 1e30f, 1e30f};
        a.upper = {-1e30f, -1e30f, -1e30f};
        for (unsigned v = first; v < end; v += 4)
        {
            vertices[v + 1][3] = float(a.material);
            if (kind >= 5)
            {
                // Material import consumed these RGB lanes. Store OBB axes;
                // W retains material/actor IDs. Z is the cross product.
                for (unsigned c = 0; c < 3; ++c)
                {
                    vertices[v + 1][c] = c == 0 ? 1.f : 0.f;
                    vertices[v + 2][c] = c == 1 ? 1.f : 0.f;
                }
            }
            for (unsigned c = 0; c < 3; ++c)
            {
                float extent = kind >= 5 ? vertices[v + 3][c] : 0.f;
                a.lower[c] = std::min(a.lower[c], vertices[v][c] - extent);
                a.upper[c] = std::max(a.upper[c], vertices[v][c] + extent);
            }
        }
        for (unsigned c = 0; c < 3; ++c)
            a.position[c] = a.homePosition[c] = (a.lower[c] + a.upper[c]) * .5f;
        if (type == Type::Transparent)
            objectMaterials[index] = a.material;
        if (type == Type::Opaque)
            opaqueActors.push_back(unsigned(actors.size()));
        actors.push_back(a);
    };
    // Keep transparent actor indices stable for occupancy and existing tests.
    for (unsigned i = 0; i < objectFirstRecords.size(); ++i)
        mesh(Type::Transparent, objectFirstRecords[i],
             i + 1 < objectFirstRecords.size() ? objectFirstRecords[i + 1] : (opaqueVertices + transparentVertices) * 4,
             i);
    for (unsigned i = 0; i < opaqueFirstRecords.size(); ++i)
        mesh(Type::Opaque, opaqueFirstRecords[i],
             i + 1 < opaqueFirstRecords.size() ? opaqueFirstRecords[i + 1] : opaqueVertices * 4, i);
    for (unsigned i = 0; i < vfxVertices / 6; ++i)
    {
        unsigned first = (opaqueVertices + transparentVertices + i * 6) * 4;
        mesh(Type::Distortion, first, first + 24, i);
    }
    if (useDonutScene)
    {
        avboit::SceneActor sun, spot;
        sun.type = Type::Sun;
        sun.name = "Sun";
        sun.lightIntensity = sceneLighting.sunIntensity;
        sun.position = sun.homePosition = {0, 5, 4};
        sun.rotation = sun.homeRotation = avboit::DirectionRotation(sceneLighting.sunDirection);
        spot.type = Type::Spotlight;
        spot.name = "Spotlight";
        spot.lightIntensity = sceneLighting.spotIntensity;
        spot.animate = sceneLighting.animateSpot;
        spot.position = spot.homePosition = sceneLighting.spotPosition;
        F3 direction;
        for (unsigned c = 0; c < 3; ++c)
            direction[c] = sceneLighting.spotTarget[c] - spot.position[c];
        spot.rotation = spot.homeRotation = avboit::DirectionRotation(direction);
        for (auto a : {sun, spot})
        {
            for (unsigned c = 0; c < 3; ++c)
            {
                a.lower[c] = a.position[c] - .15f;
                a.upper[c] = a.position[c] + .15f;
            }
            actors.push_back(a);
        }
    }
    actorRestVertices = vertices;
}
F3 ActorCenter(unsigned actor) const
{
    return actors.at(actor).position;
}
void ApplyActorTransform(unsigned actor)
{
    auto &a = actors.at(actor);
    auto basis = avboit::RotationBasis(a.rotation);
    a.lower = {1e30f, 1e30f, 1e30f};
    a.upper = {-1e30f, -1e30f, -1e30f};
    if (a.firstRecord == a.endRecord)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            a.lower[c] = a.position[c] - .15f;
            a.upper[c] = a.position[c] + .15f;
        }
    }
    else
    {
        const unsigned kind = materials.entries[a.material].kind;
        for (unsigned v = a.firstRecord; v < a.endRecord; v += 4)
        {
            F3 offset;
            for (unsigned c = 0; c < 3; ++c)
                offset[c] = actorRestVertices[v][c] - a.homePosition[c];
            auto rotated = avboit::Rotate(basis, offset);
            for (unsigned c = 0; c < 3; ++c)
            {
                vertices[v][c] = a.position[c] + rotated[c];
                float extent = 0;
                if (kind >= 5)
                    for (unsigned b = 0; b < 3; ++b)
                        extent += std::abs(basis[b][c]) * actorRestVertices[v + 3][b];
                a.lower[c] = std::min(a.lower[c], vertices[v][c] - extent);
                a.upper[c] = std::max(a.upper[c], vertices[v][c] + extent);
                if (kind >= 5)
                {
                    vertices[v + 1][c] = basis[0][c];
                    vertices[v + 2][c] = basis[1][c];
                }
            }
            if (kind == 4 || (kind == 0 && actorRestVertices[v + 3][3] > 0) ||
                (kind == 2 && actorRestVertices[v + 3][3] < -1.5f))
            {
                F3 normal{actorRestVertices[v + 3][0], actorRestVertices[v + 3][1], actorRestVertices[v + 3][2]};
                normal = avboit::Rotate(basis, normal);
                for (unsigned c = 0; c < 3; ++c)
                    vertices[v + 3][c] = normal[c];
            }
        }
        if (a.type == avboit::ActorType::Transparent)
            for (unsigned c = 0; c < 3; ++c)
            {
                bounds[3 + a.renderIndex * 2][c] = a.lower[c];
                bounds[4 + a.renderIndex * 2][c] = a.upper[c];
            }
        for (unsigned v = 0; v < localVolumeRecords.size(); ++v)
            if (localVolumeRecords[v] == a.firstRecord)
            {
                localVolumes[v].center = a.position;
                localVolumes[v].rotation = a.rotation;
            }
        actorGeometryDirty = true;
    }
    if (temporalAA)
        temporalAA->Invalidate();
}
void SetActorPosition(unsigned actor, F3 position)
{
    for (float x : position)
        if (!std::isfinite(x))
            return;
    actors.at(actor).position = position;
    ApplyActorTransform(actor);
}
void TranslateActor(unsigned actor, F3 delta)
{
    auto position = ActorCenter(actor);
    for (unsigned c = 0; c < 3; ++c)
        position[c] += delta[c];
    SetActorPosition(actor, position);
}
void SetActorRotation(unsigned actor, F3 rotation)
{
    for (float x : rotation)
        if (!std::isfinite(x))
            return;
    actors.at(actor).rotation = rotation;
    ApplyActorTransform(actor);
}
void ResetActor(unsigned actor)
{
    actors[actor].position = actors[actor].homePosition;
    SetActorRotation(actor, actors[actor].homeRotation);
}
F3 ActorAnimation(unsigned actor) const
{
    F3 offset{};
    for (const auto &motion : fixtureMotions)
        if (actors[actor].type == avboit::ActorType::Transparent && motion.object == actors[actor].renderIndex)
            for (unsigned c = 0; c < 3; ++c)
                offset[c] += motion.amplitude[c] * std::sin(time * motion.frequency);
    if (actors[actor].type == avboit::ActorType::Spotlight && actors[actor].animate)
        offset[0] += 1.5f * std::sin(time * .6f);
    return offset;
}
void ActorDisplayBounds(unsigned actor, F3 &low, F3 &high, F3 &center) const
{
    auto offset = ActorAnimation(actor);
    for (unsigned c = 0; c < 3; ++c)
    {
        low[c] = actors[actor].lower[c] + offset[c];
        high[c] = actors[actor].upper[c] + offset[c];
        center[c] = actors[actor].position[c] + offset[c];
    }
}
void DrawActorGizmo()
{
    if (!showActorGizmo || selectedActor < 0 || unsigned(selectedActor) >= actors.size() ||
        std::find(viewObjects.begin(), viewObjects.end(), unsigned(selectedActor)) != viewObjects.end())
    {
        actorGizmo.Reset();
        return;
    }
    unsigned actor = unsigned(selectedActor);
    F3 low, high, center, delta;
    ActorDisplayBounds(actor, low, high, center);
    avboit::TransformGizmo::View view{navigation.Eye(),     navigation.Right(),         navigation.Up(),
                                      navigation.Forward(), ImGui::GetIO().DisplaySize, float(Width) / Height};
    auto rotation = actors[actor].rotation;
    if (rotateActorGizmo)
    {
        if (actorGizmo.DrawRotation(view, center, rotation))
            SetActorRotation(actor, rotation);
    }
    else if (actorGizmo.Draw(view, center, low, high, delta))
        TranslateActor(actor, delta);
    if (actorGizmo.Active())
        animate = false;
}
double pointerX = 0, pointerY = 0;
bool preserveMaterialsOnRebuild = false;
std::string ActorLabel(unsigned actor) const
{
    return actors.at(actor).name;
}
void SelectActor(unsigned actor)
{
    if (actor >= actors.size())
        return;
    actorGizmo.Reset();
    selectedActor = int(actor);
    selectedMaterial = int(actors[actor].material);
}
void PickActor()
{
    if (!ImGui::GetCurrentContext() || ImGui::GetIO().WantCaptureMouse ||
        actorGizmo.HitTest(ImVec2(float(pointerX), float(pointerY))))
        return;
    auto size = ImGui::GetIO().DisplaySize;
    if (size.x <= 0 || size.y <= 0)
        return;
    float x = (float(pointerX) / size.x * 2.f - 1.f) * (float(Width) / Height) * .57735027f;
    float y = (1.f - float(pointerY) / size.y * 2.f) * .57735027f;
    F3 direction;
    for (unsigned c = 0; c < 3; ++c)
        direction[c] = navigation.Forward()[c] + x * navigation.Right()[c] + y * navigation.Up()[c];
    float nearest = 80.f;
    int hit = -1;
    for (unsigned actor = 0; actor < actors.size(); ++actor)
    {
        if (!actors[actor].enabled)
            continue;
        F3 origin = navigation.Eye(), ray = direction;
        if (std::find(viewObjects.begin(), viewObjects.end(), actor) != viewObjects.end())
        {
            origin = {0, 0, 0};
            ray = {x, y, 1};
        }
        F3 low, high, center;
        ActorDisplayBounds(actor, low, high, center);
        float enter = .05f, leave = nearest;
        for (unsigned c = 0; c < 3; ++c)
        {
            if (std::abs(ray[c]) < 1e-7f)
            {
                if (origin[c] < low[c] || origin[c] > high[c])
                    leave = -1;
            }
            else
            {
                float a = (low[c] - origin[c]) / ray[c], b = (high[c] - origin[c]) / ray[c];
                enter = std::max(enter, std::min(a, b));
                leave = std::min(leave, std::max(a, b));
            }
        }
        if (leave >= enter && enter < nearest)
        {
            nearest = enter;
            hit = int(actor);
        }
    }
    if (hit >= 0)
        SelectActor(unsigned(hit));
}
avboit::SceneLightingSettings ActorLighting() const
{
    auto settings = sceneLighting;
    // Animate the light actor before deriving its target, preserving rotation.
    settings.animateSpot = false;
    for (unsigned i = 0; i < actors.size(); ++i)
    {
        const auto &a = actors[i];
        auto forward = avboit::RotationBasis(a.rotation)[2];
        if (a.type == avboit::ActorType::Sun)
        {
            settings.sunDirection = forward;
            settings.sunIntensity = a.enabled ? a.lightIntensity : 0;
        }
        if (a.type == avboit::ActorType::Spotlight)
        {
            auto offset = ActorAnimation(i);
            for (unsigned c = 0; c < 3; ++c)
            {
                settings.spotPosition[c] = a.position[c] + offset[c];
                settings.spotTarget[c] = settings.spotPosition[c] + forward[c];
            }
            settings.spotIntensity = a.enabled ? a.lightIntensity : 0;
        }
    }
    return settings;
}
void DrawActorEditor()
{
    ImGui::TextDisabled("Click an actor or filter the list below.");
    actorFilter.Draw("##actor-filter", -1);
    if (ImGui::BeginListBox("##actors", ImVec2(-1, 130)))
    {
        for (unsigned i = 0; i < actors.size(); ++i)
            if (actorFilter.PassFilter(actors[i].name.c_str()) &&
                ImGui::Selectable(actors[i].name.c_str(), selectedActor == int(i)))
                SelectActor(i);
        ImGui::EndListBox();
    }
    if (selectedActor < 0 || unsigned(selectedActor) >= actors.size())
        return;
    auto &a = actors[selectedActor];
    ImGui::Text("Selected: %s", a.name.c_str());
    if (ImGui::Checkbox("Enabled", &a.enabled))
    {
        interfaceSelectionDirty = true;
        if (temporalAA)
            temporalAA->Invalidate();
    }
    F3 position = a.position, rotation = a.rotation;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat3("##actor-position", position.data(), .02f, -1000.f, 1000.f, "%.3f"))
        SetActorPosition(unsigned(selectedActor), position);
    ImGui::TextDisabled("Position XYZ (world units)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloat3("##actor-rotation", rotation.data(), .5f, 0, 0, "%.1f"))
        SetActorRotation(unsigned(selectedActor), rotation);
    ImGui::TextDisabled("Rotation XYZ (degrees). Ctrl-click to type.");
    if (ImGui::Checkbox("Gizmo", &showActorGizmo))
        actorGizmo.Reset();
    ImGui::SameLine();
    if (ImGui::RadioButton("Move", !rotateActorGizmo))
    {
        rotateActorGizmo = false;
        actorGizmo.Reset();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", rotateActorGizmo))
    {
        rotateActorGizmo = true;
        actorGizmo.Reset();
    }
    if (ImGui::Button("Reset transform"))
        ResetActor(unsigned(selectedActor));
    ImGui::SameLine();
    if (ImGui::Button("Reset all transforms"))
        for (unsigned i = 0; i < actors.size(); ++i)
            ResetActor(i);
    ImGui::Separator();
    using Type = avboit::ActorType;
    bool lightingChanged = false;
    if (a.type == Type::Sun)
        lightingChanged = ImGui::SliderFloat("Irradiance", &a.lightIntensity, 0.f, 12.f);
    else if (a.type == Type::Spotlight)
    {
        lightingChanged = ImGui::SliderFloat("Intensity", &a.lightIntensity, 0.f, 500.f);
        lightingChanged |= ImGui::Checkbox("Animate light", &a.animate);
        ImGui::TextDisabled("Uses the scene playback clock.");
    }
    else
        avboit::DrawMaterialEditor(materials, MaterialDefaults(), selectedMaterial);
    if (lightingChanged && temporalAA)
        temporalAA->Invalidate();
}
