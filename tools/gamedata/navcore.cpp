// navcore: the navigation data of a region from its collision data.
//
// Input: the files of collision_data.py (REGION.swcol: the terrain height, the water, the solid
// shapes, and the navigation marks of the level designers).
// Output: one file for a region with the grid of the places where a creature can stand, with
// more than one floor at one place, and with what stops the sight (see `command_build`). The
// tiles of that file have no compression: nav_file.py makes the file of the game data folder.
//
// How a region becomes navigation data (one tile of 64 m at a time, with a border). The
// analysis has a cell of 0.125 m, and a cell of the file is 2 by 2 analysis cells (0.25 m):
//   1. Each triangle of each collider that stops a creature becomes spans: for each analysis
//      column that the triangle touches, the height interval of the triangle in that column.
//      A convex part (a box, a hull) is one span for each column: it is a solid. A closed
//      mesh is a solid between a surface with its back up and the next surface with its
//      front up.
//   2. The terrain is one span for each column, from far below to the terrain height.
//   3. The spans of a column that touch become one span. The top of a span is a floor when
//      its surface is not steeper than the slope limit and the free height above it is not
//      less than the height of the agent.
//   4. A floor is free when no span of a column that is nearer than the agent radius is in
//      the height interval of the body (from the step height to the agent height above the
//      floor).
//   5. A navigation mark of the level designers: the top of a solid with the mark "not
//      walkable" is no floor, and no floor is free in a volume with that mark.
//   6. A floor below the water surface gets a water mark (shallow or deep).
//   7. A cell of the file has the free floors of its analysis cells (floors that are 0.25 m or
//      less from each other in height are one floor). Thus an opening of 0.7 m stays open in
//      the file, and no floor of the file connects two sides of a wall.
//   8. The sight: for each floor of a cell, the height interval above it that is free in all
//      analysis cells of the cell. A cell with no free floor has its surfaces as floors with
//      the mark "blocked", so that the sight over them has its interval too.
//
// The program needs the C++17 standard library, and for the command `noise` the file
// vendor/open-simplex-noise/open-simplex-noise.c (public domain): the client has the same noise
// code, and the terrain rule needs equal numbers. Compile the two files with -ffp-contract=off:
// the terrain rule is a sequence of float operations that must not be joined.
//
// Commands:
//   navcore noise OUTPUT.f32                                    (the noise texture of the terrain rule, see `command_noise`)
//   navcore cookies NOISE.f32 ORIGIN_X ORIGIN_Y HEIGHTS.u16 COOKIES.tsv OUTPUT.u16
//                                                               (the terrain samples of a region after its cookies)
//   navcore build COLLISION_DIR REGION OUTPUT.raw [threads]
//   navcore window COLLISION_DIR X0 Y0 X1 Y1 OUTPUT.bin        (the spans and floors of a rectangle, analysis cells)
//   navcore surfaces COLLISION_DIR POINTS.tsv OUTPUT.tsv       (the surfaces at each point)
// Environment (the agent: project values, protocol notes 4.19):
//   NAV_CELL (0.25 m: the cell of the file), NAV_FINE (2: analysis cells for one cell of the file, on a side),
//   NAV_RADIUS (0.35 m), NAV_HEIGHT (1.8 m), NAV_STEP (0.35 m), NAV_SLOPE (50 degrees),
//   NAV_WADE (1.2 m: the deepest water that a creature walks in)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include "vendor/open-simplex-noise/open-simplex-noise.h"
}

namespace {

constexpr int kRegionSize = 2048;
constexpr float kHeightScale = 2048.0f / 65535.0f;
constexpr uint16_t kNoWater = 0xFFFF;
constexpr int kTileMeters = 64;
constexpr float kFar = 1.0e9f;

// Flags of a floor in the navigation file.
constexpr uint8_t kFloorTerrain = 1;       // the floor is the terrain surface
constexpr uint8_t kFloorShallowWater = 2;  // water above the floor, not deeper than the wade limit
constexpr uint8_t kFloorDeepWater = 4;     // water above the floor, deeper than the wade limit
constexpr uint8_t kFloorBlocked = 8;       // no place for a creature: the floor is in the file for the sight
// Marks of a placement in the collision data (nav_slices.py).
constexpr uint8_t kMarkNotWalkable = 1;    // a solid whose top is no floor
constexpr uint8_t kMarkCut = 2;            // no solid: a volume in which no floor is free

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

double env_number(const char* name, double fallback) {
    const char* text = std::getenv(name);
    if (text == nullptr || *text == 0) return fallback;
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || !std::isfinite(value) || value <= 0.0) throw Failure(std::string(name) + " is not a positive number");
    return value;
}

struct Agent {
    float cell = 0.25f;        // the cell of the file
    int fine = 2;              // analysis cells for one cell of the file, on a side
    float radius = 0.35f;
    float height = 1.8f;
    float step = 0.35f;
    float slopeDegrees = 50.0f;
    float wade = 1.2f;

    static Agent from_environment() {
        Agent agent;
        agent.cell = static_cast<float>(env_number("NAV_CELL", agent.cell));
        agent.fine = static_cast<int>(env_number("NAV_FINE", agent.fine));
        agent.radius = static_cast<float>(env_number("NAV_RADIUS", agent.radius));
        agent.height = static_cast<float>(env_number("NAV_HEIGHT", agent.height));
        agent.step = static_cast<float>(env_number("NAV_STEP", agent.step));
        agent.slopeDegrees = static_cast<float>(env_number("NAV_SLOPE", agent.slopeDegrees));
        agent.wade = static_cast<float>(env_number("NAV_WADE", agent.wade));
        const double perMeter = 1.0 / agent.cell;
        if (perMeter != std::floor(perMeter) || perMeter > 16) throw Failure("NAV_CELL must be 1 m divided by a whole number from 1 to 16");
        if (agent.fine < 1 || agent.fine > 4) throw Failure("NAV_FINE must be a whole number from 1 to 4");
        return agent;
    }
};

// ---- the terrain rule of the client: the cookies ---------------------------------------------
//
// A placement of a capitals file of a region can have a member `cookie`. The client changes the
// samples of the height map of the region with each cookie, one time, after it read the height
// map (protocol notes 4.19; client code at the image addresses 0x6A26A70, 0x6A23AC0, 0x6A197F0,
// 0x6A023B0, 0x69F72E0, 0x6A29B10): the terrain near the placement goes to the height of the
// placement. The height map of the game copy does not have this change. Each step below is a
// step of the client code with numbers of 32 bits: do not change the sequence of the operations.

constexpr int kNoiseSize = 1024;

struct Cookie {
    float x, y, z, inner, outer, blend, edge, feature, ground;
};

struct NoiseTexture {
    std::vector<float> data;

    // The read of the texture: between four values, with wrap (0x6A29B10).
    float texture(float u, float v) const {
        const float fx = static_cast<float>(kNoiseSize) * u, fy = static_cast<float>(kNoiseSize) * v;
        // A position that is not a number (a feature size of 0) gives a value that is not a number.
        if (!std::isfinite(fx) || !std::isfinite(fy)) return std::numeric_limits<float>::quiet_NaN();
        const float bx = std::floor(fx), by = std::floor(fy);
        const float tx = fx - bx, ty = fy - by;
        const long long ix = static_cast<long long>(bx), iy = static_cast<long long>(by);
        auto wrap = [](long long index) { return static_cast<size_t>(index < 0 ? 0 : index % kNoiseSize); };
        const size_t x0 = wrap(ix), x1 = wrap(ix + 1), y0 = wrap(iy), y1 = wrap(iy + 1);
        const float a = data[y0 * kNoiseSize + x0], b = data[y0 * kNoiseSize + x1];
        const float c = data[y1 * kNoiseSize + x0], e = data[y1 * kNoiseSize + x1];
        const float top = (b - a) * tx + a;
        const float bottom = (e - c) * tx + c;
        return (bottom - top) * ty + top;
    }

    float lookup(float x, float y) const {
        const float shift = -0.5f / static_cast<float>(kNoiseSize);
        return texture(x * 0.15f + shift, y * 0.15f + shift);
    }

    // 10 octaves, each one 2.07 times finer and half as strong (0x6A023B0).
    float fbm(float x, float y) const {
        float total = lookup(x, y), amplitude = 1.0f, weight = 1.0f;
        for (int octave = 0; octave < 9; ++octave) {
            x = x * 2.07f;
            y = y * 2.07f;
            amplitude = amplitude * 0.5f;
            total = total + lookup(x, y) * amplitude;
            weight = weight + amplitude;
        }
        return total / weight;
    }
};

float float_of_bits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// The target height and the weight of one cookie at a position of the world (0x6A197F0).
void cookie_values(const NoiseTexture& noise, const Cookie& cookie, float px, float py, float& target, float& weight) {
    const float blend = std::min(std::max(cookie.blend, 0.0f), 1.0f);
    const float width = cookie.outer - cookie.inner;
    const float reach = (width * cookie.edge) * 0.5f;
    const float first = noise.fbm(px * 0.015625f + 7.1f, py * 0.015625f + 19.7f);
    const float second = noise.fbm(px * 0.125f + (7.1f + 7.3f), py * 0.125f + (19.7f + 73.9f));
    const float a = static_cast<float>(std::pow(static_cast<double>(std::fabs(first)), 0.45));
    const float b = static_cast<float>(std::pow(static_cast<double>(std::fabs(second)), 0.1));
    target = ((b + (a - 0.5f)) - 0.5f) * cookie.ground + cookie.z;
    // The edge noise moves the position (0x69F72E0). The four constants have the bits of the client code.
    const float scale = 1.0f / cookie.feature;
    const float moveX = noise.lookup(px * scale + float_of_bits(0x42C99999), py * scale + float_of_bits(0x42BB3334)) * reach;
    const float moveY = noise.lookup(px * scale + float_of_bits(0x42D83333), py * scale + float_of_bits(0x43278000)) * reach;
    const float dx = (px + moveX) - cookie.x;
    const float dy = (py + moveY) - cookie.y;
    const float distance = std::sqrt(dy * dy + dx * dx);
    // A distance that is not a number gives 0 here (the client uses the instruction maxss): the weight is then the blend.
    float t = std::fmax(distance - (reach + cookie.inner), 0.0f) / std::max(width, 0.0001f);
    t = std::min(t, 1.0f);
    const float f = std::min(std::max(1.0f - t, 0.0f), 1.0f);
    weight = (3.0f - (f + f)) * f * f * blend;
}

