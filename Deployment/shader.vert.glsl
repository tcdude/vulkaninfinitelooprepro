#version 450
in vec2 vertex_position;
in vec2 vertex_uv;
out vec2 uv;
uniform vec4 sg_data[14];
void main() {
gl_Position = vec4(vertex_position, 0.0, 1.0);
uv = vertex_uv;
}
