#include "renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>

Renderer::~Renderer() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
}

bool Renderer::init(const char* shader_dir) {
    std::string dir(shader_dir);
    if (!m_shader.load(dir + "/tile.vert", dir + "/tile.frag")) {
        std::cerr << "[Renderer] Shader load failed\n";
        return false;
    }

    // Unit quad in world space: positions + UVs
    // The quad sits at [-0.5, 0.5] x [-0.5, 0.5].
    // UV (0,0) = top-left of image, so V is flipped relative to world Y.
    //  pos.x  pos.y   u     v
    float verts[] = {
       -0.5f, -0.5f,  0.0f, 1.0f,   // bottom-left
        0.5f, -0.5f,  1.0f, 1.0f,   // bottom-right
        0.5f,  0.5f,  1.0f, 0.0f,   // top-right
       -0.5f,  0.5f,  0.0f, 0.0f,   // top-left
    };
    unsigned int indices[] = { 0, 1, 2,  2, 3, 0 };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // position: location 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // uv: location 1
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void Renderer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    m_aspect = float(width) / float(height);
    glViewport(0, 0, width, height);
}

GLuint Renderer::upload_texture(const unsigned char* rgba, int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void Renderer::free_texture(GLuint tex) {
    glDeleteTextures(1, &tex);
}

void Renderer::begin_frame() {
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::draw_tile(GLuint texture, const TileQuad& quad, float opacity) {
    m_shader.use();
    m_shader.set_mat4 ("u_mvp",     compute_mvp(quad));
    m_shader.set_float ("u_opacity", opacity);
    m_shader.set_int   ("u_texture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

glm::mat4 Renderer::compute_mvp(const TileQuad& quad) const {
    // Orthographic projection: maps visible world region to NDC [-1,1]
    float half_h = 0.5f / m_zoom;
    float half_w = half_h * m_aspect;

    glm::mat4 proj = glm::ortho(
        m_pan.x - half_w,  m_pan.x + half_w,
        m_pan.y - half_h,  m_pan.y + half_h,
        -1.0f, 1.0f);

    // Model: place the unit quad at the tile's world-space bounds
    float sx = quad.x_max - quad.x_min;
    float sy = quad.y_max - quad.y_min;
    float cx = (quad.x_min + quad.x_max) * 0.5f;
    float cy = (quad.y_min + quad.y_max) * 0.5f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(cx, cy, 0.0f));
    model = glm::scale(model, glm::vec3(sx, sy, 1.0f));

    return proj * model;
}

glm::vec2 Renderer::screen_to_world(glm::vec2 screen_px) const {
    // Screen pixel → NDC [-1,1]
    float nx =  (screen_px.x / m_width)  * 2.0f - 1.0f;
    float ny = -(screen_px.y / m_height) * 2.0f + 1.0f;

    float half_h = 0.5f / m_zoom;
    float half_w = half_h * m_aspect;

    return { m_pan.x + nx * half_w,
             m_pan.y + ny * half_h };
}
