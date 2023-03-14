#version 450

in vec2 uv;

out vec4 frag_color;

uniform float i_time;
uniform vec2 i_resolution;
uniform vec2 sg_alpha;
uniform vec2 sg_mouse;

// IQs' magic
float dot2(in vec2 v) {
	return dot(v, v);
}
float dot2(in vec3 v) {
	return dot(v, v);
}
float ndot(in vec2 a, in vec2 b) {
	return a.x * b.x - a.y * b.y;
}

// CSG

vec2 smin_cubic(float a, float b, float k) {
	float h = max(k - abs(a - b), 0.0) / k;
	float m = h * h * h * 0.5;
	float s = m * k * (1.0 / 3.0);
	return (a < b) ? vec2(a - s, m) : vec2(b - s, 1.0 - m);
}

vec2 smax_cubic(float a, float b, float k) {
	float h = max(k - abs(a - b), 0.0) / k;
	float m = h * h * h * 0.5;
	float s = m * k * (1.0 / 3.0);
	return (a > b) ? vec2(a - s, m) : vec2(b - s, 1.0 - m);
}

vec4 sdf_union(vec4 a, vec4 b) {
	return (a.a < b.a) ? a : b;
}

vec4 sdf_subtraction(vec4 a, vec4 b) {
	return (-a.a > b.a) ? vec4(a.rgb, -a.a) : b;
}

vec4 sdf_intersection(vec4 a, vec4 b) {
	return (a.a > b.a) ? a : b;
}

vec4 sdf_smooth_union(vec4 a, vec4 b, float k) {
	vec2 sm = smin_cubic(a.a, b.a, k);
	vec3 col = mix(a.rgb, b.rgb, sm.y);
	return vec4(col, sm.x);
}

vec4 sdf_smooth_subtraction(vec4 a, vec4 b, float k) {
	vec2 sm = smax_cubic(-a.a, b.a, k);
	vec3 col = mix(a.rgb, b.rgb, sm.y);
	return vec4(col, sm.x);
}

vec4 sdf_smooth_intersection(vec4 a, vec4 b, float k) {
	vec2 sm = smax_cubic(a.a, b.a, k);
	vec3 col = mix(a.rgb, b.rgb, sm.y);
	return vec4(col, sm.x);
}

// Tools for GLSL 1.10
vec3 dumbround(vec3 v) {
	v += 0.5f;
	return vec3(float(int(v.x)), float(int(v.y)), float(int(v.z)));
}

vec4 render();

void main() {
	frag_color = render() - (i_time * i_resolution.x * 1e-20);
}

////---- shadergen_source ----////
float sd_box(vec3 p, vec3 b) {
vec3 q = abs(p) - b;
return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);}

uniform vec4 sg_data[14];
vec4 sg_node4(vec3 p);vec4 sg_node4(vec3 p) {return vec4(sg_data[12][0],sg_data[12][1],sg_data[12][2],sd_box(p,vec3(sg_data[11][1],sg_data[11][2],sg_data[11][3])));}
vec4 get_dist(vec3 p) {vec4 dist=vec4(0.0,0.0,0.0,1000.0);dist = sdf_union(sg_node4(p), dist);return dist;}

#define MAX_STEPS int(sg_data[9][2])
#define MAX_DIST sg_data[9][3]
#define SURF_DIST sg_data[10][0]

#define shininess sg_data[10][1]

vec4 raymarch(vec3 ro, vec3 rd) {
	float dO = 0.0;
	vec3 col = vec3(0.0);
	for (int i = 0; i < MAX_STEPS; i++) {
		vec3 p = ro + rd * dO;
		vec4 dS = get_dist(p);
		dO += dS.a;
		if (abs(dS.a) < SURF_DIST) col = dS.rgb;
		if (dO > MAX_DIST || abs(dS.a) < SURF_DIST) break;
	}
	return vec4(col, dO);
}

vec3 get_normal(vec3 p) {
	const float h = 0.001;
	const vec2 k = vec2(1.0, -1.0);

	return normalize(k.xyy * get_dist(p + k.xyy * h).a + k.yyx * get_dist(p + k.yyx * h).a +
	                 k.yxy * get_dist(p + k.yxy * h).a + k.xxx * get_dist(p + k.xxx * h).a);
}

vec3 get_ray_direction(vec2 nuv, vec3 p, vec3 l, float z) {
	vec3 f = normalize(l - p);
	vec3 r = normalize(cross(vec3(0.0, 1.0, 0.0), f));
	vec3 u = cross(f, r);
	vec3 c = f * z;
	vec3 i = c + nuv.x * r + nuv.y * u;
	vec3 d = normalize(i);
	return d;
}

