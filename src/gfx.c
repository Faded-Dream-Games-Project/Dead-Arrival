// ============================================================================
//  DEAD ARRIVAL - gfx.c
//  Rendering: world shader (lambert + exp2 fog + muzzle-flash point light),
//  per-texture merged static meshes, screen-space sky gradient.
// ============================================================================
#include "game.h"

// ---------------------------------------------------------------------------
// World shader: standard raylib locations + custom uniforms.
// Vertex: pass world position + world normal.
// Fragment: half-lambert directional light + ambient + flash point light
//           + exponential squared distance fog.
// ---------------------------------------------------------------------------
static const char *WORLD_VS =
"#version 330\n"
"in vec3 vertexPosition;\n"
"in vec3 vertexNormal;\n"
"in vec2 vertexTexCoord;\n"
"uniform mat4 mvp;\n"
"uniform mat4 matModel;\n"
"out vec3 wPos;\n"
"out vec3 wNor;\n"
"out vec2 vUv;\n"
"void main() {\n"
"    vec4 wp = matModel*vec4(vertexPosition,1.0);\n"
"    wPos = wp.xyz;\n"
"    wNor = normalize(mat3(matModel)*vertexNormal);\n"
"    vUv = vertexTexCoord;\n"
"    gl_Position = mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char *WORLD_FS =
"#version 330\n"
"in vec3 wPos;\n"
"in vec3 wNor;\n"
"in vec2 vUv;\n"
"uniform sampler2D texture0;\n"
"uniform vec3 colDiffuse;\n"
"uniform vec3 sunDir;\n"
"uniform vec3 sunColor;\n"
"uniform vec3 ambient;\n"
"uniform vec3 viewPos;\n"
"uniform vec3 fogColor;\n"
"uniform float fogDensity;\n"
"uniform vec3 flashPos;\n"
"uniform float flashStrength;\n"
"out vec4 finalColor;\n"
"void main() {\n"
"    vec4 texel = texture(texture0, vUv);\n"
"    vec3 n = normalize(wNor);\n"
"    float nl = dot(n, -sunDir)*0.5+0.5;   // half-lambert\n"
"    nl *= nl;\n"
"    vec3 lit = colDiffuse*texel.rgb*(ambient + sunColor*nl);\n"
"    // muzzle flash point light (quadratic falloff)\n"
"    if (flashStrength > 0.001) {\n"
"        vec3 fd = flashPos - wPos;\n"
"        float d = length(fd);\n"
"        float att = flashStrength/(1.0+0.35*d+0.09*d*d);\n"
"        lit += colDiffuse*texel.rgb*vec3(1.0,0.85,0.55)*att*max(dot(n,normalize(fd)),0.0);\n"
"    }\n"
"    float dist = length(viewPos - wPos);\n"
"    float fog = 1.0 - exp(-fogDensity*fogDensity*dist*dist);\n"
"    vec3 col = mix(lit, fogColor, clamp(fog,0.0,1.0));\n"
"    finalColor = vec4(col, 1.0);\n"
"}\n";

static Shader sWorldShader;
static Mesh   sCubeMesh;            // unit cube centered at origin ([-0.5,0.5])
static Material sCubeMat;           // tinted material bound to world shader

static struct {
    Vector3 flashPos;
    float   flashStrength;
    Vector3 sunDir, sunColor, ambient, fogColor;
    float   fogDensity;
} sScene;

// mesh builder ---------------------------------------------------------------
typedef struct MB {
    float *pos, *nor, *uv;
    int vCount, vCap;
    unsigned short *idx;
    int iCount, iCap;
} MB;

static MB *sBuilders[TEX_COUNT];
static Mesh sStaticMesh[TEX_COUNT];
static bool sStaticValid[TEX_COUNT];

// ---------------------------------------------------------------------------
static void MB_Ensure(MB *m, int moreV, int moreI) {
    if (m->vCount + moreV > m->vCap) {
        m->vCap = (m->vCount + moreV) * 2;
        m->pos = realloc(m->pos, m->vCap * 3 * sizeof(float));
        m->nor = realloc(m->nor, m->vCap * 3 * sizeof(float));
        m->uv  = realloc(m->uv,  m->vCap * 2 * sizeof(float));
    }
    if (m->iCount + moreI > m->iCap) {
        m->iCap = (m->iCount + moreI) * 2;
        m->idx = realloc(m->idx, m->iCap * sizeof(unsigned short));
    }
}

