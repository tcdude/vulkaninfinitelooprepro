#include <kinc/graphics4/graphics.h>
#include <kinc/graphics4/indexbuffer.h>
#include <kinc/graphics4/pipeline.h>
#include <kinc/graphics4/shader.h>
#include <kinc/graphics4/vertexbuffer.h>
#include <kinc/io/filereader.h>
#include <kinc/system.h>
#include <kinc/threads/event.h>
#include <kinc/threads/thread.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static kinc_g4_shader_t vertex_shader;
static kinc_g4_shader_t fragment_shader;
static kinc_g4_pipeline_t pipeline;
static kinc_g4_vertex_buffer_t vertices;
static kinc_g4_index_buffer_t indices;
static kinc_g4_constant_location_t sg_data_loc;
static kinc_g4_constant_location_t i_time_loc;
static kinc_g4_constant_location_t i_resolution_loc;
static kinc_g4_constant_location_t sg_mouse_loc;
static kinc_g4_constant_location_t sg_alpha_loc;

static float sg_data[16] = {
    4.500f,  0.000f,  0.000f,  -12.000f, 0.000f,  0.000f,  0.000f,  0.020f, 0.020f,   0.020f,
    0.870f,  0.880f,  0.860f,  0.180f,   0.250f,  0.310f};

#define HEAP_SIZE 1024 * 1024
static uint8_t *heap = NULL;
static size_t heap_top = 0;

static void *allocate(size_t size) {
	size_t old_top = heap_top;
	heap_top += size;
	assert(heap_top <= HEAP_SIZE);
	return &heap[old_top];
}

extern int krafix_compile(const char *source, char *output, int *length, const char *targetlang,
                          const char *system, const char *shadertype);

static int compile_shader(char *output, const char *source, int *length,
                          kinc_g4_shader_type_t type) {
#ifdef KINC_WINDOWS
	const char *system = "windows";
#elif defined(KINC_MACOS)
	const char *system = "macos";
#elif defined(KINC_LINUX)
	const char *system = "linux";
#elif defined(KINC_ANDROID)
	const char *system = "android";
#elif defined(KINC_IOS)
	const char *system = "ios";
#else
	const char *system = "unknown";
#endif

#ifdef KORE_VULKAN
	const char *targetlang = "spirv";
#elif defined(KORE_DIRECT3D11) || defined(KORE_DIRECT3D12)
	const char *targetlang = "d3d11";
#elif defined(KORE_DIRECT3D9) || defined(KORE_DIRECT3D12)
	const char *targetlang = "d3d9";
#elif defined(KORE_METAL)
	const char *targetlang = "metal";
#elif defined(KORE_OPENGL)
#if !defined(KINC_ANDROID) && !defined(KORE_EMSCRIPTEN)
	const char *targetlang = "glsl";
#else
	const char *targetlang = "essl";
#endif
#endif

	int errors = krafix_compile(source, output, length, targetlang, system,
	                            type == KINC_G4_SHADER_TYPE_FRAGMENT ? "frag" : "vert");
	if (errors > 0) {
		printf("Unable to compile this shader:\n\n%s", source);
		return errors;
	}
	return 0;
}

typedef struct twoevents {
	kinc_event_t in;
	kinc_event_t out;
} twoevents_t;

static char *vert_src = NULL;
static char *frag_src = NULL;

static char *vert_compiled = NULL;
static char *frag_compiled = NULL;
static int vertc_len;
static int fragc_len;

static void compile_worker(void *data) {
	twoevents_t *inout = (twoevents_t *)data;
	kinc_event_wait(&inout->in);
	char buffer[32 * 1024];
	memset(buffer, 0, 32 * 1024);
	int length;
	int err = compile_shader(buffer, vert_src, &length, KINC_G4_SHADER_TYPE_VERTEX);
	assert(err == 0);
	vert_compiled = (char *)allocate(length);
	memcpy(vert_compiled, buffer, length);
	vertc_len = length;

	memset(buffer, 0, 32 * 1024);
	err = compile_shader(buffer, frag_src, &length, KINC_G4_SHADER_TYPE_FRAGMENT);
	assert(err == 0);
	frag_compiled = (char *)allocate(length);
	memcpy(frag_compiled, buffer, length);
	fragc_len = length;

	err = compile_shader(buffer, vert_src, &length, KINC_G4_SHADER_TYPE_VERTEX);
	assert(err == 0);
	vert_compiled = (char *)allocate(length);
	memcpy(vert_compiled, buffer, length);
	vertc_len = length;

	memset(buffer, 0, 32 * 1024);
	err = compile_shader(buffer, frag_src, &length, KINC_G4_SHADER_TYPE_FRAGMENT);
	assert(err == 0);
	frag_compiled = (char *)allocate(length);
	memcpy(frag_compiled, buffer, length);
	fragc_len = length;
	kinc_event_signal(&inout->out);
}

static twoevents_t events;
static bool ct_signaled = false;
static bool pp_ready = false;