// One cookie on the samples of a region (0x6A23AC0). @return The samples that the cookie changed.
size_t draw_cookie(const NoiseTexture& noise, const Cookie& cookie, std::vector<uint16_t>& samples, double originX, double originY) {
    const int count = kRegionSize;
    const float size = static_cast<float>(count);
    // The middle of the tile plus one half cell (0x6A26A70).
    const float centerX = static_cast<float>(originX + count * 0.5 + 0.5), centerY = static_cast<float>(originY + count * 0.5 + 0.5);
    const float lowX = centerX - size * 0.5f, lowY = centerY - size * 0.5f;
    const float largest = std::max(cookie.inner, cookie.outer);
    const float half = (largest - cookie.inner) * 0.5f + largest;
    auto span = [half](float middle, float low, long& first, long& length) {
        first = static_cast<long>((middle - half) - low);     // to zero, as the client
        const long last = static_cast<long>((middle + half) - low);
        length = std::labs(last - first);
    };
    long i0, wide, j0, high;
    span(cookie.x, lowX, i0, wide);
    span(cookie.y, lowY, j0, high);
    const long ia = std::max(i0, 0L), ib = std::min(i0 + wide, static_cast<long>(count));
    const long ja = std::max(j0, 0L), jb = std::min(j0 + high, static_cast<long>(count));
    const float step = 1.0f / static_cast<float>(count);
    const float maxHeight = static_cast<float>(kRegionSize);   // the height of the sample 65535
    size_t changed = 0;
    for (long j = ja; j < jb; ++j) {
        const float py = ((static_cast<float>(j) + 0.5f) * step) * size + lowY;
        for (long i = ia; i < ib; ++i) {
            const float px = ((static_cast<float>(i) + 0.5f) * step) * size + lowX;
            float target, weight;
            cookie_values(noise, cookie, px, py, target, weight);
            target = std::min(std::max(target, 0.0f), maxHeight);
            const long wanted = static_cast<long>((target * 65535.0f) / maxHeight + 0.5f) & 0xFFFF;
            uint16_t& sample = samples[static_cast<size_t>(j) * count + static_cast<size_t>(i)];
            const long old = sample;
            const float mixed = static_cast<float>(wanted - old) * weight + static_cast<float>(old);
            const float value = static_cast<float>(static_cast<long>(mixed) & 0xFFFF) + 0.5f;
            const uint16_t next = static_cast<uint16_t>(static_cast<long>(value));
            if (next != sample) ++changed;
            sample = next;
        }
    }
    return changed;
}

// The noise texture: 1024 x 1024 values of 4D noise on a torus, three octaves, seed 77374 (0x6A0E2E0, 0x6A03D70).
int command_noise(const std::string& output) {
    const int64_t seed = 0x12E3E;
    // The permutation of the seed, as the client makes it (0x6A208C0): the remainder of a signed number.
    int16_t source[256], permutation[256];
    for (int i = 0; i < 256; ++i) source[i] = static_cast<int16_t>(i);
    uint64_t state = static_cast<uint64_t>(seed);
    for (int i = 0; i < 3; ++i) state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    for (int i = 255; i >= 0; --i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        int r = static_cast<int>(static_cast<int64_t>(state + 31) % static_cast<int64_t>(i + 1));
        if (r < 0) r += i + 1;
        permutation[i] = source[r];
        source[r] = source[i];
    }
    osn_context* context = nullptr;
    if (open_simplex_noise(0, &context) != 0 || open_simplex_noise_init_perm(context, permutation, 256) != 0)
        throw Failure("the noise function has no memory");
    const double twoPi = 6.2831854820251465, k = 0.15915493866300567;
    const float frequency = 6.0f;
    std::vector<double> values(static_cast<size_t>(kNoiseSize) * kNoiseSize);
    double low = 0.0, high = 0.0;
    const double step = 1.0 / static_cast<double>(kNoiseSize);
    for (int row = 0; row < kNoiseSize; ++row) {
        const double ay = static_cast<double>(row) * step * twoPi;
        const double sy = std::sin(ay), cy = std::cos(ay);
        for (int column = 0; column < kNoiseSize; ++column) {
            const double ax = static_cast<double>(column) * step * twoPi;
            const double sx = std::sin(ax), cx = std::cos(ax);
            double f = static_cast<double>(frequency);
            double value = open_simplex_noise4(context, cx * f * k, cy * f * k, sx * f * k, sy * f * k);
            f = static_cast<double>(frequency + frequency);
            value = open_simplex_noise4(context, cx * f * k, cy * f * k, sx * f * k, sy * f * k) * 0.3 + value;
            f = static_cast<double>(frequency * 4.0f);
            value = value + open_simplex_noise4(context, cx * f * k, cy * f * k, sx * f * k, sy * f * k) * 0.1;
            values[static_cast<size_t>(row) * kNoiseSize + column] = value;
            high = std::max(high, value);
            low = std::min(low, value);
        }
    }
    open_simplex_noise_free(context);
    double scale = 1.0 / (high - low);
    scale = scale + scale;
    std::vector<float> out(values.size());
    for (size_t i = 0; i < values.size(); ++i) out[i] = static_cast<float>((values[i] - low) * scale - 1.0);
    std::ofstream file(output + ".part", std::ios::binary);
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size() * sizeof(float)));
    file.close();
    if (!file || std::rename((output + ".part").c_str(), output.c_str()) != 0) throw Failure("cannot write " + output);
    std::printf("{\"values\": %zu, \"low\": %.9f, \"high\": %.9f}\n", out.size(), low, high);
    return 0;
}

// COOKIES.tsv: one line for each cookie: x y z innerRadius outerRadius maxHeightBlend edgeNoise edgeNoiseFeatureSize
// groundNoiseScale (a cookie with a key that is not in the game file has the default of the client in the line).
int command_cookies(const std::string& noisePath, double originX, double originY, const std::string& input, const std::string& cookiesPath,
                    const std::string& output) {
    NoiseTexture noise;
    noise.data.resize(static_cast<size_t>(kNoiseSize) * kNoiseSize);
    {
        std::ifstream file(noisePath, std::ios::binary);
        if (!file || !file.read(reinterpret_cast<char*>(noise.data.data()), static_cast<std::streamsize>(noise.data.size() * sizeof(float))))
            throw Failure(noisePath + " is not a noise texture of this program");
    }
    std::vector<uint16_t> samples(static_cast<size_t>(kRegionSize) * kRegionSize);
    {
        std::ifstream file(input, std::ios::binary);
        if (!file || !file.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(samples.size() * 2)) || file.peek() != EOF)
            throw Failure(input + " does not have 2048 x 2048 samples of 16 bits");
    }
    std::vector<Cookie> cookies;
    {
        std::ifstream file(cookiesPath);
        if (!file) throw Failure("cannot read " + cookiesPath);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            double v[9];
            if (std::sscanf(line.c_str(), "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) != 9)
                throw Failure(cookiesPath + ": a line does not have nine numbers");
            Cookie cookie{static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]), static_cast<float>(v[3]),
                          static_cast<float>(v[4]), static_cast<float>(v[5]), static_cast<float>(v[6]), static_cast<float>(v[7]),
                          static_cast<float>(v[8])};
            // The client uses each placement whose cookie has a blend above 0 (0x6A26A70).
            if (cookie.blend > 0.0f) cookies.push_back(cookie);
        }
    }
    // The lowest cookie first: each cookie changes the samples that the cookies before it changed (0x69B0440).
    std::stable_sort(cookies.begin(), cookies.end(), [](const Cookie& a, const Cookie& b) { return a.z < b.z; });
    const float size = static_cast<float>(kRegionSize);
    const float centerX = static_cast<float>(originX + kRegionSize * 0.5 + 0.5), centerY = static_cast<float>(originY + kRegionSize * 0.5 + 0.5);
    size_t used = 0, changed = 0;
    for (const Cookie& cookie : cookies) {
        const float largest = std::max(cookie.inner, cookie.outer);
        if (!(std::fabs(cookie.x - centerX) - largest * 1.5f < size * 0.5f && std::fabs(cookie.y - centerY) - largest * 1.5f < size * 0.5f)) continue;
        ++used;
        changed += draw_cookie(noise, cookie, samples, originX, originY);
    }
    std::ofstream file(output + ".part", std::ios::binary);
    file.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(samples.size() * 2));
    file.close();
    if (!file || std::rename((output + ".part").c_str(), output.c_str()) != 0) throw Failure("cannot write " + output);
    std::printf("{\"cookies\": %zu, \"cookiesUsed\": %zu, \"sampleChanges\": %zu}\n", cookies.size(), used, changed);
    return 0;
}

// ---- the collision data of one region ---------------------------------------------------------

struct Part {
    uint32_t flags, firstVertex, vertexCount, firstIndex, indexCount;   // flags: 1 convex solid, 2 closed mesh, 0 open mesh
};

struct ShapeRange {
    uint32_t firstPart, partCount;
};

struct Placement {
    uint32_t shape;
    uint16_t filter;
    uint8_t source;
    uint8_t mark;
    float m[12];
};