// append one axis-aligned quad; axis: 0=x 1=y 2=z, dir=+1/-1
static void MB_Quad(MB *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                    Vector3 n, float uvScale, int uAxis, int vAxis) {
    MB_Ensure(m, 4, 6);
    Vector3 p[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++) {
        m->pos[(m->vCount+i)*3+0] = p[i].x; m->pos[(m->vCount+i)*3+1] = p[i].y; m->pos[(m->vCount+i)*3+2] = p[i].z;
        m->nor[(m->vCount+i)*3+0] = n.x;    m->nor[(m->vCount+i)*3+1] = n.y;    m->nor[(m->vCount+i)*3+2] = n.z;
        float u = (uAxis==0?p[i].x:uAxis==1?p[i].y:p[i].z)/uvScale;
        float v = (vAxis==0?p[i].x:vAxis==1?p[i].y:p[i].z)/uvScale;
        m->uv[(m->vCount+i)*2+0] = u; m->uv[(m->vCount+i)*2+1] = v;
    }
    unsigned short base = (unsigned short)m->vCount;
    m->idx[m->iCount+0] = base+0; m->idx[m->iCount+1] = base+1; m->idx[m->iCount+2] = base+2;
    m->idx[m->iCount+3] = base+0; m->idx[m->iCount+4] = base+2; m->idx[m->iCount+5] = base+3;
    m->vCount += 4; m->iCount += 6;
}

// Append an AABB box with world-scaled UVs (per-face axis projection).
void Gfx_MB_Box(int tex, AABB box, float uvScale) {
    if (tex < 0 || tex >= TEX_COUNT) return;
    if (!sBuilders[tex]) { sBuilders[tex] = calloc(1, sizeof(MB)); }
    MB *m = sBuilders[tex];
    Vector3 mn = box.min, mx = box.max;
    // +Y top
    MB_Quad(m, Vec3(mn.x,mx.y,mn.z), Vec3(mx.x,mx.y,mn.z), Vec3(mx.x,mx.y,mx.z), Vec3(mn.x,mx.y,mx.z),
           Vec3(0,1,0), uvScale, 0, 2);
    // -Y bottom
    MB_Quad(m, Vec3(mn.x,mn.y,mx.z), Vec3(mx.x,mn.y,mx.z), Vec3(mx.x,mn.y,mn.z), Vec3(mn.x,mn.y,mn.z),
           Vec3(0,-1,0), uvScale, 0, 2);
    // +X
    MB_Quad(m, Vec3(mx.x,mn.y,mn.z), Vec3(mx.x,mn.y,mx.z), Vec3(mx.x,mx.y,mx.z), Vec3(mx.x,mx.y,mn.z),
           Vec3(1,0,0), uvScale, 2, 1);
    // -X
    MB_Quad(m, Vec3(mn.x,mn.y,mx.z), Vec3(mn.x,mn.y,mn.z), Vec3(mn.x,mx.y,mn.z), Vec3(mn.x,mx.y,mx.z),
           Vec3(-1,0,0), uvScale, 2, 1);
    // +Z
    MB_Quad(m, Vec3(mn.x,mn.y,mx.z), Vec3(mx.x,mn.y,mx.z), Vec3(mx.x,mx.y,mx.z), Vec3(mn.x,mx.y,mx.z),
           Vec3(0,0,1), uvScale, 0, 1);
    // -Z
    MB_Quad(m, Vec3(mx.x,mn.y,mn.z), Vec3(mn.x,mn.y,mn.z), Vec3(mn.x,mx.y,mn.z), Vec3(mx.x,mx.y,mn.z),
           Vec3(0,0,-1), uvScale, 0, 1);
}

void Gfx_MB_Reset(void) {
    for (int t = 0; t < TEX_COUNT; t++) {
        if (!sBuilders[t]) continue;
        sBuilders[t]->vCount = 0;
        sBuilders[t]->iCount = 0;
    }
}


