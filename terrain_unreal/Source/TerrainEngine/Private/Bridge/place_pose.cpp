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

// value brought into (-180, 180], as FRotator::NormalizeAxis.
double degrees180(double value) {
    const double wrapped = degrees360(value);
    return wrapped > 180.0 ? wrapped - 360.0 : wrapped;
}

// value rounded to 1 / perUnit: an integer divided by perUnit, so that 182.03 is the double that
// "182.03" reads to.
double roundTo(double value, double perUnit) {
    return std::round(value * perUnit) / perUnit;
}

} // namespace

double yawOfHeading(double heading) {
    return degrees180(heading - 90.0);
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

bool columnHasWater(const HeightSample& sample) {
    return sample.water != WaterKind::None && sample.waterLevel > sample.height;
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

UnrealRotation viewRotation(const ViewPose& pose) {
    return {std::clamp(pose.pitch, -90.0, 90.0), pose.yaw, 0.0};
}

UnrealRotation viewRotation(double heading, double pitch) {
    return {std::clamp(pitch, -90.0, 90.0), yawOfHeading(heading), 0.0};
}

UnrealDirection lookDirection(const UnrealRotation& rotation) {
    constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
    const double pitch = rotation.pitch * kRadiansPerDegree;
    const double yaw = rotation.yaw * kRadiansPerDegree;
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}

ViewPose cameraPose(const CorePoint& eye, double pitch, double yaw) {
    ViewPose pose;
    pose.eye = eye;
    pose.yaw = degrees180(yaw);
    pose.pitch = std::clamp(degrees180(pitch), -90.0, 90.0);
    return pose;
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