struct Box {
    float lowX = kFar, lowY = kFar, lowZ = kFar, highX = -kFar, highY = -kFar, highZ = -kFar;
};

struct Collision {
    int regionX = 0, regionY = 0;
    float ocean = 0.0f;
    std::vector<uint16_t> heights, waterSurface, waterFloor;
    std::vector<uint8_t> filterFlags;
    std::vector<std::string> filterNames, shapeNames;
    std::vector<ShapeRange> shapes;
    std::vector<Part> parts;
    std::vector<float> vertices;
    std::vector<uint32_t> indexes;
    std::vector<Placement> placements;
    std::vector<Box> boxes;                          // world coordinates
    std::vector<std::vector<uint32_t>> buckets;      // placements of each square of 32 m
    static constexpr int kBucket = 32;
    static constexpr int kBuckets = kRegionSize / kBucket;

    double originX() const { return static_cast<double>(regionX) * kRegionSize; }
    double originY() const { return static_cast<double>(regionY) * kRegionSize; }

    template <typename T>
    static void take(const std::vector<char>& data, size_t offset, size_t size, std::vector<T>& out, const char* tag) {
        if (size % sizeof(T) != 0) throw Failure(std::string("section ") + tag + " has a wrong size");
        out.resize(size / sizeof(T));
        std::memcpy(out.data(), data.data() + offset, size);
    }

    static std::unique_ptr<Collision> load(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw Failure("no collision data file " + path);
        const size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> data(size);
        file.seekg(0);
        file.read(data.data(), static_cast<std::streamsize>(size));
        if (!file || size < 20 || std::memcmp(data.data(), "SWCL", 4) != 0) throw Failure(path + " is not a collision data file");
        uint32_t version, regionSize;
        auto out = std::make_unique<Collision>();
        std::memcpy(&version, data.data() + 4, 4);
        std::memcpy(&out->regionX, data.data() + 8, 4);
        std::memcpy(&out->regionY, data.data() + 12, 4);
        std::memcpy(&regionSize, data.data() + 16, 4);
        if (version != 2 || regionSize != kRegionSize)
            throw Failure(path + " has a version or a region size that this program does not read (make the collision data again)");
        size_t position = 20;
        std::vector<Placement> placements;
        while (position + 12 <= size) {
            char tag[5] = {0, 0, 0, 0, 0};
            uint64_t length;
            std::memcpy(tag, data.data() + position, 4);
            std::memcpy(&length, data.data() + position + 4, 8);
            position += 12;
            if (length > size - position) throw Failure(path + ": section " + tag + " is longer than the file");
            const std::string name(tag);
            if (name == "OCEN") {
                std::memcpy(&out->ocean, data.data() + position, 4);
            } else if (name == "HGHT") {
                take(data, position, length, out->heights, tag);
            } else if (name == "WSRF") {
                take(data, position, length, out->waterSurface, tag);
            } else if (name == "WFLR") {
                take(data, position, length, out->waterFloor, tag);
            } else if (name == "FILT" || name == "SNAM") {
                uint32_t count;
                std::memcpy(&count, data.data() + position, 4);
                size_t at = position + 4;
                for (uint32_t i = 0; i < count; ++i) {
                    if (name == "FILT") {
                        const uint8_t flags = static_cast<uint8_t>(data[at]);
                        const uint8_t textSize = static_cast<uint8_t>(data[at + 1]);
                        out->filterFlags.push_back(flags);
                        out->filterNames.emplace_back(data.data() + at + 2, textSize);
                        at += 2 + textSize;
                    } else {
                        uint16_t textSize;
                        std::memcpy(&textSize, data.data() + at, 2);
                        out->shapeNames.emplace_back(data.data() + at + 2, textSize);
                        at += 2 + textSize;
                    }
                }
            } else if (name == "SHAP") {
                take(data, position + 4, length - 4, out->shapes, tag);
            } else if (name == "PART") {
                take(data, position + 4, length - 4, out->parts, tag);
            } else if (name == "VERT") {
                take(data, position, length, out->vertices, tag);
            } else if (name == "INDX") {
                take(data, position, length, out->indexes, tag);
            } else if (name == "PLAC") {
                take(data, position + 4, length - 4, out->placements, tag);
            }
            position += length;
        }
        const size_t samples = static_cast<size_t>(kRegionSize) * kRegionSize;
        if (out->heights.size() != samples || out->waterSurface.size() != samples || out->waterFloor.size() != samples)
            throw Failure(path + " has no complete terrain or water section");
        out->index();
        return out;
    }

    void transform(const Placement& placement, const float* v, float* w) const {
        const float* m = placement.m;
        w[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3] + static_cast<float>(originX());
        w[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7] + static_cast<float>(originY());
        w[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
    }

    void index() {
        boxes.resize(placements.size());
        buckets.assign(static_cast<size_t>(kBuckets) * kBuckets, {});
        for (size_t p = 0; p < placements.size(); ++p) {
            const Placement& placement = placements[p];
            if (placement.shape >= shapes.size()) throw Failure("a placement names a shape that is not in the file");
            Box& box = boxes[p];
            const uint32_t firstPart = shapes[placement.shape].firstPart, partCount = shapes[placement.shape].partCount;
            for (uint32_t part = firstPart; part < firstPart + partCount; ++part) {
                const Part& info = parts[part];
                for (uint32_t i = 0; i < info.vertexCount; ++i) {
                    float w[3];
                    transform(placement, &vertices[(info.firstVertex + i) * 3], w);
                    box.lowX = std::min(box.lowX, w[0]); box.highX = std::max(box.highX, w[0]);
                    box.lowY = std::min(box.lowY, w[1]); box.highY = std::max(box.highY, w[1]);
                    box.lowZ = std::min(box.lowZ, w[2]); box.highZ = std::max(box.highZ, w[2]);
                }
            }
            if (box.lowX > box.highX) continue;
            const int x0 = std::clamp(static_cast<int>(std::floor((box.lowX - originX()) / kBucket)), 0, kBuckets - 1);
            const int x1 = std::clamp(static_cast<int>(std::floor((box.highX - originX()) / kBucket)), 0, kBuckets - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor((box.lowY - originY()) / kBucket)), 0, kBuckets - 1);
            const int y1 = std::clamp(static_cast<int>(std::floor((box.highY - originY()) / kBucket)), 0, kBuckets - 1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) buckets[static_cast<size_t>(y) * kBuckets + x].push_back(static_cast<uint32_t>(p));
        }
    }
};

// The regions near one place: the terrain and the colliders of each region that is loaded.
struct Scene {
    std::string folder;
    std::map<std::pair<int, int>, std::unique_ptr<Collision>> regions;
    std::mutex mutex;

    static std::string region_name(int x, int y) {
        char text[32];
        std::snprintf(text, sizeof text, "r_%+03d_%+03d", x, y);
        return text;
    }

    // @return The region, or nothing when the world has no such region (or it has no file yet).
    Collision* region(int x, int y) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto key = std::make_pair(x, y);
        auto found = regions.find(key);
        if (found != regions.end()) return found->second.get();
        std::unique_ptr<Collision> loaded;
        const std::string path = folder + "/" + region_name(x, y) + ".swcol";
        if (std::ifstream(path).good()) loaded = Collision::load(path);
        return regions.emplace(key, std::move(loaded)).first->second.get();
    }

    Collision* region_at(double x, double y) {
        return region(static_cast<int>(std::floor(x / kRegionSize)), static_cast<int>(std::floor(y / kRegionSize)));
    }

    // The terrain sample at whole meters of the world. A place with no region has the height of its nearest sample.
    bool sample(long x, long y, float& height, Collision*& cache) {
        const int rx = static_cast<int>(std::floor(static_cast<double>(x) / kRegionSize));
        const int ry = static_cast<int>(std::floor(static_cast<double>(y) / kRegionSize));
        if (cache == nullptr || cache->regionX != rx || cache->regionY != ry) cache = region(rx, ry);
        if (cache == nullptr) return false;
        height = cache->heights[static_cast<size_t>(y - static_cast<long>(ry) * kRegionSize) * kRegionSize +
                                static_cast<size_t>(x - static_cast<long>(rx) * kRegionSize)] * kHeightScale;
        return true;
    }

    // @return True when the place has terrain. The height is between the four samples around the place.
    bool terrain(double x, double y, float& height, float* slope = nullptr) {
        const long ix = static_cast<long>(std::floor(x)), iy = static_cast<long>(std::floor(y));
        const float fx = static_cast<float>(x - ix), fy = static_cast<float>(y - iy);
        Collision* cache = nullptr;
        float a, b, c, d;
        if (!sample(ix, iy, a, cache)) return false;
        // A neighbor that the world does not have repeats the sample of the edge, as in
        // height_maps.py: with no region to the east, the diagonal sample is the sample to the
        // north; with an east sample and no diagonal one, it is the east sample (code review of
        // stage 3.4d, finding F11: the mean of the two gave a different height and slope).
        const bool noEast = !sample(ix + 1, iy, b, cache);
        if (noEast) b = a;
        if (!sample(ix, iy + 1, c, cache)) c = a;
        if (!sample(ix + 1, iy + 1, d, cache)) d = noEast ? c : b;
        height = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        if (slope != nullptr) {
            const float gx = (b - a) * (1 - fy) + (d - c) * fy;
            const float gy = (c - a) * (1 - fx) + (d - b) * fx;
            *slope = std::atan(std::sqrt(gx * gx + gy * gy)) * 57.29578f;
        }
        return true;
    }

    // @return The water surface at a place, or a value far below when the place has no water.
    float water(double x, double y, float terrainHeight) {
        Collision* region = region_at(x, y);
        if (region == nullptr) return -kFar;
        const int lx = std::clamp(static_cast<int>(std::floor(x - region->originX())), 0, kRegionSize - 1);
        const int ly = std::clamp(static_cast<int>(std::floor(y - region->originY())), 0, kRegionSize - 1);
        const uint16_t value = region->waterSurface[static_cast<size_t>(ly) * kRegionSize + lx];
        float surface = value == kNoWater ? -kFar : value / 32.0f;
        if (terrainHeight < region->ocean) surface = std::max(surface, region->ocean);
        return surface;
    }

    // Calls `visit` for each placement whose box touches the rectangle (one time for each placement).
    template <typename Visit>
    void placements(double x0, double y0, double x1, double y1, Visit visit) {
        const int rx0 = static_cast<int>(std::floor((x0 - 128.0) / kRegionSize)), rx1 = static_cast<int>(std::floor((x1 + 128.0) / kRegionSize));
        const int ry0 = static_cast<int>(std::floor((y0 - 128.0) / kRegionSize)), ry1 = static_cast<int>(std::floor((y1 + 128.0) / kRegionSize));
        for (int ry = ry0; ry <= ry1; ++ry) {
            for (int rx = rx0; rx <= rx1; ++rx) {
                Collision* region = this->region(rx, ry);
                if (region == nullptr) continue;
                const int bx0 = std::clamp(static_cast<int>(std::floor((x0 - region->originX()) / Collision::kBucket)), 0, Collision::kBuckets - 1);
                const int bx1 = std::clamp(static_cast<int>(std::floor((x1 - region->originX()) / Collision::kBucket)), 0, Collision::kBuckets - 1);
                const int by0 = std::clamp(static_cast<int>(std::floor((y0 - region->originY()) / Collision::kBucket)), 0, Collision::kBuckets - 1);
                const int by1 = std::clamp(static_cast<int>(std::floor((y1 - region->originY()) / Collision::kBucket)), 0, Collision::kBuckets - 1);
                std::vector<uint32_t> seen;
                for (int by = by0; by <= by1; ++by) {
                    for (int bx = bx0; bx <= bx1; ++bx) {
                        for (uint32_t p : region->buckets[static_cast<size_t>(by) * Collision::kBuckets + bx]) {
                            const Box& box = region->boxes[p];
                            if (box.highX < x0 || box.lowX > x1 || box.highY < y0 || box.lowY > y1) continue;
                            // A placement is in more than one bucket: take it in the first bucket of the rectangle.
                            const int fx = std::max(bx0, std::clamp(static_cast<int>(std::floor((box.lowX - region->originX()) / Collision::kBucket)), 0, Collision::kBuckets - 1));
                            const int fy = std::max(by0, std::clamp(static_cast<int>(std::floor((box.lowY - region->originY()) / Collision::kBucket)), 0, Collision::kBuckets - 1));
                            if (fx != bx || fy != by) continue;
                            visit(*region, p);
                        }
                    }
                }
            }
        }
    }
};

