#include "upload_pacer.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

double steadyMilliseconds() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

UnrealPoint moved(const UnrealPoint& point, const UnrealPoint& offset) {
    return {point.x + offset.x, point.y + offset.y, point.z + offset.z};
}

} // namespace

UploadPacer::UploadPacer() : UploadPacer(steadyMilliseconds) {}

UploadPacer::UploadPacer(Clock clock) : clock_(std::move(clock)) {}

void UploadPacer::start(StreamerUpdate update) {
    if (busy_) {
        throw std::logic_error("UploadPacer: an update is still being applied");
    }
    update_ = std::move(update);
    next_ = 0;
    busy_ = !update_.empty();
}

PacedFrame UploadPacer::apply(UploadSink& sink, const UploadBudget& budget) {
    return run(sink, budget);
}

PacedFrame UploadPacer::flush(UploadSink& sink) {
    return run(sink, UploadBudget{});
}

// Before each upload but the first: the time spent, read now, and the estimate of the next one
// must stay within the budget.
PacedFrame UploadPacer::run(UploadSink& sink, const UploadBudget& budget) {
    PacedFrame frame;
    if (!busy_) {
        frame.finished = true;
        return frame;
    }
    const bool bounded = budget.milliseconds > 0.0;
    const double start = clock_();
    double last = start;
    while (next_ < update_.uploads.size()) {
        const TileUpload& upload = update_.uploads[next_];
        if (bounded && frame.uploads > 0) {
            const double spent = last - start;
            const double next = budget.estimate ? budget.estimate(upload) : 0.0;
            if (spent >= budget.milliseconds || spent + next > budget.milliseconds) {
                break;
            }
        }
        sink.upload(upload);
        const double now = clock_();
        if (budget.measured) {
            budget.measured(upload, now - last);
        }
        last = now;
        frame.vertices += upload.mesh.positions.size();
        ++frame.uploads;
        ++next_;
    }
    if (next_ == update_.uploads.size()) {
        for (const TileMove& move : update_.moves) {
            sink.move(move);
        }
        for (const TileKey& key : update_.removals) {
            sink.remove(key);
        }
        update_ = StreamerUpdate{};
        next_ = 0;
        busy_ = false;
        frame.finished = true;
    }
    frame.milliseconds = last - start;
    return frame;
}

bool UploadPacer::holds(const TileKey& key) const {
    for (std::size_t i = next_; busy_ && i < update_.uploads.size(); ++i) {
        if (update_.uploads[i].key == key) {
            return true;
        }
    }
    return false;
}

void UploadPacer::shift(const UnrealPoint& offset) {
    for (std::size_t i = next_; i < update_.uploads.size(); ++i) {
        update_.uploads[i].origin = moved(update_.uploads[i].origin, offset);
    }
    for (TileMove& move : update_.moves) {
        move.origin = moved(move.origin, offset);
    }
}

} // namespace terrain::unreal
