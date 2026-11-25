// SPDX-License-Identifier: MIT
#include "sample/SmokeCoverage.h"
#include "sample/SmokeGpuBricks.h"
#include "sample/SmokeTrails.h"
#include <cstdio>
#include <stdexcept>
using namespace avboit;
static void Check(bool v)
{
    if (!v)
        throw std::runtime_error("Coverage replication contract");
}
int main()
{
    try
    {
        SmokeTrails trails;
        for (unsigned i = 0; i < 20; ++i)
            trails.Add({float(i), 0, 0}, {float(i), 0, 10}, .4f, 2.f);
        Check(trails.Count() == 15 && trails.Pack()[1][0] == 5.f);
        Check(trails.Pack()[2][3] == 1.f);
        trails.Tick(1.4f);
        Check(std::abs(trails.Pack()[2][3] - .5f) < 1e-5f);
        trails.Tick(.6f);
        Check(trails.Count() == 0 && trails.Pack()[0][0] == 0.f);
        trails.Add({}, {}, 0, 1);
        Check(trails.Count() == 0);
        trails.Add({}, {}, .4f, 1); // Degenerate capsule is a sphere, not an invalid segment.
        Check(trails.Count() == 1);
        trails.Clear();
        Check(trails.Count() == 0);
        auto closed = MakeGrenadeCoverage(false, 48, 1);
        SmokeCoverage client;
        auto snapshot = closed.Encode();
        Check(client.Apply(snapshot));
        Check(client.Expand() == closed.Expand());
        auto open = MakeGrenadeCoverage(true, 48, 2);
        auto delta = open.Encode(&closed);
        Check(client.Apply(delta));
        Check(client.Expand() == open.Expand());
        Check(!client.Apply(delta));
        Check(!client.Apply(snapshot));
        auto retract = MakeGrenadeCoverage(false, 20, 3);
        auto removal = retract.Encode(&open);
        Check(client.Apply(removal));
        Check(client.bricks == retract.bricks);
        SmokeCoverage lost;
        Check(lost.Apply(snapshot));
        Check(!lost.Apply(removal));
        Check(lost.Apply(retract.Encode()));
        Check(lost.bricks == retract.bricks);
        for (size_t size = 0; size < snapshot.size(); size += 17)
        {
            auto truncated = snapshot;
            truncated.resize(size);
            SmokeCoverage c;
            Check(!c.Apply(truncated));
        }
        auto corrupted = delta;
        corrupted[corrupted.size() / 2] ^= 1;
        auto before = client.Encode();
        Check(!client.Apply(corrupted));
        Check(client.Encode() == before);
        SmokeCoverage full;
        full.size = 8;
        full.revision = 1;
        for (unsigned z = 0; z < 8; ++z)
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x)
                    full.Set(x, y, z);
        Check(full.Encode().size() == 57);
        SmokeCoverage fullCopy;
        Check(fullCopy.Apply(full.Encode()));
        Check(fullCopy.bricks == full.bricks);
        auto feather = open.Expand();
        auto original = feather;
        FeatherSmokeCoverage(feather, open.size);
        for (size_t i = 0; i < feather.size(); ++i)
            Check(original[i] || !feather[i]);
        auto grid = MakeGrenadeCoverage(true, 40.f, 1, 80).Expand();
        // The visual demo must not retain the replication fixture's invisible wall.
        auto unobstructed = MakeGrenadeCoverage(true, 40.f, 1, 80, false).Expand();
        for (int z = 0; z < 80; ++z)
            for (int y = 0; y < 80; ++y)
                for (int x = 0; x < 80; ++x)
                {
                    int r2 = (x - 40) * (x - 40) + (y - 40) * (y - 40) + (z - 40) * (z - 40);
                    Check(bool(unobstructed[x + 80 * (y + 80 * z)]) == (r2 <= 1600));
                }
        FeatherSmokeCoverage(grid, 80, 2);
        for (unsigned mode = 1; mode <= 2; ++mode)
        {
            SmokeGpuBricks gpu(grid, 80, mode, true);
            for (unsigned bz = 0; bz < 10; ++bz)
                for (unsigned by = 0; by < 10; ++by)
                    for (unsigned bx = 0; bx < 10; ++bx)
                        for (unsigned z = 0; z < 9; ++z)
                            for (unsigned y = 0; y < 9; ++y)
                                for (unsigned x = 0; x < 9; ++x)
                                {
                                    unsigned entry = gpu.words[9 + bx + 10 * (by + 10 * bz)],
                                             value = entry == 1 ? 255 : 0;
                                    if (entry >= 2)
                                    {
                                        unsigned r = entry - 2, index = x + 9 * (y + 9 * z);
                                        if (mode == 1)
                                            value =
                                                (gpu.words[gpu.words[4] + r * 183 + index / 4] >> ((index % 4) * 8)) &
                                                255;
                                        else
                                        {
                                            unsigned axis = gpu.words[5], n = gpu.textureSize;
                                            value = gpu.texture[(r % axis) * 9 + x +
                                                                n * (((r / axis) % axis) * 9 + y +
                                                                     n * ((r / (axis * axis)) * 9 + z))];
                                        }
                                    }
                                    Check(value ==
                                          grid[std::min(bx * 8 + x, 79u) +
                                               80 * (std::min(by * 8 + y, 79u) + 80 * std::min(bz * 8 + z, 79u))]);
                                }
            printf("GPU storage %u: %u mixed, %zu buffer + %zu texture bytes, apron reconstruction PASS\n", mode,
                   gpu.mixed, gpu.words.size() * 4, gpu.texture.size());
        }
        auto c = closed.Expand(), o = open.Expand();
        size_t rightClosed = 0, rightOpen = 0;
        for (unsigned z = 0; z < 128; ++z)
            for (unsigned y = 0; y < 128; ++y)
                for (unsigned x = 66; x < 128; ++x)
                {
                    rightClosed += c[x + 128 * (y + 128 * z)] != 0;
                    rightOpen += o[x + 128 * (y + 128 * z)] != 0;
                }
        Check(rightClosed == 0 && rightOpen > 0);
        printf(
            "Smoke coverage PASS: snapshot %zu B, open-door delta %zu B, removal %zu B; right-side cells %zu -> %zu\n",
            snapshot.size(), delta.size(), removal.size(), rightClosed, rightOpen);
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
