#include <SDL3/SDL.h>
#include <geGL/geGL.h>
#include <geGL/StaticCalls.h>
#include <cmath>
#include <map>
#include <cstddef>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <vector>
#include <string>

using namespace ge::gl;

struct MeshPart
{
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  size_t indexCount;
};

struct LogicalObject
{
  std::string name;
  std::vector<MeshPart> parts;
};

std::vector<LogicalObject> carObjects;

MeshPart loadMesh(aiMesh *mesh)
{
  std::vector<float> vertices;
  std::vector<unsigned int> indices;

  for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
  {
    vertices.push_back(mesh->mVertices[v].x);
    vertices.push_back(mesh->mVertices[v].y);
    vertices.push_back(mesh->mVertices[v].z);

    vertices.push_back(mesh->mNormals[v].x);
    vertices.push_back(mesh->mNormals[v].y);
    vertices.push_back(mesh->mNormals[v].z);
  }

  for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
  {
    aiFace face = mesh->mFaces[f];
    for (unsigned int idx = 0; idx < face.mNumIndices; ++idx)
      indices.push_back(face.mIndices[idx]);
  }

  MeshPart part;

  glGenVertexArrays(1, &part.vao);
  glBindVertexArray(part.vao);

  glGenBuffers(1, &part.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

  glGenBuffers(1, &part.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);

  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));

  glBindVertexArray(0);

  part.indexCount = indices.size();

  return part;
}

void processNode(aiNode *node, const aiScene *scene)
{
  LogicalObject obj;
  obj.name = node->mName.C_Str();

  for (unsigned int i = 0; i < node->mNumMeshes; ++i)
  {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    MeshPart part = loadMesh(mesh);
    obj.parts.push_back(part);
  }

  if (!obj.parts.empty())
  {
    carObjects.push_back(obj);
    std::cout << "Caricato oggetto: " << obj.name << " con " << obj.parts.size() << " mesh\n";
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i)
  {
    processNode(node->mChildren[i], scene);
  }
}

bool loadCarModel(const std::string &path)
{
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(path,
                                           aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);

  if (!scene || !scene->mRootNode)
  {
    std::cerr << "Errore caricamento modello: " << importer.GetErrorString() << std::endl;
    return false;
  }

  carObjects.clear();
  processNode(scene->mRootNode, scene);

  return true;
}

GLuint createShader(GLenum type, std::string const &src)
{
  auto vs = glCreateShader(type);
  char const *srcs[] = {
      src.c_str(),
  };
  glShaderSource(vs, 1, srcs, 0);
  glCompileShader(vs);

  GLint status;
  glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE)
  {
    char buf[10000];
    glGetShaderInfoLog(vs, 10000, 0, buf);
    std::cerr << "ERROR: " << buf << std::endl;
  }

  return vs;
}

GLuint createProgram(std::vector<GLuint> const &shaders)
{
  auto prg = glCreateProgram();
  for (auto const &x : shaders)
    glAttachShader(prg, x);
  glLinkProgram(prg);

  GLint status;
  glGetProgramiv(prg, GL_LINK_STATUS, &status);
  if (status != GL_TRUE)
  {
    char buf[10000];
    glGetProgramInfoLog(prg, 10000, 0, buf);
    std::cerr << "ERROR: " << buf << std::endl;
  }

  return prg;
}

void matrixMultiplication(float *O, float *A, float *B)
{
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
    {
      O[c * 4 + r] = 0;
      for (int i = 0; i < 4; ++i)
        O[c * 4 + r] += A[i * 4 + r] * B[c * 4 + i];
    }
}

void matrixIdentity(float *O)
{
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      O[c * 4 + r] = (float)(c == r);
}

void rotateX(float *O, float angle)
{
  matrixIdentity(O);
  auto cosa = cos(angle);
  auto sina = sin(angle);
  O[5] = +cosa;
  O[6] = +sina;
  O[9] = -sina;
  O[10] = +cosa;
}

void rotateY(float *O, float angle)
{
  matrixIdentity(O);
  auto cosa = cos(angle);
  auto sina = sin(angle);
  O[0] = +cosa;
  O[2] = +sina;
  O[8] = -sina;
  O[10] = +cosa;
}

void translate(float *O, float x, float y, float z)
{
  matrixIdentity(O);
  O[12] = x;
  O[13] = y;
  O[14] = z;
}

void frustum(float *O, float L, float R, float B, float T, float n, float f)
{
  matrixIdentity(O);
  O[0] = 2 * n / (R - L);
  O[5] = 2 * n / (T - B);
  O[8] = (R + L) / (R - L);
  O[9] = (T + B) / (T - B);
  O[10] = -(f + n) / (f - n);
  O[11] = -1;
  O[14] = -2 * n * f / (f - n);
  O[15] = 0;
}

void perspective(float *O, float fovy, float aspect, float n, float f)
{
  float R = n * tan(fovy / 2);
  float L = -R;
  float T = R / aspect;
  float B = -T;
  frustum(O, L, R, B, T, n, f);
}