void Gfx_MB_Build(void) {
    for (int t = 0; t < TEX_COUNT; t++) {
        if (sStaticValid[t]) { UnloadMesh(sStaticMesh[t]); sStaticValid[t] = false; }
        MB *m = sBuilders[t];
        if (!m || m->iCount == 0) continue;
        Mesh mesh = { 0 };
        mesh.vertexCount = m->vCount;
        mesh.triangleCount = m->iCount / 3;
        mesh.vertices  = malloc(m->vCount*3*sizeof(float)); memcpy(mesh.vertices,  m->pos, m->vCount*3*sizeof(float));
        mesh.normals   = malloc(m->vCount*3*sizeof(float)); memcpy(mesh.normals,   m->nor, m->vCount*3*sizeof(float));
        mesh.texcoords = malloc(m->vCount*2*sizeof(float)); memcpy(mesh.texcoords, m->uv,  m->vCount*2*sizeof(float));
        mesh.indices   = malloc(m->iCount*sizeof(unsigned short)); memcpy(mesh.indices, m->idx, m->iCount*sizeof(unsigned short));
        UploadMesh(&mesh, false);
        sStaticMesh[t] = mesh;
        sStaticValid[t] = true;
    }
}

// ---------------------------------------------------------------------------
void GfxInit(void) {
    sWorldShader = LoadShaderFromMemory(WORLD_VS, WORLD_FS);
    // bind raylib standard locations so DrawMesh feeds mvp/model automatically
    sWorldShader.locs[SHADER_LOC_MATRIX_MODEL]      = GetShaderLocation(sWorldShader, "matModel");
    sWorldShader.locs[SHADER_LOC_MATRIX_VIEW]       = GetShaderLocation(sWorldShader, "view"); // not used; rlgl sets if != -1
    sWorldShader.locs[SHADER_LOC_MATRIX_PROJECTION] = GetShaderLocation(sWorldShader, "projection");
    sWorldShader.locs[SHADER_LOC_COLOR_DIFFUSE]     = GetShaderLocation(sWorldShader, "colDiffuse");
    sWorldShader.locs[SHADER_LOC_MAP_DIFFUSE]       = GetShaderLocation(sWorldShader, "texture0");

    sCubeMesh = GenMeshCube(1.0f, 1.0f, 1.0f);
    sCubeMat = LoadMaterialDefault();
    sCubeMat.shader = sWorldShader;
    GfxInitMaterials();

    memset(&sScene, 0, sizeof(sScene));
    sScene.flashStrength = 0.0f;
}

void GfxShutdown(void) {
    Gfx_MB_Reset();
    Gfx_MB_Build(); // frees built meshes
    for (int t = 0; t < TEX_COUNT; t++) if (sBuilders[t]) { free(sBuilders[t]->pos); free(sBuilders[t]->nor); free(sBuilders[t]->uv); free(sBuilders[t]->idx); free(sBuilders[t]); sBuilders[t] = NULL; }
    UnloadMesh(sCubeMesh);
    UnloadShader(sWorldShader);
}

Shader* GfxWorldShader(void) { return &sWorldShader; }

void GfxBegin(Camera3D cam) {
    BeginMode3D(cam);
    // push scene uniforms once per frame
    int loc;
    Vector3 viewPos = cam.position;
    loc = GetShaderLocation(sWorldShader, "viewPos");     SetShaderValue(sWorldShader, loc, &viewPos, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "sunDir");      SetShaderValue(sWorldShader, loc, &sScene.sunDir, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "sunColor");    SetShaderValue(sWorldShader, loc, &sScene.sunColor, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "ambient");     SetShaderValue(sWorldShader, loc, &sScene.ambient, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "fogColor");    SetShaderValue(sWorldShader, loc, &sScene.fogColor, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "fogDensity");  SetShaderValue(sWorldShader, loc, &sScene.fogDensity, SHADER_UNIFORM_FLOAT);
    loc = GetShaderLocation(sWorldShader, "flashPos");    SetShaderValue(sWorldShader, loc, &sScene.flashPos, SHADER_UNIFORM_VEC3);
    loc = GetShaderLocation(sWorldShader, "flashStrength");SetShaderValue(sWorldShader, loc, &sScene.flashStrength, SHADER_UNIFORM_FLOAT);
}

