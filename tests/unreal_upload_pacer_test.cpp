// The uploads of the updates of the streamer applied within a time per frame (agent 38,
// Bridge/upload_pacer.hpp): the real time read after each upload stops the frame, so that an
// estimate ten times too low makes a frame go beyond its budget by one upload at most; one upload
// a frame at least; the moves and the removals once every upload of the update is applied; the
// uploads left are not shown yet, and follow the world origin.
#include "upload_pacer.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace {

using terrain::TileKey;
using terrain::unreal::PacedFrame;
using terrain::unreal::StreamerUpdate;
using terrain::unreal::TileMove;
using terrain::unreal::TileUpload;
using terrain::unreal::UnrealPoint;
using terrain::unreal::UploadBudget;
using terrain::unreal::UploadPacer;

constexpr double kBudget = 6.0;

// The meshes of the test: each upload takes its true time (written in conversionMilliseconds) on
// the clock of the test, the start of each upload of the frame kept.
struct FakeSink final : terrain::unreal::UploadSink {
    double now = 0.0;
    std::vector<TileUpload> uploaded;
    std::vector<double> starts;
    std::vector<TileKey> moved;
    std::vector<TileKey> removed;

    void upload(const TileUpload& upload) override {
        starts.push_back(now);
        uploaded.push_back(upload);
        now += upload.conversionMilliseconds;
    }
    void move(const TileMove& move) override { moved.push_back(move.key); }
    void remove(const TileKey& key) override { removed.push_back(key); }
};

TileKey keyOf(int index) {
    return {{index * 16, 0}, 0};
}

// count uploads of 1 to 5.9 ms (the heavy LOD 0 tiles of the fall: up to 6 ms), a move and a
// removal.
StreamerUpdate updateOf(int count) {
    StreamerUpdate update;
    for (int i = 0; i < count; ++i) {
        TileUpload upload;
        upload.key = keyOf(i);
        upload.origin = {static_cast<double>(i), 0.0, 0.0};
        upload.conversionMilliseconds = 1.0 + static_cast<double>((i * 37) % 50) / 10.0;
        upload.mesh.positions.resize(830);
        update.uploads.push_back(upload);
    }
    update.moves.push_back({{{-16, 0}, 1}, {1.0, 2.0, 3.0}});
    update.removals.push_back({{-32, 0}, 2});
    return update;
}

// Applies the update frame by frame; each frame must upload one tile at least, start every
// upload within the budget and measure its time; the moves and removals come with the last one.
int applyAll(UploadPacer& pacer, FakeSink& sink, const UploadBudget& budget, bool& ok,
             bool exact) {
    int frames = 0;
    while (pacer.busy() && frames < 1000) {
        const std::size_t before = sink.uploaded.size();
        const double start = sink.now;
        sink.starts.clear();
        const PacedFrame frame = pacer.apply(sink, budget);
        ok = ok && frame.uploads >= 1 && frame.uploads == sink.uploaded.size() - before;
        ok = ok && frame.vertices == 830 * frame.uploads;
        ok = ok && std::abs(frame.milliseconds - (sink.now - start)) < 1e-9;
        for (const double begun : sink.starts) {
            ok = ok && begun - start < kBudget; // beyond the budget by the last upload at most
        }
        if (exact) {
            ok = ok && (frame.uploads == 1 || frame.milliseconds <= kBudget);
        }
        ok = ok && frame.finished == !pacer.busy() && sink.removed.empty() == pacer.busy();
        ++frames;
    }
    return frames;
}

bool inOrder(const FakeSink& sink, int count) {
    bool ok = static_cast<int>(sink.uploaded.size()) == count;
    for (int i = 0; ok && i < count; ++i) {
        ok = sink.uploaded[static_cast<std::size_t>(i)].key == keyOf(i);
    }
    return ok && sink.moved.size() == 1 && sink.removed.size() == 1;
}