int main(int argc, char *argv[])
{
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

  auto window = SDL_CreateWindow("PGR2025", 1024, 768, SDL_WINDOW_OPENGL);
  auto context = SDL_GL_CreateContext(window);

  ge::gl::init();

  if (!loadCarModel("../model/importMustang.obj"))
  {
    std::cerr << "Impossibile caricare il modello della macchina" << std::endl;
    return 1;
  }

  auto vsSrc = R".(
  #version 410
  layout(location=0) in vec3 position;
  layout(location=1) in vec3 normal;
  out vec3 vNormal;
  uniform mat4 viewMatrix = mat4(1);
  uniform mat4 projMatrix = mat4(1);
  uniform mat4 modelMatrix = mat4(1);
  void main(){
    mat4 mvp = projMatrix * viewMatrix * modelMatrix;
    gl_Position = mvp * vec4(position, 1);
    vNormal = mat3(viewMatrix * modelMatrix) * normal;
  }
  ).";

  auto fsSrc = R".(
  #version 410
  in vec3 vNormal;
  out vec4 fColor;
  void main(){
    vec3 color = normalize(vNormal) * 0.5 + 0.5;
    fColor = vec4(color, 1);
  }
  ).";

  auto vs = createShader(GL_VERTEX_SHADER, vsSrc);
  auto fs = createShader(GL_FRAGMENT_SHADER, fsSrc);
  auto prg = createProgram({vs, fs});

  auto viewMatrixL = glGetUniformLocation(prg, "viewMatrix");
  auto projMatrixL = glGetUniformLocation(prg, "projMatrix");
  auto modelMatrixL = glGetUniformLocation(prg, "modelMatrix");

  float cameraPosition[3] = {0, 0.5, 2};
  float angleX = 0.3f;
  float angleY = 0.f;

  float viewMatrix[16];
  float VT[16];
  float VR[16];
  float VRX[16];
  float VRY[16];
  float projMatrix[16];
  matrixIdentity(projMatrix);

  float sensitivity = 0.01;
  float cameraSpeed = 0.01;

  std::map<int, bool> keys;

  glEnable(GL_DEPTH_TEST);
  bool running = true;
  while (running)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
      if (event.type == SDL_EVENT_KEY_UP)
        keys[event.key.key] = false;
      if (event.type == SDL_EVENT_KEY_DOWN)
        keys[event.key.key] = true;
      if (event.type == SDL_EVENT_MOUSE_MOTION)
      {
        if (event.motion.state & SDL_BUTTON_LEFT)
        {
          angleY -= event.motion.xrel * sensitivity;
          angleX += event.motion.yrel * sensitivity;
        }
      }
    }

    float leftRight = ((int)(keys[SDLK_D]) - (int)keys[SDLK_A]) * cameraSpeed;
    float forwardBackward = ((int)(keys[SDLK_S]) - (int)keys[SDLK_W]) * cameraSpeed;
    float upDown = ((int)(keys[SDLK_SPACE]) - (int)keys[SDLK_LSHIFT]) * cameraSpeed;
    cameraPosition[0] += VR[0] * leftRight;
    cameraPosition[1] += VR[4] * leftRight;
    cameraPosition[2] += VR[8] * leftRight;
    cameraPosition[0] += VR[2] * forwardBackward;
    cameraPosition[1] += VR[6] * forwardBackward;
    cameraPosition[2] += VR[10] * forwardBackward;
    cameraPosition[0] += VR[1] * upDown;
    cameraPosition[1] += VR[5] * upDown;
    cameraPosition[2] += VR[9] * upDown;

    translate(VT, -cameraPosition[0], -cameraPosition[1], -cameraPosition[2]);
    rotateX(VRX, angleX);
    rotateY(VRY, angleY);

    matrixMultiplication(VR, VRX, VRY);
    matrixMultiplication(viewMatrix, VR, VT);

    perspective(projMatrix, 90. / 180. * 3.1415925, 1024. / 768., 0.1, 1000.f);

    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPointSize(10);
    glUseProgram(prg);
    glProgramUniformMatrix4fv(prg, viewMatrixL, 1, GL_FALSE, viewMatrix);
    glProgramUniformMatrix4fv(prg, projMatrixL, 1, GL_FALSE, projMatrix);

    for (auto &obj : carObjects)
    {
      float modelMatrix[16];
      matrixIdentity(modelMatrix);

      // Applica offset basato sul nome dell'oggetto
      float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;

      if (obj.name.find("door_R") != std::string::npos)
      {
        offsetX = 20.0f;
      }
      else if (obj.name.find("door_L") != std::string::npos)
      {
        offsetX = -20.0f;
      }
      else if (obj.name.find("hood") != std::string::npos)
      {
        offsetY = 20.0f;
      }

      translate(modelMatrix, offsetX, offsetY, offsetZ);
      glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, modelMatrix);

      for (auto &part : obj.parts)
      {
        glBindVertexArray(part.vao);
        glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  return 0;
}
