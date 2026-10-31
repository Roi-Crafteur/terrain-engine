#include "place_pose.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

namespace {

// value brought into [0, 360).
double degrees360(double value) {
    double wrapped = std::fmod(value, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped >= 360.0 ? 0.0 : wrapped;
}

// value rounded to 1 / perUnit: an integer divided by perUnit, so that 182.03 is the double that
// "182.03" reads to.
double roundTo(double value, double perUnit) {
    return std::round(value * perUnit) / perUnit;
}

} // namespace

double yawOfHeading(double heading) {
    const double yaw = degrees360(heading - 90.0);
    return yaw > 180.0 ? yaw - 360.0 : yaw;
}

double headingOfYaw(double yaw) {
    return degrees360(yaw + 90.0);
}

double columnTop(const HeightSample& sample) {
    const double ground = sample.height;
    if (sample.water == WaterKind::None) {
        return ground;
    }
    return std::max(ground, static_cast<double>(sample.waterLevel));
}

double eyeAltitude(double eye, dev::EyeMode mode, double top) {
    return mode == dev::EyeMode::Ground ? top + eye : eye;
}

ViewPose viewPose(double x, double z, double heading, double pitch, double eye,
                  dev::EyeMode mode, double top) {
    ViewPose pose;
    pose.eye = {x, eyeAltitude(eye, mode, top), z};
    pose.yaw = yawOfHeading(heading);
    pose.pitch = std::clamp(pitch, -90.0, 90.0);
    return pose;
}

ViewPose placePose(const dev::Place& place, double top) {
    return viewPose(place.x, place.z, place.heading, place.pitch, place.eye, place.eyeMode, top);
}

dev::Place placeOfView(const std::string& name, const std::string& note, const ViewPose& pose,
                       bool planet, const WorldSize& world) {
    constexpr double kCentimeters = 100.0; // per meter
    constexpr double kHundredths = 100.0;  // per degree
    dev::Place place;
    place.name = name;
    place.note = note;
    place.x = wrapCoordinate(roundTo(wrapCoordinate(pose.eye.x, world.x), kCentimeters), world.x);
    place.z = wrapCoordinate(roundTo(wrapCoordinate(pose.eye.z, world.z), kCentimeters), world.z);
    place.heading = degrees360(roundTo(headingOfYaw(pose.yaw), kHundredths));
    place.pitch = std::clamp(roundTo(pose.pitch, kHundredths), -90.0, 90.0);
    place.eye = roundTo(pose.eye.y, kCentimeters);
    place.eyeMode = dev::EyeMode::Altitude;
    place.planet = planet;
    dev::validPlace(place);
    return place;
}

} // namespace terrain::unreal
