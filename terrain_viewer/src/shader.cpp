#include "shader.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace viewer {

namespace {

using InfoLog = std::array<GLchar, 1024>;

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }
    InfoLog log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error(std::string("shader compilation failed: ") + log.data());
}

} // namespace

ShaderProgram::ShaderProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return;
    }
    InfoLog log{};
    glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteProgram(program_);
    throw std::runtime_error(std::string("shader link failed: ") + log.data());
}

ShaderProgram::~ShaderProgram() {
    glDeleteProgram(program_);
}

void ShaderProgram::use() const {
    glUseProgram(program_);
}

void ShaderProgram::setMat4(const char* name, const Mat4& value) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, value.m.data());
}

void ShaderProgram::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(program_, name), x, y);
}

} // namespace viewer
