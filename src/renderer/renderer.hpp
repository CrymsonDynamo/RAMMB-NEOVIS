#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "shader.hpp"

// World space: the full satellite image occupies a unit square
//   X: [-0.5,  0.5] left → right
//   Y: [-0.5,  0.5] bottom → top
//
// Each tile at zoom Z, row R, col C occupies:
//   x: [ col/N - 0.5,  (col+1)/N - 0.5 ]   where N = 2^Z
//   y: [ 0.5 - (row+1)/N,  0.5 - row/N  ]   (image rows are top-down)

struct TileQuad {
    float x_min, x_max;
    float y_min, y_max;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(const char* shader_dir);
    void resize(int width, int height);

    // Upload decoded RGBA pixel data as a GPU texture. Returns GL texture ID.
    GLuint upload_texture(const unsigned char* rgba, int w, int h);
    void   free_texture(GLuint tex);

    void begin_frame();
    void draw_tile(GLuint texture, const TileQuad& quad, float opacity = 1.0f);
    // end_frame is implicit – caller swaps buffers

    // Camera
    void      set_pan(glm::vec2 pan) { m_pan  = pan;  }
    void      set_zoom(float zoom)   { m_zoom = zoom; }
    glm::vec2 pan()  const { return m_pan;  }
    float     zoom() const { return m_zoom; }

    // Convert a screen pixel position to world space
    glm::vec2 screen_to_world(glm::vec2 screen_px) const;

private:
    int   m_width{1}, m_height{1};
    float m_aspect{1.0f};

    glm::vec2 m_pan{0.0f, 0.0f};
    float     m_zoom{1.0f};

    Shader m_shader;
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_ebo{0};

    glm::mat4 compute_mvp(const TileQuad& quad) const;
};