static void update(void *data) {
	kinc_g4_begin(0);

	if (!ct_signaled) {
		kinc_event_signal(&events.in);
		ct_signaled = true;
	}

	if (!pp_ready && kinc_event_try_to_wait(&events.out, 0.0001)) {

		kinc_g4_shader_init(&vertex_shader, vert_compiled, vertc_len, KINC_G4_SHADER_TYPE_VERTEX);
		kinc_g4_shader_init(&fragment_shader, frag_compiled, fragc_len,
		                    KINC_G4_SHADER_TYPE_FRAGMENT);

		FILE *fp = NULL;
		fp = fopen("shader.vert.spirv", "wb");
		assert(fp != NULL);
		fwrite(vert_compiled, vertc_len, 1, fp);
		fclose(fp);

		fp = NULL;
		fp = fopen("shader.frag.spirv", "wb");
		assert(fp != NULL);
		fwrite(frag_compiled, fragc_len, 1, fp);
		fclose(fp);

		kinc_g4_vertex_structure_t structure;
		kinc_g4_vertex_structure_init(&structure);
		kinc_g4_vertex_structure_add(&structure, "vertex_position", KINC_G4_VERTEX_DATA_FLOAT2);
		kinc_g4_vertex_structure_add(&structure, "vertex_uv", KINC_G4_VERTEX_DATA_FLOAT2);
		kinc_g4_pipeline_init(&pipeline);
		pipeline.vertex_shader = &vertex_shader;
		pipeline.fragment_shader = &fragment_shader;
		pipeline.input_layout[0] = &structure;
		pipeline.input_layout[1] = NULL;
		pipeline.blend_source = KINC_G4_BLEND_SOURCE_ALPHA;
		pipeline.blend_destination = KINC_G4_BLEND_INV_SOURCE_ALPHA;
		pipeline.alpha_blend_source = KINC_G4_BLEND_SOURCE_ALPHA;
		pipeline.alpha_blend_destination = KINC_G4_BLEND_INV_SOURCE_ALPHA;
		kinc_g4_pipeline_compile(&pipeline);
		i_time_loc = kinc_g4_pipeline_get_constant_location(&pipeline, "i_time");
		i_resolution_loc = kinc_g4_pipeline_get_constant_location(&pipeline, "i_resolution");
		sg_data_loc = kinc_g4_pipeline_get_constant_location(&pipeline, "sg_data");
		sg_alpha_loc = kinc_g4_pipeline_get_constant_location(&pipeline, "sg_alpha");
		sg_mouse_loc = kinc_g4_pipeline_get_constant_location(&pipeline, "sg_mouse");

		kinc_g4_vertex_buffer_init(&vertices, 3, &structure, KINC_G4_USAGE_STATIC, 0);
		{
			float *v = kinc_g4_vertex_buffer_lock_all(&vertices);
			int i = 0;

			v[i++] = -1;
			v[i++] = -1;

			v[i++] = 0.0;
			v[i++] = 0.0;

			v[i++] = 1;
			v[i++] = -1;

			v[i++] = 1.0;
			v[i++] = 0.0;

			v[i++] = -1;
			v[i++] = 1;

			v[i++] = 0.0;
			v[i++] = 1.0;

			kinc_g4_vertex_buffer_unlock_all(&vertices);
		}

		kinc_g4_index_buffer_init(&indices, 3, KINC_G4_INDEX_BUFFER_FORMAT_16BIT,
		                          KINC_G4_USAGE_STATIC);
		{
			uint16_t *i = (uint16_t *)kinc_g4_index_buffer_lock_all(&indices);
			i[0] = 0;
			i[1] = 1;
			i[2] = 2;
			kinc_g4_index_buffer_unlock_all(&indices);
		}
		pp_ready = true;
	}

	kinc_g4_clear(KINC_G4_CLEAR_COLOR, 0, 0.0f, 0);

	if (pp_ready) {
		kinc_g4_set_pipeline(&pipeline);
		kinc_g4_set_vertex_buffer(&vertices);
		kinc_g4_set_index_buffer(&indices);
		kinc_g4_set_floats(sg_data_loc, sg_data, 16);
		kinc_g4_set_float2(sg_alpha_loc, 1.0f, 1.0f);
		kinc_g4_set_float2(sg_mouse_loc, 0, 0);
		kinc_g4_set_float2(i_resolution_loc, 1024, 768);
		kinc_g4_set_float(i_time_loc, 0);
		kinc_g4_draw_indexed_vertices();
	}

	kinc_g4_end(0);
	kinc_g4_swap_buffers();
}

static void load_shader(const char *filename, kinc_g4_shader_t *shader,
                        kinc_g4_shader_type_t shader_type) {
	kinc_file_reader_t file;
	kinc_file_reader_open(&file, filename, KINC_FILE_TYPE_ASSET);
	size_t data_size = kinc_file_reader_size(&file);
	char *data = (char *)allocate(data_size + 1);
	kinc_file_reader_read(&file, data, data_size);
	kinc_file_reader_close(&file);
	data[data_size] = 0;

	char *compiled = NULL;
	if (shader_type == KINC_G4_SHADER_TYPE_VERTEX) {
		vert_src = (char *)allocate(data_size + 1);
		memcpy(vert_src, data, data_size + 1);
	}
	else {
		frag_src = (char *)allocate(data_size * 4);
		memcpy(frag_src, data, data_size + 1);
	}
}

int kickstart(int argc, char **argv) {
	kinc_init("Shader", 1024, 768, NULL, NULL);
	kinc_set_update_callback(update, NULL);

	heap = (uint8_t *)malloc(HEAP_SIZE);
	assert(heap != NULL);

	load_shader("shader.vert.glsl", &vertex_shader, KINC_G4_SHADER_TYPE_VERTEX);
	load_shader("shader.frag.glsl", &fragment_shader, KINC_G4_SHADER_TYPE_FRAGMENT);

	kinc_threads_init();
	kinc_event_init(&events.in, false);
	kinc_event_init(&events.out, false);
	kinc_thread_t thread;
	kinc_thread_init(&thread, compile_worker, &events);

	kinc_start();

	return 0;
}
