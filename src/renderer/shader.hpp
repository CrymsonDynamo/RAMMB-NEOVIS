#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>

class Shader {
public:
    Shader() = default;
    ~Shader();

    // Non-copyable, movable
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }

    bool load(const std::string& vert_path, const std::string& frag_path);
    void use() const;

    void set_int  (const char* name, int              v) const;
    void set_float(const char* name, float            v) const;
    void set_mat4 (const char* name, const glm::mat4& v) const;

    bool valid() const { return m_id != 0; }

private:
    GLuint m_id{0};

    static GLuint       compile(GLenum type, const std::string& src);
    static std::string  read_file(const std::string& path);
};
