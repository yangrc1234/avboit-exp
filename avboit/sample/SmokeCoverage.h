// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <vector>
#include <stdexcept>
#include <algorithm>

namespace avboit
{
// Authority data only. No GPU handles, VDB frame, floating density or lighting.
// Coordinates are integer millimetres; revisions are per smoke instance.
struct SmokeCoverage
{
    using Brick = std::array<uint32_t, 16>; // 8^3 binary cells
    uint32_t id = 1, revision = 0, size = 128, cellMm = 50, seed = 1;
    std::array<int32_t, 3> originMm{};
    std::map<uint32_t, Brick> bricks;

    void Set(uint32_t x, uint32_t y, uint32_t z)
    {
        if (x >= size || y >= size || z >= size)
            throw std::out_of_range("Smoke cell");
        uint32_t n = size / 8, key = x / 8 + n * (y / 8 + n * (z / 8));
        uint32_t bit = x % 8 + 8 * (y % 8 + 8 * (z % 8));
        bricks[key][bit / 32] |= 1u << (bit % 32);
    }
    std::vector<uint8_t> Expand() const
    {
        std::vector<uint8_t> pixels(size_t(size) * size * size, 0);
        uint32_t n = size / 8;
        for (const auto &entry : bricks)
        {
            uint32_t bx = entry.first % n, by = (entry.first / n) % n, bz = entry.first / (n * n);
            for (uint32_t bit = 0; bit < 512; ++bit)
                if (entry.second[bit / 32] & (1u << (bit % 32)))
                    pixels[bx * 8 + bit % 8 + size * (by * 8 + (bit / 8) % 8 + size * (bz * 8 + bit / 64))] = 255;
        }
        return pixels;
    }
    bool SameGrid(const SmokeCoverage &b) const
    {
        return id == b.id && size == b.size && cellMm == b.cellMm && originMm == b.originMm && seed == b.seed;
    }
    static uint32_t Hash(const std::vector<uint8_t> &data, size_t length)
    {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < length; ++i)
            h = (h ^ data[i]) * 16777619u;
        return h;
    }
    // Explicit little-endian wire format, never serialize native C++ structs.
    std::vector<uint8_t> Encode(const SmokeCoverage *base = nullptr) const
    {
        if (revision == 0 || (base && (!SameGrid(*base) || revision <= base->revision)))
            throw std::invalid_argument("Invalid smoke delta baseline");
        std::map<uint32_t, Brick> changes;
        for (const auto &b : bricks)
            if (!base || !base->bricks.count(b.first) || base->bricks.at(b.first) != b.second)
                changes[b.first] = b.second;
        if (base)
            for (const auto &b : base->bricks)
                if (!bricks.count(b.first))
                    changes[b.first] = {};
        std::vector<uint8_t> out;
        auto put = [&](uint32_t v)
        {
            for (int j = 0; j < 4; ++j)
                out.push_back(uint8_t(v >> (j * 8)));
        };
        put(0x314b4d53);
        put(1);
        put(id);
        put(revision);
        put(base ? base->revision : 0);
        put(size);
        put(cellMm);
        for (auto v : originMm)
            put(uint32_t(v));
        put(seed);
        put(uint32_t(changes.size()));
        for (const auto &b : changes)
        {
            put(b.first);
            bool full = true, empty = true;
            for (auto word : b.second)
            {
                full &= word == 0xffffffffu;
                empty &= word == 0;
            }
            out.push_back(empty ? 0 : full ? 1 : 2);
            if (!empty && !full)
                for (auto word : b.second)
                    put(word);
        }
        put(Hash(out, out.size()));
        return out;
    }
    // Transactional receiver: missing/out-of-order revisions request a snapshot.
    // Transport authentication, ACKs and fragmentation belong to the network layer.
    bool Apply(const std::vector<uint8_t> &packet)
    {
        if (packet.size() < 52 || packet.size() > 3 * 1024 * 1024)
            return false;
        size_t offset = 0;
        auto read = [&]() -> uint32_t
        {
            if (offset + 4 > packet.size() - 4)
                throw std::runtime_error("Truncated packet");
            uint32_t v = 0;
            for (int j = 0; j < 4; ++j)
                v |= uint32_t(packet[offset++]) << (8 * j);
            return v;
        };
        uint32_t checksum = 0;
        for (int j = 0; j < 4; ++j)
            checksum |= uint32_t(packet[packet.size() - 4 + j]) << (8 * j);
        if (Hash(packet, packet.size() - 4) != checksum)
            return false;
        try
        {
            if (read() != 0x314b4d53 || read() != 1)
                return false;
            SmokeCoverage next;
            next.id = read();
            next.revision = read();
            uint32_t base = read();
            next.size = read();
            next.cellMm = read();
            for (auto &v : next.originMm)
                v = int32_t(read());
            next.seed = read();
            uint32_t count = read();
            if (next.size < 8 || next.size > 256 || next.size % 8 || !next.cellMm || !next.revision)
                return false;
            uint32_t n = next.size / 8, capacity = n * n * n;
            if (count > capacity || (revision && (next.id != id || next.revision <= revision)))
                return false;
            if (base)
            {
                if (base != revision || !SameGrid(next))
                    return false;
                next.bricks = bricks;
            }
            uint32_t previous = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t key = read();
                if (key >= capacity || (i && key <= previous) || offset >= packet.size() - 4)
                    return false;
                previous = key;
                uint8_t type = packet[offset++];
                Brick b{};
                if (type == 1)
                    b.fill(0xffffffffu);
                else if (type == 2)
                {
                    for (auto &word : b)
                        word = read();
                }
                else if (type != 0)
                    return false;
                if (type == 0)
                    next.bricks.erase(key);
                else
                    next.bricks[key] = b;
            }
            if (offset != packet.size() - 4)
                return false;
            *this = std::move(next);
            return true;
        }
        catch (const std::runtime_error &)
        {
            return false;
        }
    }
};