// ---- spans of a rectangle of the world -------------------------------------------------------

struct Span {
    float low, high;
    uint8_t walkable;   // the top surface is not steeper than the slope limit
    uint8_t terrain;    // the top surface is the terrain
    uint8_t marked;     // the top surface has the mark "not walkable" of the level designers
};

struct Floor {
    float height;
    float ceiling;
    uint8_t state;      // 0 free; 1 too near to a solid; 2 a navigation mark; 3 the surface is too steep or the room is too low
    uint8_t flags;      // kFloor...
};

enum : uint8_t { kFree = 0, kNearSolid = 1, kMarked = 2, kNoRoom = 3 };

// A rectangle of grid columns with their spans and floors.
struct Grid {
    Agent agent;
    float cell = 0.125f;           // the analysis cell
    long cellX0 = 0, cellY0 = 0;   // the first column, in analysis cells of the world
    int width = 0, height = 0;
    std::vector<uint32_t> first;   // spans of column i: spans[first[i]] ... spans[first[i + 1] - 1]
    std::vector<Span> spans;
    std::vector<uint32_t> firstFloor;
    std::vector<Floor> floors;
    size_t triangles = 0, placements = 0;

    struct Raw {
        uint32_t column;
        Span span;
    };

    struct Piece {
        uint32_t column;
        float low, high;
        int8_t facing;
        uint8_t walkable;
    };

    static int clip(const float* in, int count, float* out, int axis, float limit, bool keepAbove) {
        int size = 0;
        for (int i = 0, j = count - 1; i < count; j = i++) {
            const float a = in[j * 3 + axis], b = in[i * 3 + axis];
            const bool insideA = keepAbove ? a >= limit : a <= limit;
            const bool insideB = keepAbove ? b >= limit : b <= limit;
            if (insideA != insideB) {
                const float t = (limit - a) / (b - a);
                for (int k = 0; k < 3; ++k) out[size * 3 + k] = in[j * 3 + k] + (in[i * 3 + k] - in[j * 3 + k]) * t;
                out[size * 3 + axis] = limit;
                ++size;
            }
            if (insideB) {
                for (int k = 0; k < 3; ++k) out[size * 3 + k] = in[i * 3 + k];
                ++size;
            }
        }
        return size;
    }

    // Calls `emit(column, low, high)` for each column that a triangle touches (grid coordinates in cells).
    template <typename Emit>
    void raster(const float* a, const float* b, const float* c, Emit emit) const {
        const float lowX = std::min({a[0], b[0], c[0]}), highX = std::max({a[0], b[0], c[0]});
        const float lowY = std::min({a[1], b[1], c[1]}), highY = std::max({a[1], b[1], c[1]});
        if (highX < 0 || lowX > width || highY < 0 || lowY > height) return;
        const int y0 = std::max(0, static_cast<int>(std::floor(lowY))), y1 = std::min(height - 1, static_cast<int>(std::floor(highY)));
        float polygon[7 * 3], row[7 * 3], rest[7 * 3], cell[7 * 3], scratch[7 * 3];
        std::memcpy(polygon, a, 12); std::memcpy(polygon + 3, b, 12); std::memcpy(polygon + 6, c, 12);
        for (int y = y0; y <= y1; ++y) {
            int count = clip(polygon, 3, scratch, 1, static_cast<float>(y), true);
            count = clip(scratch, count, row, 1, static_cast<float>(y + 1), false);
            if (count < 3) continue;
            float rowLow = row[0], rowHigh = row[0];
            for (int i = 1; i < count; ++i) { rowLow = std::min(rowLow, row[i * 3]); rowHigh = std::max(rowHigh, row[i * 3]); }
            const int x0 = std::max(0, static_cast<int>(std::floor(rowLow))), x1 = std::min(width - 1, static_cast<int>(std::floor(rowHigh)));
            for (int x = x0; x <= x1; ++x) {
                int size = clip(row, count, rest, 0, static_cast<float>(x), true);
                size = clip(rest, size, cell, 0, static_cast<float>(x + 1), false);
                if (size < 3) continue;
                float low = cell[2], high = cell[2];
                for (int i = 1; i < size; ++i) { low = std::min(low, cell[i * 3 + 2]); high = std::max(high, cell[i * 3 + 2]); }
                emit(static_cast<uint32_t>(y) * static_cast<uint32_t>(width) + static_cast<uint32_t>(x), low, high);
            }
        }
    }

