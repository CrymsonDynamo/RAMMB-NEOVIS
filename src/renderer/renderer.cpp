#include "renderer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

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
    resize_viewport(0, 0, width, height);
}

void Renderer::resize_viewport(int x, int y, int width, int height) {
    m_vp_x   = x;
    m_vp_y   = y;
    m_vp_w   = std::max(1, width);
    m_vp_h   = std::max(1, height);
    m_aspect = float(m_vp_w) / float(m_vp_h);
    glViewport(m_vp_x, m_vp_y, m_vp_w, m_vp_h);
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
    // m_aspect already reflects the viewport sub-region (excludes sidebar)
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

bool Renderer::render_offscreen(const std::vector<std::pair<GLuint, TileQuad>>& tiles,
                                float crop_x_min, float crop_x_max,
                                float crop_y_min, float crop_y_max,
                                int   out_w,      int   out_h,
                                OffscreenResult&  out) {
    // Save current viewport state
    GLint prev_vp[4];
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    GLint prev_fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

    // Create FBO + texture + depth renderbuffer
    GLuint fbo = 0, color_tex = 0, rbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &color_tex);
    glBindTexture(GL_TEXTURE_2D, color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, out_w, out_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, out_w, out_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color_tex);
        glDeleteRenderbuffers(1, &rbo);
        return false;
    }

    // Render into FBO using crop region as the ortho projection
    glViewport(0, 0, out_w, out_h);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    float saved_pan_x = m_pan.x, saved_pan_y = m_pan.y;
    float saved_zoom  = m_zoom;
    int   saved_vp_x  = m_vp_x, saved_vp_y = m_vp_y;
    int   saved_vp_w  = m_vp_w, saved_vp_h = m_vp_h;

    // Override camera to match crop region exactly
    m_pan.x  = (crop_x_min + crop_x_max) * 0.5f;
    m_pan.y  = (crop_y_min + crop_y_max) * 0.5f;
    m_aspect = float(out_w) / float(out_h);
    // Zoom such that crop_h fills the viewport vertically
    float crop_h = crop_y_max - crop_y_min;
    float crop_w = crop_x_max - crop_x_min;
    // Use the tighter constraint so the crop fits exactly
    float zoom_from_h = 1.0f / crop_h;
    float zoom_from_w = (1.0f / crop_w) * m_aspect; // convert world-w to zoom
    m_zoom = std::min(zoom_from_h, zoom_from_w);
    m_vp_x = 0; m_vp_y = 0; m_vp_w = out_w; m_vp_h = out_h;

    for (const auto& [tex, quad] : tiles)
        draw_tile(tex, quad);

    // Readback (OpenGL is bottom-up, flip to top-down)
    std::vector<uint8_t> raw(out_w * out_h * 4);
    glReadPixels(0, 0, out_w, out_h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    out.rgba.resize(out_w * out_h * 4);
    out.width  = out_w;
    out.height = out_h;
    for (int row = 0; row < out_h; ++row)
        memcpy(out.rgba.data() + row * out_w * 4,
               raw.data() + (out_h - 1 - row) * out_w * 4,
               out_w * 4);

    // Restore state
    m_pan.x = saved_pan_x; m_pan.y = saved_pan_y;
    m_zoom  = saved_zoom;
    m_vp_x  = saved_vp_x;  m_vp_y = saved_vp_y;
    m_vp_w  = saved_vp_w;  m_vp_h = saved_vp_h;
    m_aspect = float(m_vp_w) / float(m_vp_h);

    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &color_tex);
    glDeleteRenderbuffers(1, &rbo);
    return true;
}

glm::vec2 Renderer::screen_to_world(glm::vec2 screen_px) const {
    // Offset by viewport origin, then normalize to [-1,1] within viewport
    float nx =  ((screen_px.x - m_vp_x) / m_vp_w) * 2.0f - 1.0f;
    float ny = -((screen_px.y - m_vp_y) / m_vp_h) * 2.0f + 1.0f;

    float half_h = 0.5f / m_zoom;
    float half_w = half_h * m_aspect;

    return { m_pan.x + nx * half_w,
             m_pan.y + ny * half_h };
}
