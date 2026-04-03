#include "shader.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>

Shader::~Shader() {
    if (m_id) glDeleteProgram(m_id);
}

bool Shader::load(const std::string& vert_path, const std::string& frag_path) {
    std::string vert_src = read_file(vert_path);
    std::string frag_src = read_file(frag_path);
    if (vert_src.empty() || frag_src.empty()) return false;

    GLuint vert = compile(GL_VERTEX_SHADER,   vert_src);
    GLuint frag = compile(GL_FRAGMENT_SHADER, frag_src);
    if (!vert || !frag) { glDeleteShader(vert); glDeleteShader(frag); return false; }

    m_id = glCreateProgram();
    glAttachShader(m_id, vert);
    glAttachShader(m_id, frag);
    glLinkProgram(m_id);

    GLint ok = 0;
    glGetProgramiv(m_id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_id, 512, nullptr, log);
        std::cerr << "[Shader] Link error: " << log << "\n";
        glDeleteProgram(m_id);
        m_id = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return m_id != 0;
}

void Shader::use() const { glUseProgram(m_id); }

void Shader::set_int(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(m_id, name), v);
}
void Shader::set_float(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_id, name), v);
}
void Shader::set_mat4(const char* name, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(m_id, name), 1, GL_FALSE, glm::value_ptr(v));
}

GLuint Shader::compile(GLenum type, const std::string& src) {
    GLuint id = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(id, 1, &c, nullptr);
    glCompileShader(id);

    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(id, 512, nullptr, log);
        std::cerr << "[Shader] Compile error: " << log << "\n";
        glDeleteShader(id);
        return 0;
    }
    return id;
}

std::string Shader::read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "[Shader] Cannot open: " << path << "\n"; return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