void GfxSetScene(Vector3 sunDir, Color sunColor, Color ambient, Color fogColor, float fogDensity) {
    sScene.sunDir = sunDir;
    sScene.sunColor = Vec3(sunColor.r/255.0f, sunColor.g/255.0f, sunColor.b/255.0f);
    sScene.ambient = Vec3(ambient.r/255.0f, ambient.g/255.0f, ambient.b/255.0f);
    sScene.fogColor = Vec3(fogColor.r/255.0f, fogColor.g/255.0f, fogColor.b/255.0f);
    sScene.fogDensity = fogDensity;
}

void GfxSetFlash(Vector3 pos, float strength) {
    sScene.flashPos = pos;
    sScene.flashStrength = strength;
}

void GfxEnd(void) { EndMode3D(); }

// Persistent scratch material. IMPORTANT: never UnloadMaterial() this - the
// shader belongs to Gfx and raylib would unload it with the material.
static Material sScratchMat;

void GfxInitMaterials(void) { sScratchMat = LoadMaterialDefault(); }

// Draw all static (merged) world geometry.
void GfxDrawStatic(void) {
    sScratchMat.shader = sWorldShader;
    for (int t = 0; t < TEX_COUNT; t++) {
        if (!sStaticValid[t]) continue;
        sScratchMat.maps[MATERIAL_MAP_DIFFUSE].texture = AssetsTex(t);
        sScratchMat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(sStaticMesh[t], sScratchMat, MatrixIdentity());
    }
}

// Draw a single textured AABB (doors, dynamic boxes).
void GfxDrawBox(AABB aabb, int tex, Color tint) {
    sScratchMat.shader = sWorldShader;
    sScratchMat.maps[MATERIAL_MAP_DIFFUSE].texture = AssetsTex(tex);
    sScratchMat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    Matrix m = MatrixIdentity();
    m.m0 = aabb.max.x - aabb.min.x; m.m5 = aabb.max.y - aabb.min.y; m.m10 = aabb.max.z - aabb.min.z;
    m.m12 = (aabb.min.x+aabb.max.x)/2; m.m13 = (aabb.min.y+aabb.max.y)/2; m.m14 = (aabb.min.z+aabb.max.z)/2;
    DrawMesh(sCubeMesh, sScratchMat, m);
}

// Generic transformed tinted cube for models (zombies, guns, props).
void GfxDrawCubePart(Vector3 pos, Vector3 size, Vector3 rotDeg, Color tint, Texture2D tex) {
    sScratchMat.shader = sWorldShader;
    sScratchMat.maps[MATERIAL_MAP_DIFFUSE].texture = tex.id ? tex : AssetsTex(TEX_METAL);
    sScratchMat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    Matrix m = MatrixIdentity();
    m = MatrixMultiply(m, MatrixTranslate(pos.x, pos.y, pos.z));
    if (rotDeg.x || rotDeg.y || rotDeg.z)
        m = MatrixMultiply(MatrixRotateXYZ(Vec3(rotDeg.x*DEG2RAD, rotDeg.y*DEG2RAD, rotDeg.z*DEG2RAD)), m);
    m = MatrixMultiply(MatrixScale(size.x, size.y, size.z), m);
    DrawMesh(sCubeMesh, sScratchMat, m);
}

// Screen-space sky: vertical gradient with horizon placed from camera pitch.
void GfxDrawSky(Camera3D cam, float playerPitchDeg) {
    int w = GetScreenWidth(), h = GetScreenHeight();
    float halfFovTan = tanf(cam.fovy*DEG2RAD*0.5f);
    float horizon = h*0.5f + (playerPitchDeg/halfFovTan)*(h*0.5f);
    horizon = CLAMP(horizon, -h*0.5f, h*1.5f);
    Color top = g.map.skyTop, hor = g.map.skyHorizon;
    // sky above horizon
    if (horizon > 0) {
        int skyH = (int)CLAMP(horizon, 0, h);
        DrawRectangleGradientV(0, 0, w, skyH, top, hor);
    }
    // below horizon: fog-colored void (unbuilt ground far away)
    if (horizon < h) {
        int gy = (int)CLAMP(horizon, 0, h);
        DrawRectangleGradientV(0, gy, w, h-gy, hor, g.map.fogColor);
    }
}