    void build(Scene& scene, long x0, long y0, int w, int h) {
        cellX0 = x0; cellY0 = y0; width = w; height = h;
        const float cell = this->cell;
        const double worldX0 = x0 * static_cast<double>(cell), worldY0 = y0 * static_cast<double>(cell);
        const double worldX1 = worldX0 + w * static_cast<double>(cell), worldY1 = worldY0 + h * static_cast<double>(cell);
        const float slopeLimit = std::cos(agent.slopeDegrees / 57.29578f);
        std::vector<Raw> raw, cuts;
        raw.reserve(static_cast<size_t>(w) * h * 2);
        // The terrain.
        std::vector<uint8_t> hasTerrain(static_cast<size_t>(w) * h, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float ground, slope;
                if (!scene.terrain(worldX0 + (x + 0.5) * cell, worldY0 + (y + 0.5) * cell, ground, &slope)) continue;
                hasTerrain[static_cast<size_t>(y) * w + x] = 1;
                raw.push_back({static_cast<uint32_t>(y) * w + x, {-kFar, ground, static_cast<uint8_t>(slope <= agent.slopeDegrees), 1, 0}});
            }
        }
        // The colliders.
        std::vector<float> columnLow(static_cast<size_t>(w) * h), columnHigh(static_cast<size_t>(w) * h);
        std::vector<Piece> pieces;
        std::vector<uint32_t> stamp(static_cast<size_t>(w) * h, 0), touched;
        std::vector<uint8_t> columnWalk(static_cast<size_t>(w) * h);
        uint32_t stampNow = 0;
        std::vector<float> world;
        scene.placements(worldX0, worldY0, worldX1, worldY1, [&](Collision& region, uint32_t p) {
            const Placement& placement = region.placements[p];
            const bool cut = placement.mark == kMarkCut;
            if (!cut && (region.filterFlags[placement.filter] & 1) == 0) return;
            ++placements;
            const size_t rawStart = raw.size();
            const float* m = placement.m;
            const bool mirror = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]) < 0.0f;
            const uint32_t firstPart = region.shapes[placement.shape].firstPart, partCount = region.shapes[placement.shape].partCount;
            for (uint32_t part = firstPart; part < firstPart + partCount; ++part) {
                const Part& info = region.parts[part];
                world.resize(static_cast<size_t>(info.vertexCount) * 3);
                for (uint32_t i = 0; i < info.vertexCount; ++i) {
                    float v[3];
                    region.transform(placement, &region.vertices[(info.firstVertex + i) * 3], v);
                    world[i * 3] = static_cast<float>((v[0] - worldX0) / cell);
                    world[i * 3 + 1] = static_cast<float>((v[1] - worldY0) / cell);
                    world[i * 3 + 2] = v[2];
                }
                const bool convex = info.flags == 1, closed = info.flags == 2;
                if (convex) { ++stampNow; touched.clear(); }
                pieces.clear();
                for (uint32_t i = 0; i + 2 < info.indexCount; i += 3) {
                    const float* a = &world[region.indexes[info.firstIndex + i] * 3];
                    const float* b = &world[region.indexes[info.firstIndex + i + 1] * 3];
                    const float* c = &world[region.indexes[info.firstIndex + i + 2] * 3];
                    // The normal in meters: x and y are in cells.
                    const float ux = (b[0] - a[0]) * cell, uy = (b[1] - a[1]) * cell, uz = b[2] - a[2];
                    const float vx = (c[0] - a[0]) * cell, vy = (c[1] - a[1]) * cell, vz = c[2] - a[2];
                    const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                    const float size = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (size < 1e-12f) continue;
                    const uint8_t walkable = std::fabs(nz) / size >= slopeLimit;
                    // The side of the triangle that is up: 1 its front (the top of a solid), -1 its back.
                    const float up = (mirror ? -nz : nz) / size;
                    const int8_t facing = up > 0.05f ? 1 : (up < -0.05f ? -1 : 0);
                    ++triangles;
                    raster(a, b, c, [&](uint32_t column, float low, float high) {
                        if (closed) {
                            pieces.push_back({column, low, high, facing, walkable});
                            return;
                        }
                        if (!convex) {
                            raw.push_back({column, {low, high, walkable, 0, 0}});
                            return;
                        }
                        if (stamp[column] != stampNow) {
                            stamp[column] = stampNow;
                            touched.push_back(column);
                            columnLow[column] = low; columnHigh[column] = high; columnWalk[column] = walkable;
                            return;
                        }
                        columnLow[column] = std::min(columnLow[column], low);
                        if (high > columnHigh[column] + 0.02f) { columnHigh[column] = high; columnWalk[column] = walkable; }
                        else if (high > columnHigh[column] - 0.02f) { columnHigh[column] = std::max(columnHigh[column], high); columnWalk[column] |= walkable; }
                    });
                }
                if (convex)
                    for (uint32_t column : touched) raw.push_back({column, {columnLow[column], columnHigh[column], columnWalk[column], 0, 0}});
                if (closed) {
                    // A closed mesh is a solid between a surface with its back up (the bottom of the solid) and
                    // the next surface above it with its front up (the top of the solid).
                    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
                        return a.column != b.column ? a.column < b.column : a.low + a.high < b.low + b.high;
                    });
                    bool open = false;
                    float openLow = 0.0f;
                    for (size_t i = 0; i < pieces.size(); ++i) {
                        const Piece& piece = pieces[i];
                        if (i > 0 && pieces[i - 1].column != piece.column) open = false;
                        if (piece.facing < 0) {
                            if (!open) { open = true; openLow = piece.low; }
                            raw.push_back({piece.column, {piece.low, piece.high, 0, 0, 0}});
                        } else if (piece.facing > 0) {
                            raw.push_back({piece.column, {open ? openLow : piece.low, piece.high, piece.walkable, 0, 0}});
                            // A sheet with no thickness has its two sides at one height, in any sequence: the
                            // back of the sheet does not start a solid.
                            if (!open && i + 1 < pieces.size() && pieces[i + 1].column == piece.column && pieces[i + 1].facing < 0 &&
                                pieces[i + 1].low + pieces[i + 1].high - piece.low - piece.high <= 0.04f) {
                                raw.push_back({piece.column, {pieces[i + 1].low, pieces[i + 1].high, 0, 0, 0}});
                                ++i;
                            }
                            open = false;
                        } else {
                            raw.push_back({piece.column, {piece.low, piece.high, 0, 0, 0}});
                        }
                    }
                }
            }
            if (cut) {
                // A volume of a navigation mark is no solid: its spans do not go into the columns.
                cuts.insert(cuts.end(), raw.begin() + static_cast<std::ptrdiff_t>(rawStart), raw.end());
                raw.resize(rawStart);
            } else if (placement.mark == kMarkNotWalkable) {
                for (size_t i = rawStart; i < raw.size(); ++i) raw[i].span.marked = 1;
            }
        });
        // Sort the spans by column and height, and join the spans that touch.
        std::sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) {
            return a.column != b.column ? a.column < b.column : a.span.low < b.span.low;
        });
        first.assign(static_cast<size_t>(w) * h + 1, 0);
        spans.clear();
        size_t at = 0;
        for (uint32_t column = 0; column < static_cast<uint32_t>(w) * h; ++column) {
            first[column] = static_cast<uint32_t>(spans.size());
            const size_t begin = spans.size();
            for (; at < raw.size() && raw[at].column == column; ++at) {
                const Span& next = raw[at].span;
                if (spans.size() > begin && next.low <= spans.back().high + 0.05f) {
                    Span& top = spans.back();
                    if (next.high > top.high + 0.1f) {
                        top.high = next.high; top.walkable = next.walkable; top.terrain = next.terrain; top.marked = next.marked;
                    } else if (next.high >= top.high - 0.1f) {
                        if (next.high > top.high) { top.high = next.high; top.terrain = next.terrain; }
                        top.walkable |= next.walkable;
                        top.marked |= next.marked;
                    }
                } else {
                    spans.push_back(next);
                }
            }
        }
        first[static_cast<size_t>(w) * h] = static_cast<uint32_t>(spans.size());
        // The floors: the top of each span with room above it.
        firstFloor.assign(static_cast<size_t>(w) * h + 1, 0);
        floors.clear();
        for (uint32_t column = 0; column < static_cast<uint32_t>(w) * h; ++column) {
            firstFloor[column] = static_cast<uint32_t>(floors.size());
            if (!hasTerrain[column]) continue;
            for (uint32_t s = first[column]; s < first[column + 1]; ++s) {
                const float ceiling = s + 1 < first[column + 1] ? spans[s + 1].low : kFar;
                const bool room = ceiling - spans[s].high >= agent.height;
                if (!spans[s].walkable && !room) continue;
                const uint8_t state = !(spans[s].walkable && room) ? kNoRoom : (spans[s].marked ? kMarked : kFree);
                Floor floor{spans[s].high, ceiling, state, static_cast<uint8_t>(spans[s].terrain ? kFloorTerrain : 0)};
                floors.push_back(floor);
            }
        }
        firstFloor[static_cast<size_t>(w) * h] = static_cast<uint32_t>(floors.size());
        // The room for the body: no span of a near column in the height interval of the body.
        const int reach = static_cast<int>(std::ceil(agent.radius / cell));
        std::vector<std::pair<int, int>> offsets;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
                if (dx == 0 && dy == 0) continue;
                // The nearest point of the other cell to the center of this cell.
                const float nx = (std::abs(dx) - 0.5f) * cell, ny = (std::abs(dy) - 0.5f) * cell;
                const float mx = std::max(nx, 0.0f), my = std::max(ny, 0.0f);
                if (mx * mx + my * my < agent.radius * agent.radius) offsets.emplace_back(dx, dy);
            }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t column = static_cast<uint32_t>(y) * w + x;
                for (uint32_t f = firstFloor[column]; f < firstFloor[column + 1]; ++f) {
                    Floor& floor = floors[f];
                    if (floor.state != kFree) continue;
                    const float low = floor.height + agent.step, high = floor.height + agent.height;
                    for (const auto& [dx, dy] : offsets) {
                        const int ox = x + dx, oy = y + dy;
                        if (ox < 0 || oy < 0 || ox >= w || oy >= h) continue;
                        const uint32_t other = static_cast<uint32_t>(oy) * w + ox;
                        for (uint32_t s = first[other]; s < first[other + 1]; ++s) {
                            if (spans[s].low >= high) break;
                            if (spans[s].high > low) { floor.state = kNearSolid; break; }
                        }
                        if (floor.state != kFree) break;
                    }
                }
            }
        }
        // The volumes of the navigation marks: no floor in a volume is free.
        for (const Raw& mark : cuts) {
            for (uint32_t f = firstFloor[mark.column]; f < firstFloor[mark.column + 1]; ++f) {
                Floor& floor = floors[f];
                if (floor.state == kFree && floor.height >= mark.span.low - 0.05f && floor.height <= mark.span.high + 0.05f) floor.state = kMarked;
            }
        }
        // The water.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint32_t column = static_cast<uint32_t>(y) * w + x;
                if (firstFloor[column] == firstFloor[column + 1]) continue;
                const double wx = worldX0 + (x + 0.5) * cell, wy = worldY0 + (y + 0.5) * cell;
                float ground;
                if (!scene.terrain(wx, wy, ground)) continue;
                const float surface = scene.water(wx, wy, ground);
                if (surface <= ground) continue;
                for (uint32_t f = firstFloor[column]; f < firstFloor[column + 1]; ++f) {
                    Floor& floor = floors[f];
                    // Only a floor that the water volume has: from the ground up to the surface.
                    if (floor.height >= surface || floor.height < ground - 0.5f) continue;
                    floor.flags |= surface - floor.height > agent.wade ? kFloorDeepWater : kFloorShallowWater;
                }
            }
        }
    }
};

// ---- the navigation file ---------------------------------------------------------------------