// https://iquilezles.org/articles/nvscene2008/rwwtt.pdf
float calc_ao(vec3 pos, vec3 nor) {
	float occ = 0.0;
	float sca = 1.0;
	for (int i = 0; i < 5; i++) {
		float h = 0.01 + 0.12 * float(i) / 4.0;
		float d = get_dist(pos + h * nor).a;
		occ += (h - d) * sca;
		sca *= 0.95;
		if (occ > 0.35) break;
	}
	return clamp(1.0 - 3.0 * occ, 0.0, 1.0) * (0.5 + 0.5 * nor.y);
}

mat3 set_camera(in vec3 ro, in vec3 ta, float cr) {
	vec3 cw = normalize(ta - ro);
	vec3 cp = vec3(sin(cr), cos(cr), 0.0);
	vec3 cu = normalize(cross(cw, cp));
	vec3 cv = (cross(cu, cw));
	return mat3(cu, cv, cw);
}

vec3 spherical_harmonics(vec3 n) {
	const float C1 = 0.429043;
	const float C2 = 0.511664;
	const float C3 = 0.743125;
	const float C4 = 0.886227;
	const float C5 = 0.247708;

	const vec3 L00 = vec3(sg_data[2][2], sg_data[2][3], sg_data[3][0]);
	const vec3 L1m1 = vec3(sg_data[3][1], sg_data[3][2], sg_data[3][3]);
	const vec3 L10 = vec3(sg_data[4][0], sg_data[4][1], sg_data[4][2]);
	const vec3 L11 = vec3(sg_data[4][3], sg_data[5][0], sg_data[5][1]);
	const vec3 L2m2 = vec3(sg_data[5][2], sg_data[5][3], sg_data[6][0]);
	const vec3 L2m1 = vec3(sg_data[6][1], sg_data[6][2], sg_data[6][3]);
	const vec3 L20 = vec3(sg_data[7][0], sg_data[7][1], sg_data[7][2]);
	const vec3 L21 = vec3(sg_data[7][3], sg_data[8][0], sg_data[8][1]);
	const vec3 L22 = vec3(sg_data[8][2], sg_data[8][3], sg_data[9][0]);

	return (C1 * L22 * (n.x * n.x - n.y * n.y) + C3 * L20 * n.z * n.z + C4 * L00 - C5 * L20 +
	        2.0 * C1 * L2m2 * n.x * n.y + 2.0 * C1 * L21 * n.x * n.z + 2.0 * C1 * L2m1 * n.y * n.z +
	        2.0 * C2 * L11 * n.x + 2.0 * C2 * L1m1 * n.y + 2.0 * C2 * L10 * n.z) *
	       sg_data[9][1];
}

vec4 render() {
	vec2 frag_coord = uv * i_resolution;

	// camera
	vec3 ta = vec3(sg_data[1][0], sg_data[1][1], sg_data[1][2]);
	vec3 ro = vec3(sg_data[0][1], sg_data[0][2], sg_data[0][3]);
	mat3 ca = set_camera(ro, ta, 0.0);

	vec3 tot = vec3(0.0);
	int hits = 0;
	for (int m = 0; m < 2; m++)
		for (int n = 0; n < 2; n++) {
			// pixel coordinates
			vec2 o = vec2(float(m), float(n)) / 2.0f - 0.5;
			vec2 frag_pos = (2.0 * (frag_coord + o) - i_resolution.xy) / i_resolution.y;
			vec2 m = (2.0 * (sg_mouse + o) - i_resolution.xy) / i_resolution.y;

			// focal length
			const float fl = sg_data[0][0];

			// ray direction
			vec3 rd = ca * normalize(vec3(frag_pos, fl));

			// render
			vec4 rm = raymarch(ro, rd);
			vec3 ambient = vec3(0.05);
			vec3 specular = vec3(1.0);
			vec3 col = vec3(sg_data[1][3], sg_data[2][0], sg_data[2][1]);

			vec3 l_dir = normalize(vec3(sg_data[10][2], sg_data[10][3], sg_data[11][0]));

			if (rm.a < MAX_DIST) {
				vec3 p = ro + rd * rm.a;
				vec3 n = get_normal(p);
				float occ = calc_ao(p, n);

				float intensity = max(dot(n, l_dir), 0.0);
				vec3 spec = vec3(0.0);

				if (intensity > 0.0) {
					vec3 h = normalize(l_dir - rd);
					float int_spec = max(dot(h, n), 0.0);
					spec = specular * pow(int_spec, shininess);
				}
				rm.rgb += spherical_harmonics(n);
				col = max(intensity * rm.rgb + spec, ambient * rm.rgb) * occ;
				hits++;
				float hl = (0.05 - clamp(length(frag_pos - m), 0.0, 0.05)) / 0.05;
				col.r += hl;
				col.gb = mix(col.gb, vec2(0.0), hl);
			}

			// gamma
			col = pow(col, vec3(0.4545));

			tot += col;
		}
	tot /= 4.0f;
	float alpha = hits > 0 ? sg_alpha.y : sg_alpha.x;
	return vec4(tot, alpha);
}
