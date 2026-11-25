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
    check(records.size() == 4 * MaterialTable::GpuRecordCount &&
          records[MaterialTable::GpuRecordCount + 2][0] == 1.8f && records[MaterialTable::GpuRecordCount + 2][1] == 2);
    check(records[0][3] == .4f); // Coverage survived vertex-index replacement.
    check(table.Resolve(0, defaults).FrostTheta() == 0);
    table.entries[3].vfxPattern = 2;
    table.entries[3].vfxPhase = .35f;
    records = table.GpuRecords(defaults);
    check(records[3 * MaterialTable::GpuRecordCount + 3][2] == 2 &&
          records[3 * MaterialTable::GpuRecordCount + 3][3] == .35f);
    table.entries[1].flatWeightStrength = 4.f;
    table.entries[1].flatDepthRange = 2.f;
    records = table.GpuRecords(defaults);
    check(records[MaterialTable::GpuRecordCount + 5][0] == 4.f && records[MaterialTable::GpuRecordCount + 5][1] == 2.f);
    // Imported glass normals must not turn normal.z into a refraction offset.
    vertices = {{{0, 0, 1, 2}}, {{.2f, .3f, .4f, 1}}, {{.9f, .95f, .97f, .26f}}, {{0, 0, -1, -2}}};
    table.Build(vertices, defaults);
    check(table.Resolve(0, defaults).MaximumDisplacement() == 0 && table.Resolve(0, defaults).FrostTheta() > 0);
    table.entries[0].transmission[3] = 0;
    table.entries[0].cameraOffsetBins = 2.f;
    check(table.Resolve(0, defaults).OitCameraOffsetBins() == 2.f);
    check(table.GpuRecords(defaults)[4][0] == 2.f);
    table.entries[0].transmission[3] = .2f;
    check(table.Resolve(0, defaults).OitCameraOffsetBins() == 0); // Interface stays physical.
    // Legacy textured smoke coefficients are converted once. A colored analytic volume keeps all three.
    const MaterialRecord rgb = {.25f, .5f, 1.f, 0.f};
    vertices = {{{0, 0, 1, 6}}, {{.2f, .3f, .4f, 1}}, rgb, {},
                {{0, 0, 1, 5}}, {{.2f, .3f, .4f, 1}}, rgb, {}};
    table.Build(vertices, defaults);
    records = table.GpuRecords(defaults);
    const float gray = .2126f * rgb[0] + .7152f * rgb[1] + .0722f * rgb[2];
    check(std::abs(records[1][0] - gray) < 1e-6f && records[1][0] == records[1][1] && records[1][1] == records[1][2]);
    check(records[MaterialTable::GpuRecordCount + 1] == rgb);
    std::cout << "Material deduplication, inheritance, scalar/RGB classification and bounds contracts PASS\n";
}