// The file that this program writes (all numbers little-endian). nav_pack.py reads it and writes the file of the
// game data folder, with the same tiles as zlib streams.
// file    := "SWNR" version:u32 regionX:i32 regionY:i32 cell:f32 radius:f32 height:f32 step:f32
//            slope:f32 wade:f32 tileCells:u32 tilesPerSide:u32 ocean:f32
//            directory: (offset:u64 size:u32 floors:u32) * tilesPerSide * tilesPerSide
//            tile*
// tile    := terrain: u16 * 65 * 65     (the terrain samples at whole meters of the tile, the rows from the south to
//                                         the north: the last row and column are the first ones of the next tiles;
//                                         height = sample * 2048 / 65535)
//            runCount:u32 run* extraCount:u32 extra*
// run     := code:u8 length:u16      the same code for `length` cells, row by row (x first)
//            code 0: no floor.
//            code 1: one free floor, the terrain surface, no water, the sky above it.
//            code 2: the same below shallow water.
//            code 3: the same below deep water.
//            code 4: the terrain surface, not a place for a creature (too steep), the sky above it.
//            code 4 + n: n floors follow in `extra` for each cell of the run (n from 1).
// extra   := height:u16 (1/32 m) flags:u8 sightLow:u8 sightHigh:u8      the floors of a cell from the lowest one up
//            flags: 1 the terrain surface, 2 shallow water, 4 deep water, 8 blocked (no place for a creature)
//            sightLow (1/8 m) and sightHigh (1/4 m): the sight is free from height + sightLow to height + sightHigh.
//            sightLow 255: no free interval above the floor. sightHigh 255: the sky.
// The height of a floor with the terrain flag is the terrain height at the center of its cell (between the four
// samples around it). A floor with deep water is in the file with its flag.
constexpr uint32_t kNavVersion = 3;
constexpr int kTileSamples = kTileMeters + 1;
constexpr uint8_t kNoSight = 255, kOpenSky = 255;
constexpr int kCodeFloors = 4;   // the code of a run with n floors is kCodeFloors + n
constexpr size_t kMaxCellFloors = 16;   // the floors of one cell that the reader of the backend keeps (NavTile::kMaxFloors)
// Free floors of the analysis cells of one cell that are this near in height are one floor (meters).
constexpr float kFloorJoin = 0.25f;
// The sight interval of a floor starts at the floor when the solid of the cell ends less than this above it (meters).
constexpr float kLowObject = 0.5f;

struct TileData {
    std::vector<uint8_t> bytes;
    uint32_t floors = 0;   // free floors
    uint32_t columnsWithFloor = 0, columnsWithFloors = 0, shallow = 0, deep = 0, onCollider = 0, blocked = 0, extras = 0;
};

// @param factor The analysis cells of the grid for one cell of the file, on a side.
// @param tileX0, tileY0 The first whole meter of the tile in the world.
TileData encode_tile(Scene& scene, const Grid& grid, int border, int tileCells, int factor, long tileX0, long tileY0) {
    TileData out;
    out.bytes.resize(static_cast<size_t>(kTileSamples) * kTileSamples * 2);
    {
        Collision* cache = nullptr;
        for (int y = 0; y < kTileSamples; ++y) {
            for (int x = 0; x < kTileSamples; ++x) {
                const long wx = tileX0 + x, wy = tileY0 + y;
                float height = 0.0f;
                // At the edge of the world: the nearest sample of the tile.
                if (!scene.sample(wx, wy, height, cache)) { cache = nullptr;
                    if (!scene.sample(wx, wy - 1, height, cache)) { cache = nullptr;
                        if (!scene.sample(wx - 1, wy, height, cache)) { cache = nullptr;
                            if (!scene.sample(wx - 1, wy - 1, height, cache)) { cache = nullptr; height = 0.0f; } } } }
                const uint16_t sample = static_cast<uint16_t>(std::clamp(std::lround(height / kHeightScale), 0L, 65535L));
                std::memcpy(out.bytes.data() + (static_cast<size_t>(y) * kTileSamples + x) * 2, &sample, 2);
            }
        }
    }
    std::vector<uint8_t> runs, extra;
    uint32_t runCount = 0, extraCount = 0;
    int runCode = -1;
    uint32_t runLength = 0;
    auto flush = [&]() {
        if (runLength == 0) return;
        runs.push_back(static_cast<uint8_t>(runCode));
        runs.push_back(static_cast<uint8_t>(runLength & 0xFF));
        runs.push_back(static_cast<uint8_t>(runLength >> 8));
        ++runCount;
        runLength = 0;
    };
    struct FineFloor { float height; uint8_t flags; };
    struct Entry { float height; uint8_t flags; float airLow, airHigh; bool sight; };
    std::vector<std::pair<float, float>> solid, air;
    std::vector<uint8_t> claimed;
    std::vector<FineFloor> fine;
    std::vector<Entry> entries;
    for (int y = 0; y < tileCells; ++y) {
        for (int x = 0; x < tileCells; ++x) {
            solid.clear(); air.clear(); fine.clear(); entries.clear();
            bool pureTerrain = true;
            for (int fy = 0; fy < factor; ++fy) {
                for (int fx = 0; fx < factor; ++fx) {
                    const uint32_t column = static_cast<uint32_t>(y * factor + fy + border) * grid.width + static_cast<uint32_t>(x * factor + fx + border);
                    const uint32_t s0 = grid.first[column], s1 = grid.first[column + 1];
                    if (s1 - s0 != 1 || !grid.spans[s0].terrain) pureTerrain = false;
                    for (uint32_t s = s0; s < s1; ++s) solid.emplace_back(grid.spans[s].low, grid.spans[s].high);
                    for (uint32_t f = grid.firstFloor[column]; f < grid.firstFloor[column + 1]; ++f)
                        if (grid.floors[f].state == kFree) fine.push_back({grid.floors[f].height, grid.floors[f].flags});
                }
            }
            int code = 0;
            if (!solid.empty()) {
                // The solid of the cell is the union of the solids of its analysis cells. The sight is free where
                // no analysis cell has a solid.
                std::sort(solid.begin(), solid.end());
                size_t last = 0;
                for (size_t i = 1; i < solid.size(); ++i) {
                    if (solid[i].first <= solid[last].second + 0.02f) solid[last].second = std::max(solid[last].second, solid[i].second);
                    else solid[++last] = solid[i];
                }
                solid.resize(last + 1);
                for (size_t i = 0; i < solid.size(); ++i) {
                    const float high = i + 1 < solid.size() ? solid[i + 1].first : kFar;
                    if (high - solid[i].second >= 0.25f) air.emplace_back(solid[i].second, high);
                }
                claimed.assign(air.size(), 0);
                // The free floors of the analysis cells: floors with nearly one height are one floor.
                std::sort(fine.begin(), fine.end(), [](const FineFloor& a, const FineFloor& b) { return a.height < b.height; });
                for (size_t i = 0; i < fine.size();) {
                    size_t j = i;
                    float sum = 0.0f;
                    bool allTerrain = true, allDeep = true, anyWater = false;
                    while (j < fine.size() && fine[j].height - fine[i].height <= kFloorJoin) {
                        sum += fine[j].height;
                        if ((fine[j].flags & kFloorTerrain) == 0) allTerrain = false;
                        if ((fine[j].flags & kFloorDeepWater) == 0) allDeep = false;
                        if (fine[j].flags & (kFloorDeepWater | kFloorShallowWater)) anyWater = true;
                        ++j;
                    }
                    Entry entry{sum / static_cast<float>(j - i), 0, 0.0f, 0.0f, false};
                    if (allTerrain) entry.flags |= kFloorTerrain;
                    if (allDeep) entry.flags |= kFloorDeepWater;
                    else if (anyWater) entry.flags |= kFloorShallowWater;
                    for (size_t a = 0; a < air.size(); ++a) {
                        if (air[a].second > entry.height + 0.2f) {
                            entry.airLow = std::max(air[a].first, entry.height); entry.airHigh = air[a].second; entry.sight = true;
                            claimed[a] = 1;
                            break;
                        }
                    }
                    entries.push_back(entry);
                    i = j;
                }
                // A free interval of the sight with no free floor below it: its surface is a blocked floor.
                for (size_t a = 0; a < air.size(); ++a)
                    if (!claimed[a]) entries.push_back({air[a].first, kFloorBlocked, air[a].first, air[a].second, true});
                std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.height < b.height; });
                // The reader of the backend keeps 16 floors of one cell and refuses a tile with more
                // (gamedata/navigation_data.h, NavTile::kMaxFloors): such a cell stops the region, so
                // that no complete region has a tile that the backend cannot use (code review of the
                // corrections after user test 1, finding F8). The tool does not drop a floor.
                if (entries.size() > kMaxCellFloors)
                    throw Failure("a cell at " + std::to_string(x) + " " + std::to_string(y) + " has " + std::to_string(entries.size())
                                  + " floors, more than the reader of the backend keeps (" + std::to_string(kMaxCellFloors) + ")");
                uint32_t freeFloors = 0;
                for (const Entry& entry : entries) {
                    if (entry.flags & kFloorBlocked) { ++out.blocked; continue; }
                    ++freeFloors;
                    if (entry.flags & kFloorShallowWater) ++out.shallow;
                    if (entry.flags & kFloorDeepWater) ++out.deep;
                    if ((entry.flags & kFloorTerrain) == 0) ++out.onCollider;
                }
                out.floors += freeFloors;
                if (freeFloors > 0) ++out.columnsWithFloor;
                if (freeFloors > 1) ++out.columnsWithFloors;
                if (entries.empty()) {
                    code = 0;
                } else if (pureTerrain && entries.size() == 1 && entries[0].airHigh >= kFar * 0.5f) {
                    const uint8_t flags = entries[0].flags;
                    code = (flags & kFloorBlocked) ? 4 : (flags & kFloorDeepWater) ? 3 : (flags & kFloorShallowWater) ? 2 : 1;
                } else {
                    code = kCodeFloors + static_cast<int>(entries.size());
                    for (const Entry& entry : entries) {
                        uint32_t value = static_cast<uint32_t>(std::clamp(std::lround(entry.height * 32.0f), 0L, 65535L));
                        // A blocked floor is only for the sight: steps of 0.25 m are sufficient (and the file is smaller).
                        if (entry.flags & kFloorBlocked) value = std::min<uint32_t>(65528, (value + 7) & ~7u);
                        const float base = static_cast<float>(value) / 32.0f;
                        uint8_t low = kNoSight, high = 0;
                        if (entry.sight) {
                            // An object lower than `kLowObject` above the floor does not stop the sight.
                            float from = std::max(0.0f, entry.airLow - base);
                            if (from < kLowObject) from = 0.0f;
                            if (from < 31.0f) {
                                low = static_cast<uint8_t>(std::min(254L, std::lround(std::ceil(from * 4.0f - 0.01f)) * 2));
                                high = entry.airHigh >= kFar * 0.5f ? kOpenSky
                                                                    : static_cast<uint8_t>(std::clamp(static_cast<long>(std::floor((entry.airHigh - base) * 4.0f)), 0L, 254L));
                            }
                        }
                        extra.push_back(static_cast<uint8_t>(value & 0xFF));
                        extra.push_back(static_cast<uint8_t>(value >> 8));
                        extra.push_back(entry.flags);
                        extra.push_back(low);
                        extra.push_back(high);
                        ++extraCount;
                    }
                }
            }
            if (code != runCode || runLength == 65535) { flush(); runCode = code; }
            ++runLength;
        }
    }
    flush();
    out.extras = extraCount;
    const size_t terrainBytes = out.bytes.size();
    out.bytes.resize(terrainBytes + 8 + runs.size() + extra.size());
    uint8_t* at = out.bytes.data() + terrainBytes;
    std::memcpy(at, &runCount, 4);
    std::memcpy(at + 4, runs.data(), runs.size());
    std::memcpy(at + 4 + runs.size(), &extraCount, 4);
    std::memcpy(at + 8 + runs.size(), extra.data(), extra.size());
    return out;
}