// An estimate ten times too low lets the real time stop the frames: never beyond the budget by
// more than one upload, every tile uploaded once and in order, the removals at the end.
void testTenTimesTooLow() {
    FakeSink sink;
    UploadPacer pacer([&sink] { return sink.now; });
    pacer.start(updateOf(40));
    UploadBudget budget;
    budget.milliseconds = kBudget;
    budget.estimate = [](const TileUpload& upload) { return upload.conversionMilliseconds / 10.0; };
    bool ok = true;
    const int frames = applyAll(pacer, sink, budget, ok, false);
    TEST_CHECK(ok && !pacer.busy());
    TEST_CHECK(inOrder(sink, 40));
    TEST_CHECK(frames >= 10); // two or three uploads a frame at most, not all at once
}

// An exact estimate keeps every frame of more than one upload within the budget; each upload
// measured is told with its time.
void testExactEstimate() {
    FakeSink sink;
    UploadPacer pacer([&sink] { return sink.now; });
    pacer.start(updateOf(40));
    UploadBudget budget;
    budget.milliseconds = kBudget;
    budget.estimate = [](const TileUpload& upload) { return upload.conversionMilliseconds; };
    std::vector<double> measured;
    budget.measured = [&measured](const TileUpload& upload, double milliseconds) {
        measured.push_back(milliseconds - upload.conversionMilliseconds);
    };
    bool ok = true;
    applyAll(pacer, sink, budget, ok, true);
    TEST_CHECK(ok && inOrder(sink, 40) && measured.size() == 40);
    for (const double error : measured) {
        TEST_CHECK(std::abs(error) < 1e-9);
    }
}

// Without a budget, the whole update in one frame.
void testWholeUpdate() {
    FakeSink sink;
    UploadPacer pacer([&sink] { return sink.now; });
    pacer.start(updateOf(40));
    const PacedFrame frame = pacer.apply(sink, UploadBudget{});
    TEST_CHECK(frame.finished && frame.uploads == 40 && !pacer.busy() && inOrder(sink, 40));
}

// The uploads left are held (not shown in Unreal yet) and follow the world origin; the applied
// ones keep theirs.
void testHoldsAndShift() {
    FakeSink sink;
    UploadPacer pacer([&sink] { return sink.now; });
    pacer.start(updateOf(10));
    UploadBudget budget;
    budget.milliseconds = kBudget;
    const PacedFrame first = pacer.apply(sink, budget);
    TEST_CHECK(pacer.busy() && first.uploads >= 1 && first.uploads < 10);
    const int applied = static_cast<int>(first.uploads);
    TEST_CHECK(!pacer.holds(keyOf(0)) && pacer.holds(keyOf(applied)) && pacer.holds(keyOf(9)));
    TEST_CHECK(!pacer.holds({{-32, 0}, 2}));
    pacer.shift({100.0, -50.0, 0.0});
    TEST_CHECK(pacer.flush(sink).finished && !pacer.busy() && !pacer.holds(keyOf(9)));
    bool ok = inOrder(sink, 10);
    for (int i = 0; ok && i < 10; ++i) {
        const UnrealPoint origin = sink.uploaded[static_cast<std::size_t>(i)].origin;
        const double shift = i < applied ? 0.0 : 100.0;
        ok = origin.x == static_cast<double>(i) + shift && origin.y == (i < applied ? 0.0 : -50.0);
    }
    TEST_CHECK(ok);
}

// An empty update leaves the pacer free; a free pacer applies nothing; a new update while one is
// applied is refused.
void testEmptyAndBusy() {
    FakeSink sink;
    UploadPacer pacer([&sink] { return sink.now; });
    pacer.start(StreamerUpdate{});
    TEST_CHECK(!pacer.busy());
    const PacedFrame idle = pacer.apply(sink, UploadBudget{});
    TEST_CHECK(idle.finished && idle.uploads == 0 && sink.uploaded.empty());
    pacer.start(updateOf(3));
    bool refused = false;
    try {
        pacer.start(updateOf(1));
    } catch (const std::logic_error&) {
        refused = true;
    }
    TEST_CHECK(refused && pacer.busy());
}

} // namespace

int main() {
    testTenTimesTooLow();
    testExactEstimate();
    testWholeUpdate();
    testHoldsAndShift();
    testEmptyAndBusy();
    return test::result();
}
