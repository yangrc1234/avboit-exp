// SPDX-License-Identifier: MIT
#include "sample/MaterialTable.h"
#include <stdexcept>
#include <iostream>
#include <cmath>

int main()
{
    using namespace avboit;
    auto check = [](bool ok)
    {
        if (!ok)
            throw std::runtime_error("Material contract failed");
    };
    MaterialDefaults defaults{1.3f, 1.5f, 1.f, false};
    std::vector<MaterialRecord> vertices;
    for (unsigned kind : {1u, 1u, 2u, 4u, 3u})
    {
        vertices.push_back({0, 0, 4, float(kind)});
        vertices.push_back({.5f, .6f, .7f, .4f});
        vertices.push_back({0, 0, 0, .7f});
        vertices.push_back({0, 0, 12, 0});
    }
    MaterialTable table;
    table.Build(vertices, defaults);
    check(table.entries.size() == 4 && vertices[1][3] == vertices[5][3]);
    check(table.entries[0].ScalarExtinction());
    table.entries[0].transmission[0] = .2f;
    check(!table.entries[0].ScalarExtinction());
    check(table.GpuRecords(defaults)[3][1] == 0);
    check(table.Resolve(1, defaults).MaximumDisplacement() == 12);
    check(std::isinf(table.Resolve(2, defaults).MaximumDisplacement()));
    defaults.sphereIor = 1;
    check(table.Resolve(2, defaults).MaximumDisplacement() == 0);
    check(table.Resolve(2, defaults).FrostTheta() == 0);
    defaults.normalSphere = true;
    check(std::abs(table.Resolve(2, defaults).MaximumDisplacement() - 28.8f) < .0001f);
    auto &custom = table.entries[1];
    custom.inheritOptics = false;
    custom.ior = 1.8f;
    custom.gain = 2;
    custom.displacement = -10;
    defaults.gain = 4;
    check(table.Resolve(1, defaults).MaximumDisplacement() == 20);
    check(table.Resolve(3, defaults).value.gain == 1); // VFX does not inherit glass gain.
    auto records = table.GpuRecords(defaults);
    check(records.size() == 16 && records[6][0] == 1.8f && records[6][1] == 2);
    check(records[0][3] == .4f); // Coverage survived vertex-index replacement.
    check(table.Resolve(0, defaults).FrostTheta() == 0);
    table.entries[3].vfxPattern = 2;
    table.entries[3].vfxPhase = .35f;
    records = table.GpuRecords(defaults);
    check(records[15][2] == 2 && records[15][3] == .35f);
    // Imported glass normals must not turn normal.z into a refraction offset.
    vertices = {{{0, 0, 1, 2}}, {{.2f, .3f, .4f, 1}}, {{.9f, .95f, .97f, .26f}}, {{0, 0, -1, -2}}};
    table.Build(vertices, defaults);
    check(table.Resolve(0, defaults).MaximumDisplacement() == 0 && table.Resolve(0, defaults).FrostTheta() > 0);
    std::cout << "Material deduplication, inheritance, scalar/RGB classification and bounds contracts PASS\n";
}