int command_build(const std::string& folder, const std::string& regionName, const std::string& output, int threads) {
    const Agent agent = Agent::from_environment();
    Scene scene;
    scene.folder = folder;
    int rx, ry;
    if (std::sscanf(regionName.c_str(), "r_%d_%d", &rx, &ry) != 2) throw Failure(regionName + " is not a region name");
    Collision* region = scene.region(rx, ry);
    if (region == nullptr) throw Failure("no collision data for " + regionName + " in " + folder);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) scene.region(rx + dx, ry + dy);
    const int tileCells = static_cast<int>(std::lround(kTileMeters / agent.cell));
    const int tiles = kRegionSize / kTileMeters;
    std::vector<TileData> data(static_cast<size_t>(tiles) * tiles);
    std::atomic<int> next{0}, coarseTiles{0};
    std::atomic<size_t> triangles{0}, spans{0};
    const auto start = std::chrono::steady_clock::now();
    std::string error;
    std::mutex errorMutex;
    auto work = [&]() {
        try {
            for (;;) {
                const int tile = next.fetch_add(1);
                if (tile >= tiles * tiles) return;
                const int tx = tile % tiles, ty = tile / tiles;
                const long meterX0 = static_cast<long>(rx) * kRegionSize + static_cast<long>(tx) * kTileMeters;
                const long meterY0 = static_cast<long>(ry) * kRegionSize + static_cast<long>(ty) * kTileMeters;
                // A tile with no collider near it needs no analysis cells: each of its cells is the terrain.
                bool empty = true;
                const double reach = agent.radius + 2.0 * agent.cell;
                scene.placements(meterX0 - reach, meterY0 - reach, meterX0 + kTileMeters + reach, meterY0 + kTileMeters + reach,
                                 [&](Collision& where, uint32_t p) {
                                     const Placement& placement = where.placements[p];
                                     if (placement.mark == kMarkCut || (where.filterFlags[placement.filter] & 1) != 0) empty = false;
                                 });
                const int factor = empty ? 1 : agent.fine;
                if (empty) ++coarseTiles;
                Grid grid;
                grid.agent = agent;
                grid.cell = agent.cell / static_cast<float>(factor);
                const int border = static_cast<int>(std::ceil(agent.radius / grid.cell)) + 1;
                const long perMeter = std::lround(1.0 / grid.cell);   // a whole number: the cell size divides 1 m
                grid.build(scene, meterX0 * perMeter - border, meterY0 * perMeter - border, tileCells * factor + 2 * border,
                           tileCells * factor + 2 * border);
                triangles += grid.triangles;
                spans += grid.spans.size();
                data[tile] = encode_tile(scene, grid, border, tileCells, factor, meterX0, meterY0);
            }
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> lock(errorMutex);
            error = exception.what();
        }
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < std::max(1, threads); ++i) pool.emplace_back(work);
    for (auto& thread : pool) thread.join();
    if (!error.empty()) throw Failure(error);
    const std::string temporary = output + ".part";
    std::ofstream file(temporary, std::ios::binary);
    if (!file) throw Failure("cannot write " + temporary);
    file.write("SWNR", 4);
    file.write(reinterpret_cast<const char*>(&kNavVersion), 4);
    file.write(reinterpret_cast<const char*>(&rx), 4);
    file.write(reinterpret_cast<const char*>(&ry), 4);
    const float values[6] = {agent.cell, agent.radius, agent.height, agent.step, agent.slopeDegrees, agent.wade};
    file.write(reinterpret_cast<const char*>(values), sizeof values);
    const uint32_t layout[2] = {static_cast<uint32_t>(tileCells), static_cast<uint32_t>(tiles)};
    file.write(reinterpret_cast<const char*>(layout), sizeof layout);
    file.write(reinterpret_cast<const char*>(&region->ocean), 4);
    uint64_t offset = static_cast<uint64_t>(file.tellp()) + static_cast<uint64_t>(data.size()) * 16;
    size_t floors = 0, tileBytes = 0, columnsWithFloor = 0, columnsWithFloors = 0, shallow = 0, deep = 0, onCollider = 0, blocked = 0, extras = 0;
    for (const TileData& tile : data) {
        columnsWithFloor += tile.columnsWithFloor; columnsWithFloors += tile.columnsWithFloors;
        shallow += tile.shallow; deep += tile.deep; onCollider += tile.onCollider; blocked += tile.blocked; extras += tile.extras;
        const uint32_t size = static_cast<uint32_t>(tile.bytes.size());
        file.write(reinterpret_cast<const char*>(&offset), 8);
        file.write(reinterpret_cast<const char*>(&size), 4);
        file.write(reinterpret_cast<const char*>(&tile.floors), 4);
        offset += size;
        floors += tile.floors;
        tileBytes += size;
    }
    for (const TileData& tile : data) file.write(reinterpret_cast<const char*>(tile.bytes.data()), static_cast<std::streamsize>(tile.bytes.size()));
    file.close();
    if (!file) throw Failure("cannot write " + temporary);
    if (std::rename(temporary.c_str(), output.c_str()) != 0) throw Failure("cannot move " + temporary);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("{\"region\": \"%s\", \"cell\": %.4f, \"analysisCellsOnASide\": %d, \"radius\": %.3f, \"height\": %.3f, \"step\": %.3f, "
                "\"slope\": %.1f, \"wade\": %.2f, \"tiles\": %d, \"tilesWithNoCollider\": %d, \"cells\": %lld, \"cellsWithFloor\": %zu, "
                "\"cellsWith2Floors\": %zu, \"freeFloors\": %zu, \"floorsOnCollider\": %zu, \"floorsShallowWater\": %zu, "
                "\"floorsDeepWater\": %zu, \"blockedFloors\": %zu, \"floorRecords\": %zu, \"triangles\": %zu, \"spans\": %zu, "
                "\"tileBytes\": %zu, \"bytes\": %llu, \"seconds\": %.2f, \"threads\": %d}\n",
                regionName.c_str(), agent.cell, agent.fine, agent.radius, agent.height, agent.step, agent.slopeDegrees, agent.wade, tiles * tiles,
                coarseTiles.load(), static_cast<long long>(tiles) * tiles * tileCells * tileCells, columnsWithFloor, columnsWithFloors, floors,
                onCollider, shallow, deep, blocked, extras, triangles.load(), spans.load(), tileBytes, static_cast<unsigned long long>(offset),
                seconds, threads);
    return 0;
}