// Client-only inward feather: never grows authoritative coverage. Linear-time
// Manhattan distance transform; six cells is 15 cm at the demo's 25 mm spacing.
inline void FeatherSmokeCoverage(std::vector<uint8_t> &mask, unsigned n, unsigned featherCells = 6)
{
    const unsigned plane = n * n;
    for (unsigned z = 0; z < n; ++z)
        for (unsigned y = 0; y < n; ++y)
            for (unsigned x = 0; x < n; ++x)
            {
                auto &d = mask[x + n * y + plane * z];
                if (!d)
                    continue;
                d = uint8_t(std::min({featherCells, x ? unsigned(mask[x - 1 + n * y + plane * z]) + 1 : 1u,
                                      y ? unsigned(mask[x + n * (y - 1) + plane * z]) + 1 : 1u,
                                      z ? unsigned(mask[x + n * y + plane * (z - 1)]) + 1 : 1u}));
            }
    for (int z = int(n) - 1; z >= 0; --z)
        for (int y = int(n) - 1; y >= 0; --y)
            for (int x = int(n) - 1; x >= 0; --x)
            {
                auto &d = mask[x + n * y + plane * z];
                if (!d)
                    continue;
                d = uint8_t(std::min({unsigned(d), x + 1 < int(n) ? unsigned(mask[x + 1 + n * y + plane * z]) + 1 : 1u,
                                      y + 1 < int(n) ? unsigned(mask[x + n * (y + 1) + plane * z]) + 1 : 1u,
                                      z + 1 < int(n) ? unsigned(mask[x + n * y + plane * (z + 1)]) + 1 : 1u}));
            }
    for (auto &d : mask)
    {
        float t = float(d) / float(featherCells);
        d = uint8_t(255.f * t * t * (3.f - 2.f * t));
    }
}

// Synthetic gameplay example, NOT a Sponza collision solver. Six-neighbour
// flood propagation through a doorway, clipped to a sphere and a finite range.
inline SmokeCoverage MakeGrenadeCoverage(bool doorOpen, float radius, uint32_t revision, int n = 128,
                                         bool syntheticWall = true)
{
    SmokeCoverage result;
    result.size = unsigned(n);
    result.revision = revision;
    const int sx = n == 128 ? 42 : n / 2, sy = n == 128 ? 55 : n / 2, sz = n / 2;
    std::vector<int> distance(n * n * n, -1), queue;
    int first = sx + n * (sy + n * sz);
    distance[first] = 0;
    queue.push_back(first);
    const int directions[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t head = 0; head < queue.size(); ++head)
    {
        int cell = queue[head], x = cell % n, y = (cell / n) % n, z = cell / (n * n);
        result.Set(x, y, z);
        for (auto &d : directions)
        {
            int a = x + d[0], b = y + d[1], c = z + d[2];
            if (a < 0 || a >= n || b < 0 || b >= n || c < 0 || c >= n)
                continue;
            int key = a + n * (b + n * c);
            if (distance[key] >= 0 || distance[cell] >= int(radius * 1.8f))
                continue;
            if ((a - sx) * (a - sx) + (b - sy) * (b - sy) + (c - sz) * (c - sz) > radius * radius)
                continue;
            bool wall = a >= n / 2 - 2 && a <= n / 2 + 1,
                 door = b < n * 83 / 128 && c >= n * 48 / 128 && c <= n * 80 / 128;
            if (n != 128)
                wall = a >= n / 2 + 8 && a <= n / 2 + 9;
            if (syntheticWall && wall && !(doorOpen && door))
                continue;
            distance[key] = distance[cell] + 1;
            queue.push_back(key);
        }
    }
    return result;
}
} // namespace avboit
