#pragma once

#include "math.hpp"

#include <glad/gl.h>

namespace viewer {

// Owns a linked GLSL program built from vertex and fragment sources.
class ShaderProgram {
public:
    // Throws std::runtime_error with the GL info log on compile or link failure.
    ShaderProgram(const char* vertexSource, const char* fragmentSource);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    void use() const;
    void setMat4(const char* name, const Mat4& value) const;
    void setVec2(const char* name, float x, float y) const;

private:
    GLuint program_ = 0;
};

} // namespace viewer