// window file := "SWWN" cell:f32 x0:f64 y0:f64 width:u32 height:u32, then for each column (row by row):
//                terrain:f32 water:f32 spanCount:u16 (low:f32 high:f32 walkable:u8 terrain:u8)*
//                floorCount:u16 (height:f32 ceiling:f32 state:u8 flags:u8)*
int command_window(const std::string& folder, double x0, double y0, double x1, double y1, const std::string& output) {
    const Agent agent = Agent::from_environment();
    Scene scene;
    scene.folder = folder;
    Grid grid;
    grid.agent = agent;
    grid.cell = agent.cell / static_cast<float>(agent.fine);
    const float cell = grid.cell;
    const int border = static_cast<int>(std::ceil(agent.radius / cell)) + 1;
    const long cx0 = static_cast<long>(std::floor(x0 / cell)), cy0 = static_cast<long>(std::floor(y0 / cell));
    const int w = static_cast<int>(std::ceil((x1 - x0) / cell)), h = static_cast<int>(std::ceil((y1 - y0) / cell));
    if (w <= 0 || h <= 0 || static_cast<long>(w) * h > 40000000L) throw Failure("the rectangle is empty or too large");
    grid.build(scene, cx0 - border, cy0 - border, w + 2 * border, h + 2 * border);
    std::ofstream file(output, std::ios::binary);
    if (!file) throw Failure("cannot write " + output);
    file.write("SWWN", 4);
    const double wx0 = cx0 * static_cast<double>(cell), wy0 = cy0 * static_cast<double>(cell);
    file.write(reinterpret_cast<const char*>(&cell), 4);
    file.write(reinterpret_cast<const char*>(&wx0), 8);
    file.write(reinterpret_cast<const char*>(&wy0), 8);
    const uint32_t size[2] = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    file.write(reinterpret_cast<const char*>(size), 8);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t column = static_cast<uint32_t>(y + border) * grid.width + (x + border);
            const double wx = wx0 + (x + 0.5) * cell, wy = wy0 + (y + 0.5) * cell;
            float ground = -kFar;
            scene.terrain(wx, wy, ground);
            const float surface = scene.water(wx, wy, ground);
            file.write(reinterpret_cast<const char*>(&ground), 4);
            file.write(reinterpret_cast<const char*>(&surface), 4);
            const uint16_t spanCount = static_cast<uint16_t>(grid.first[column + 1] - grid.first[column]);
            file.write(reinterpret_cast<const char*>(&spanCount), 2);
            for (uint32_t s = grid.first[column]; s < grid.first[column + 1]; ++s) {
                file.write(reinterpret_cast<const char*>(&grid.spans[s].low), 4);
                file.write(reinterpret_cast<const char*>(&grid.spans[s].high), 4);
                file.write(reinterpret_cast<const char*>(&grid.spans[s].walkable), 1);
                file.write(reinterpret_cast<const char*>(&grid.spans[s].terrain), 1);
            }
            const uint16_t floorCount = static_cast<uint16_t>(grid.firstFloor[column + 1] - grid.firstFloor[column]);
            file.write(reinterpret_cast<const char*>(&floorCount), 2);
            for (uint32_t f = grid.firstFloor[column]; f < grid.firstFloor[column + 1]; ++f) {
                file.write(reinterpret_cast<const char*>(&grid.floors[f].height), 4);
                file.write(reinterpret_cast<const char*>(&grid.floors[f].ceiling), 4);
                file.write(reinterpret_cast<const char*>(&grid.floors[f].state), 1);
                file.write(reinterpret_cast<const char*>(&grid.floors[f].flags), 1);
            }
        }
    }
    std::printf("{\"width\": %d, \"height\": %d, \"placements\": %zu, \"triangles\": %zu, \"spans\": %zu, \"floors\": %zu}\n", w, h,
                grid.placements, grid.triangles, grid.spans.size(), grid.floors.size());
    return 0;
}

// For each point (x y z and a text): the terrain height, the slope, and the nearest surface of a collider in
// the vertical line of the point.
int command_surfaces(const std::string& folder, const std::string& input, const std::string& output) {
    Scene scene;
    scene.folder = folder;
    std::ifstream in(input);
    if (!in) throw Failure("cannot read " + input);
    std::ofstream out(output);
    if (!out) throw Failure("cannot write " + output);
    out << "x\ty\tz\tregion\tterrain\tslope\twater\tsurface\tsurfaceKind\tfilter\tshape\tdistance\troom\tbeside\tlabel\n";
    std::string line;
    size_t count = 0;
    while (std::getline(in, line)) {
        double x, y, z;
        int used = 0;
        if (std::sscanf(line.c_str(), "%lf %lf %lf%n", &x, &y, &z, &used) != 3) continue;
        std::string label = line.substr(static_cast<size_t>(used));
        while (!label.empty() && (label[0] == '\t' || label[0] == ' ')) label.erase(0, 1);
        ++count;
        float ground = -kFar, slope = 0.0f;
        const bool hasTerrain = scene.terrain(x, y, ground, &slope);
        const std::string regionName = Scene::region_name(static_cast<int>(std::floor(x / kRegionSize)), static_cast<int>(std::floor(y / kRegionSize)));
        if (!hasTerrain) {
            out << x << '\t' << y << '\t' << z << '\t' << regionName << "\t\t\t\t\tno data\t\t\t\t\t\t" << label << '\n';
            continue;
        }
        float best = ground, bestDistance = std::fabs(ground - static_cast<float>(z));
        std::string kind = "terrain", filter, shape;
        float room = kFar;   // the free height above the point: the lowest surface that is 0.3 m or more above it
        const float px = static_cast<float>(x), py = static_cast<float>(y);
        scene.placements(x, y, x, y, [&](Collision& region, uint32_t p) {
            const Placement& placement = region.placements[p];
            const uint8_t flags = region.filterFlags[placement.filter];
            if ((flags & 3) == 0) return;
            const uint32_t firstPart = region.shapes[placement.shape].firstPart, partCount = region.shapes[placement.shape].partCount;
            for (uint32_t part = firstPart; part < firstPart + partCount; ++part) {
                const Part& info = region.parts[part];
                for (uint32_t i = 0; i + 2 < info.indexCount; i += 3) {
                    float a[3], b[3], c[3];
                    region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i]) * 3], a);
                    region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i + 1]) * 3], b);
                    region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i + 2]) * 3], c);
                    const float det = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
                    if (std::fabs(det) < 1e-9f) continue;
                    const float u = ((px - a[0]) * (c[1] - a[1]) - (py - a[1]) * (c[0] - a[0])) / det;
                    const float v = ((b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])) / det;
                    if (u < -1e-4f || v < -1e-4f || u + v > 1.0001f) continue;
                    const float height = a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]);
                    const float distance = std::fabs(height - static_cast<float>(z));
                    if (height > static_cast<float>(z) + 0.3f) room = std::min(room, height - static_cast<float>(z));
                    if (distance < bestDistance) {
                        bestDistance = distance; best = height; kind = "collider";
                        filter = region.filterNames[placement.filter];
                        shape = placement.shape < region.shapeNames.size() ? region.shapeNames[placement.shape] : "";
                    }
                }
            }
        });
        // When no surface is in the vertical line: the nearest surface at the height of the point (0.3 m), 1.5 m or less away.
        float beside = -1.0f;
        if (bestDistance > 0.3f) {
            const float pz = static_cast<float>(z);
            float nearest = 1.5f * 1.5f;
            scene.placements(x - 1.5, y - 1.5, x + 1.5, y + 1.5, [&](Collision& region, uint32_t p) {
                const Placement& placement = region.placements[p];
                if ((region.filterFlags[placement.filter] & 3) == 0) return;
                const Box& box = region.boxes[p];
                if (box.lowZ > pz + 0.3f || box.highZ < pz - 0.3f) return;
                const uint32_t firstPart = region.shapes[placement.shape].firstPart, partCount = region.shapes[placement.shape].partCount;
                for (uint32_t part = firstPart; part < firstPart + partCount; ++part) {
                    const Part& info = region.parts[part];
                    for (uint32_t i = 0; i + 2 < info.indexCount; i += 3) {
                        float a[3], b[3], c[3];
                        region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i]) * 3], a);
                        region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i + 1]) * 3], b);
                        region.transform(placement, &region.vertices[(info.firstVertex + region.indexes[info.firstIndex + i + 2]) * 3], c);
                        // Only a surface that an agent can stand on (not steeper than 60 degrees).
                        const float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
                        const float nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
                        const float ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
                        if (std::fabs(nz) < 0.5f * std::sqrt(nx * nx + ny * ny + nz * nz)) continue;
                        // The nearest point of the triangle in the horizontal plane: the corners and the edges.
                        const float* corner[3] = {a, b, c};
                        for (int e = 0; e < 3; ++e) {
                            const float* u = corner[e];
                            const float* v = corner[(e + 1) % 3];
                            const float ex = v[0] - u[0], ey = v[1] - u[1];
                            const float length = ex * ex + ey * ey;
                            const float t = length > 1e-12f ? std::clamp(((px - u[0]) * ex + (py - u[1]) * ey) / length, 0.0f, 1.0f) : 0.0f;
                            const float qx = u[0] + ex * t, qy = u[1] + ey * t, qz = u[2] + (v[2] - u[2]) * t;
                            if (std::fabs(qz - pz) > 0.3f) continue;
                            nearest = std::min(nearest, (qx - px) * (qx - px) + (qy - py) * (qy - py));
                        }
                    }
                }
            });
            if (nearest < 1.5f * 1.5f) beside = std::sqrt(nearest);
        }
        const float surface = scene.water(x, y, ground);
        char text[256];
        std::snprintf(text, sizeof text, "%.3f\t%.3f\t%.3f\t%s\t%.3f\t%.1f\t%.3f\t%.3f\t", x, y, z, regionName.c_str(), ground, slope,
                      surface < -1e8f ? -1.0f : surface, best);
        out << text << kind << '\t' << filter << '\t' << shape << '\t';
        std::snprintf(text, sizeof text, "%.3f\t%.2f\t%.2f\t", bestDistance, room > 1e8f ? -1.0f : room, beside);
        out << text << label << '\n';
    }
    std::printf("{\"points\": %zu}\n", count);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "noise" && argc == 3) return command_noise(argv[2]);
        if (command == "cookies" && argc == 8) return command_cookies(argv[2], std::atof(argv[3]), std::atof(argv[4]), argv[5], argv[6], argv[7]);
        if (command == "build" && (argc == 5 || argc == 6)) return command_build(argv[2], argv[3], argv[4], argc == 6 ? std::atoi(argv[5]) : 4);
        if (command == "window" && argc == 8)
            return command_window(argv[2], std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5]), std::atof(argv[6]), argv[7]);
        if (command == "surfaces" && argc == 5) return command_surfaces(argv[2], argv[3], argv[4]);
        std::fprintf(stderr, "usage: navcore noise OUTPUT.f32\n"
                             "       navcore cookies NOISE.f32 ORIGIN_X ORIGIN_Y HEIGHTS.u16 COOKIES.tsv OUTPUT.u16\n"
                             "       navcore build COLLISION_DIR REGION OUTPUT.raw [threads]\n"
                             "       navcore window COLLISION_DIR X0 Y0 X1 Y1 OUTPUT.bin\n"
                             "       navcore surfaces COLLISION_DIR POINTS.tsv OUTPUT.tsv\n");
        return 1;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "navcore: %s\n", exception.what());
        return 1;
    }
}
