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
#include <algorithm>
#include <stb_image.h>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <BasicCamera/OrbitCamera.h>
#include <BasicCamera/PerspectiveCamera.h>

using namespace ge::gl;

const unsigned int SHADOW_WIDTH = 4096;
const unsigned int SHADOW_HEIGHT = 4096;

std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c)
                 { return std::tolower(c); });
  return s;
}

struct MeshPart
{
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  GLuint textureID;
  size_t indexCount;

  float diffuseColor[4]; // RGBA
  float specularStrength;
};

struct LogicalObject
{
  std::string name;
  std::vector<MeshPart> parts;
  glm::mat4 transform;

  aiNodeAnim *animChannel = nullptr;
  float currentAnimTime = 0.0f;
  float animDuration = 0.0f;
  bool isOpen = false;
  float minTime = 0.0f;
  float maxTime = 0.0f;
};

//  Skybox Verctices
float skyboxVertices[] = {
    -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
    -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f,
    -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
    1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
    -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f,
    1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};

//  Shadow Catcher Vertices (Floor)
float catcherVertices[] = {
    // Pos                // Normal          // UV
    -15.0f, 0.0f, -15.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    15.0f, 0.0f, -15.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
    15.0f, 0.0f, 15.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    15.0f, 0.0f, 15.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
    -15.0f, 0.0f, 15.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    -15.0f, 0.0f, -15.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

std::vector<LogicalObject> carObjects;
Assimp::Importer importer;
const aiScene *g_scene = nullptr;

MeshPart loadMesh(aiMesh *mesh)
{
  std::vector<float> vertices;
  std::vector<unsigned int> indices;

  for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
  {
    vertices.push_back(mesh->mVertices[v].x);
    vertices.push_back(mesh->mVertices[v].y);
    vertices.push_back(mesh->mVertices[v].z);

    if (mesh->HasNormals())
    {
      vertices.push_back(mesh->mNormals[v].x);
      vertices.push_back(mesh->mNormals[v].y);
      vertices.push_back(mesh->mNormals[v].z);
    }
    else
    {
      vertices.push_back(0.0f);
      vertices.push_back(0.0f);
      vertices.push_back(0.0f);
    }

    if (mesh->mTextureCoords[0])
    {
      vertices.push_back(mesh->mTextureCoords[0][v].x);
      vertices.push_back(mesh->mTextureCoords[0][v].y);
    }
    else
    {
      vertices.push_back(0.0f);
      vertices.push_back(0.0f);
    }
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

  int stride = 8 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

  glBindVertexArray(0);
  part.indexCount = indices.size();
  return part;
}

GLuint loadCubemap(std::vector<std::string> faces)
{
  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

  int width, height, nrChannels;
  for (unsigned int i = 0; i < faces.size(); i++)
  {
    unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    }
    else
    {
      std::cerr << "Cubemap texture failed: " << faces[i] << std::endl;
      stbi_image_free(data);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  return textureID;
}

GLuint loadTexture(const aiMaterial *material, const aiScene *scene)
{
  aiString path;
  if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS)
  {
    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS)
      return 0;
  }

  const aiTexture *embeddedTex = scene->GetEmbeddedTexture(path.C_Str());
  unsigned char *data = nullptr;
  int width, height, channels;

  if (embeddedTex)
  {
    if (embeddedTex->mHeight == 0)
      data = stbi_load_from_memory(reinterpret_cast<unsigned char *>(embeddedTex->pcData), embeddedTex->mWidth, &width, &height, &channels, 4);
    else
      data = stbi_load_from_memory(reinterpret_cast<unsigned char *>(embeddedTex->pcData), embeddedTex->mWidth * embeddedTex->mHeight * 4, &width, &height, &channels, 4);
  }
  else
  {
    data = stbi_load(path.C_Str(), &width, &height, &channels, 4);
  }

  if (!data)
    return 0;

  GLuint texID;
  glGenTextures(1, &texID);
  glBindTexture(GL_TEXTURE_2D, texID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glGenerateMipmap(GL_TEXTURE_2D);
  stbi_image_free(data);
  return texID;
}

void processNode(aiNode *node, const aiScene *scene)
{
  LogicalObject obj;
  obj.name = node->mName.C_Str();
  aiMatrix4x4 m = node->mTransformation;

  obj.transform = glm::mat4(
      m.a1, m.b1, m.c1, m.d1,
      m.a2, m.b2, m.c2, m.d2,
      m.a3, m.b3, m.c3, m.d3,
      m.a4, m.b4, m.c4, m.d4);

  for (unsigned int i = 0; i < node->mNumMeshes; ++i)
  {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    MeshPart part = loadMesh(mesh);

    aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
    part.textureID = loadTexture(material, scene);

    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
      material->Get(AI_MATKEY_COLOR_DIFFUSE, color);

    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 1.0f)
      color.a = opacity;

    part.diffuseColor[0] = color.r;
    part.diffuseColor[1] = color.g;
    part.diffuseColor[2] = color.b;
    part.diffuseColor[3] = color.a;

    float roughness = 0.5f, metallic = 0.0f;
    material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    part.specularStrength = std::min((1.0f - roughness) + metallic, 1.0f);

    obj.parts.push_back(part);
  }

  if (!obj.parts.empty())
  {
    if (g_scene && g_scene->HasAnimations())
    {
      bool found = false;
      std::string objNameLower = toLower(obj.name);

      for (unsigned int a = 0; a < g_scene->mNumAnimations; a++)
      {
        aiAnimation *anim = g_scene->mAnimations[a];
        for (unsigned int c = 0; c < anim->mNumChannels; c++)
        {
          std::string chanName = anim->mChannels[c]->mNodeName.C_Str();
          std::string chanNameLower = toLower(chanName);
          if (chanNameLower.find(objNameLower) != std::string::npos ||
              objNameLower.find(chanNameLower) != std::string::npos)
          {
            obj.animChannel = anim->mChannels[c];
            obj.animDuration = (float)anim->mDuration;
            obj.currentAnimTime = 0.0f;
            obj.isOpen = false;
            found = true;
            std::cout << "LINK OK: '" << obj.name << "' <-> Channel '" << chanName << "'" << std::endl;
            break;
          }
        }
        if (found)
          break;
      }
    }
    carObjects.push_back(obj);
    std::cout << "Object loaded: " << obj.name << "\n";
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    processNode(node->mChildren[i], scene);
}

// Interpolation functions
unsigned int findPosition(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumPositionKeys - 1; i++)
    if (animationTime < (float)pNodeAnim->mPositionKeys[i + 1].mTime)
      return i;
  return 0;
}

unsigned int findRotation(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumRotationKeys - 1; i++)
    if (animationTime < (float)pNodeAnim->mRotationKeys[i + 1].mTime)
      return i;
  return 0;
}

unsigned int findScaling(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumScalingKeys - 1; i++)
    if (animationTime < (float)pNodeAnim->mScalingKeys[i + 1].mTime)
      return i;
  return 0;
}

glm::vec3 calcInterpolatedPosition(float animationTime, const aiNodeAnim *pNodeAnim)
{
  if (pNodeAnim->mNumPositionKeys == 1)
  {
    auto v = pNodeAnim->mPositionKeys[0].mValue;
    return glm::vec3(v.x, v.y, v.z);
  }
  unsigned int idx = findPosition(animationTime, pNodeAnim);
  unsigned int nextIdx = idx + 1;
  float dt = (float)(pNodeAnim->mPositionKeys[nextIdx].mTime - pNodeAnim->mPositionKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mPositionKeys[idx].mTime) / dt;
  factor = glm::clamp(factor, 0.0f, 1.0f);
  const auto &s = pNodeAnim->mPositionKeys[idx].mValue;
  const auto &e = pNodeAnim->mPositionKeys[nextIdx].mValue;
  auto res = s + factor * (e - s);
  return glm::vec3(res.x, res.y, res.z);
}

glm::quat calcInterpolatedRotation(float animationTime, const aiNodeAnim *pNodeAnim)
{
  if (pNodeAnim->mNumRotationKeys == 1)
  {
    auto q = pNodeAnim->mRotationKeys[0].mValue;
    return glm::quat(q.w, q.x, q.y, q.z);
  }
  unsigned int idx = findRotation(animationTime, pNodeAnim);
  unsigned int nextIdx = idx + 1;
  float dt = (float)(pNodeAnim->mRotationKeys[nextIdx].mTime - pNodeAnim->mRotationKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mRotationKeys[idx].mTime) / dt;
  factor = glm::clamp(factor, 0.0f, 1.0f);
  const auto &sq = pNodeAnim->mRotationKeys[idx].mValue;
  const auto &eq = pNodeAnim->mRotationKeys[nextIdx].mValue;
  return glm::slerp(glm::quat(sq.w, sq.x, sq.y, sq.z), glm::quat(eq.w, eq.x, eq.y, eq.z), factor);
}

glm::vec3 calcInterpolatedScaling(float animationTime, const aiNodeAnim *pNodeAnim)
{
  if (pNodeAnim->mNumScalingKeys == 1)
  {
    auto v = pNodeAnim->mScalingKeys[0].mValue;
    return glm::vec3(v.x, v.y, v.z);
  }
  unsigned int idx = findScaling(animationTime, pNodeAnim);
  unsigned int nextIdx = idx + 1;
  float dt = (float)(pNodeAnim->mScalingKeys[nextIdx].mTime - pNodeAnim->mScalingKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mScalingKeys[idx].mTime) / dt;
  factor = glm::clamp(factor, 0.0f, 1.0f);
  const auto &s = pNodeAnim->mScalingKeys[idx].mValue;
  const auto &e = pNodeAnim->mScalingKeys[nextIdx].mValue;
  auto res = s + factor * (e - s);
  return glm::vec3(res.x, res.y, res.z);
}

void updateObjectAnimations(float deltaTime)
{
  float animSpeed = 1000.0f;
  for (auto &obj : carObjects)
  {
    if (!obj.animChannel)
      continue;
    if (obj.isOpen)
    {
      obj.currentAnimTime += deltaTime * animSpeed;
      if (obj.currentAnimTime > obj.maxTime)
        obj.currentAnimTime = obj.maxTime;
    }
    else
    {
      obj.currentAnimTime -= deltaTime * animSpeed;
      if (obj.currentAnimTime < obj.minTime)
        obj.currentAnimTime = obj.minTime;
    }
    glm::vec3 pos = calcInterpolatedPosition(obj.currentAnimTime, obj.animChannel);
    glm::quat rot = calcInterpolatedRotation(obj.currentAnimTime, obj.animChannel);
    glm::vec3 scale = calcInterpolatedScaling(obj.currentAnimTime, obj.animChannel);
    obj.transform = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), scale);
  }
}

bool loadCarModel(const std::string &path)
{
  g_scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);
  if (!g_scene || !g_scene->mRootNode)
  {
    std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
    return false;
  }
  std::cout << "--- DEBUG GLB ---" << g_scene->mNumAnimations << std::endl;
  carObjects.clear();
  processNode(g_scene->mRootNode, g_scene);
  return true;
}

GLuint createShader(GLenum type, std::string const &src)
{
  auto vs = glCreateShader(type);
  char const *srcs[] = {src.c_str()};
  glShaderSource(vs, 1, srcs, 0);
  glCompileShader(vs);
  GLint status;
  glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE)
  {
    char buf[10000];
    glGetShaderInfoLog(vs, 10000, 0, buf);
    std::cerr << "SHADER ERR: " << buf << std::endl;
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
    std::cerr << "LINK ERR: " << buf << std::endl;
  }
  return prg;
}

int main(int argc, char *argv[])
{
  if (!SDL_Init(SDL_INIT_VIDEO))
    return 1;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

  auto window = SDL_CreateWindow("PGR2025 - Mustang", 1024, 768, SDL_WINDOW_OPENGL);
  auto context = SDL_GL_CreateContext(window);

  ge::gl::init(reinterpret_cast<ge::gl::GET_PROC_ADDRESS>(SDL_GL_GetProcAddress));
  glEnable(GL_MULTISAMPLE);

  if (!loadCarModel("../model/mustang.glb"))
    return 1;

  for (auto &obj : carObjects)
  {
    std::string name = toLower(obj.name);
    if (name.find("door_l") != std::string::npos)
    {
      obj.minTime = 0.0f;
      obj.maxTime = 1666.0f;
      obj.currentAnimTime = obj.minTime;
    }
    if (name.find("door_r") != std::string::npos)
    {
      obj.minTime = 2083.0f;
      obj.maxTime = 3749.0f;
      obj.currentAnimTime = obj.minTime;
    }
    if (name.find("hood") != std::string::npos)
    {
      obj.minTime = 4166.0f;
      obj.maxTime = 5833.0f;
      obj.currentAnimTime = obj.minTime;
    }
  }

  // SHADOW MAP
  GLuint depthMapFBO;
  glGenFramebuffers(1, &depthMapFBO);
  GLuint depthMap;
  glGenTextures(1, &depthMap);
  glBindTexture(GL_TEXTURE_2D, depthMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
  glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // SHADERS

  // 1 Depth Shader
  auto depthVsSrc = R".(
  #version 410 core
  layout (location = 0) in vec3 position;
  uniform mat4 lightSpaceMatrix;
  uniform mat4 modelMatrix;
  void main() {
      gl_Position = lightSpaceMatrix * modelMatrix * vec4(position, 1.0);
  }
  ).";
  auto depthFsSrc = R".(
  #version 410 core
  void main() {
      // Empty fragment shader since we only need depth
  }
  ).";
  auto depthProg = createProgram({createShader(GL_VERTEX_SHADER, depthVsSrc), createShader(GL_FRAGMENT_SHADER, depthFsSrc)});
  auto d_lightSpaceMatrixL = glGetUniformLocation(depthProg, "lightSpaceMatrix");
  auto d_modelMatrixL = glGetUniformLocation(depthProg, "modelMatrix");

  // 2 Main Shader
  auto vsSrc = R".(
  #version 410
  layout(location=0) in vec3 position;
  layout(location=1) in vec3 normal;
  layout(location=2) in vec2 texCoord;

  out vec3 vNormal; out vec3 vPos; out vec2 vTexCoord;
  out vec4 vFragPosLightSpace; // Output per calcolo ombre

  uniform mat4 viewMatrix; uniform mat4 projMatrix; uniform mat4 modelMatrix;
  uniform mat4 lightSpaceMatrix; // Matrice della luce

  void main(){
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vPos = worldPos.xyz; 
    gl_Position = projMatrix * viewMatrix * worldPos;
    vNormal = mat3(transpose(inverse(modelMatrix))) * normal; 
    vTexCoord = texCoord;
    
    // Calcola posizione vista dalla luce
    vFragPosLightSpace = lightSpaceMatrix * worldPos;
  }
  ).";

  auto fsSrc = R".(
  #version 410
  in vec3 vNormal; in vec3 vPos; in vec2 vTexCoord;
  in vec4 vFragPosLightSpace; // Posizione nella shadow map

  out vec4 fColor;

  uniform sampler2D texSampler; 
  uniform samplerCube skybox;
  uniform sampler2D shadowMap; 

  uniform int hasTexture; 
  uniform vec4 materialColor; 
  uniform float specularStrength; 
  uniform vec3 viewPos;
  uniform int isShadowCatcher; 

  float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
      vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
      projCoords = projCoords * 0.5 + 0.5;
      
      if(projCoords.z > 1.0) return 0.0;

      float currentDepth = projCoords.z;
      
      float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001); 
      
      float shadow = 0.0;
      vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
      for(int x = -1; x <= 1; ++x) {
          for(int y = -1; y <= 1; ++y) {
              float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
              shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
          }    
      }
      shadow /= 9.0;
      return shadow;
  }

  void main(){
      vec3 lightPos = vec3(10.0, 20.0, 10.0);
      vec3 L = normalize(lightPos);
      vec3 N = normalize(vNormal);

      float shadow = ShadowCalculation(vFragPosLightSpace, N, L);

      if (isShadowCatcher == 1) {
          fColor = vec4(0.0, 0.0, 0.0, shadow * 0.7); 
          return;
      }

      vec4 baseColor = materialColor;
      if (hasTexture == 1) baseColor *= texture(texSampler, vTexCoord);
      
      vec3 V = normalize(viewPos - vPos); 
      vec3 R = reflect(-V, N);
      vec3 envColor = texture(skybox, R).rgb;
      
      vec3 ambient = 0.25 * baseColor.rgb;
      float diff = max(dot(N, L), 0.0);
      vec3 diffuse = diff * baseColor.rgb;
      
      vec3 lighting = ambient + (1.0 - shadow) * diffuse;
      
      vec3 finalColor;
      if (specularStrength > 0.95) { 
          finalColor = mix(lighting, envColor, 0.65); 
      } else {
          float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
          float reflectionAmount = 0.05 + (fresnel * 0.5); reflectionAmount *= specularStrength; 
          finalColor = mix(lighting, envColor, reflectionAmount);
      }
      
      fColor = vec4(finalColor, baseColor.a);
  }
  ).";

  // 3 Skybox Shader
  auto skyboxVsSrc = R".(
  #version 410
  layout (location = 0) in vec3 aPos;
  out vec3 TexCoords;
  uniform mat4 projection; uniform mat4 view;
  void main() { TexCoords = aPos; vec4 pos = projection * view * vec4(aPos, 1.0); gl_Position = pos.xyww; }
  ).";
  auto skyboxFsSrc = R".(
  #version 410
  out vec4 FragColor; in vec3 TexCoords; uniform samplerCube skybox;
  void main() { FragColor = texture(skybox, TexCoords); }
  ).";

  auto skyboxProgram = createProgram({createShader(GL_VERTEX_SHADER, skyboxVsSrc), createShader(GL_FRAGMENT_SHADER, skyboxFsSrc)});
  auto skyProjL = glGetUniformLocation(skyboxProgram, "projection");
  auto skyViewL = glGetUniformLocation(skyboxProgram, "view");
  auto skyTexL = glGetUniformLocation(skyboxProgram, "skybox");

  auto prg = createProgram({createShader(GL_VERTEX_SHADER, vsSrc), createShader(GL_FRAGMENT_SHADER, fsSrc)});
  auto viewMatrixL = glGetUniformLocation(prg, "viewMatrix");
  auto projMatrixL = glGetUniformLocation(prg, "projMatrix");
  auto modelMatrixL = glGetUniformLocation(prg, "modelMatrix");
  auto lightSpaceMatrixL = glGetUniformLocation(prg, "lightSpaceMatrix");
  auto texSamplerL = glGetUniformLocation(prg, "texSampler");
  auto hasTextureL = glGetUniformLocation(prg, "hasTexture");
  auto materialColorL = glGetUniformLocation(prg, "materialColor");
  auto specularStrengthL = glGetUniformLocation(prg, "specularStrength");
  auto viewPosL = glGetUniformLocation(prg, "viewPos");
  auto skyboxL = glGetUniformLocation(prg, "skybox");
  auto shadowMapL = glGetUniformLocation(prg, "shadowMap");
  auto isShadowCatcherL = glGetUniformLocation(prg, "isShadowCatcher");

  GLuint skyboxVAO, skyboxVBO;
  glGenVertexArrays(1, &skyboxVAO);
  glBindVertexArray(skyboxVAO);
  glGenBuffers(1, &skyboxVBO);
  glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

  GLuint catcherVAO, catcherVBO;
  glGenVertexArrays(1, &catcherVAO);
  glBindVertexArray(catcherVAO);
  glGenBuffers(1, &catcherVBO);
  glBindBuffer(GL_ARRAY_BUFFER, catcherVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(catcherVertices), &catcherVertices, GL_STATIC_DRAW);

  int stride = 8 * sizeof(float);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

  std::vector<std::string> faces = {"../model/skybox/right.jpg", "../model/skybox/left.jpg", "../model/skybox/top.jpg", "../model/skybox/bottom.jpg", "../model/skybox/front.jpg", "../model/skybox/back.jpg"};
  GLuint cubemapTexture = loadCubemap(faces);

  // Setup Orbit Camera
  auto orbitCamera = std::make_shared<basicCamera::OrbitCamera>();
  orbitCamera->setFocus(glm::vec3(0.0f, 0.5f, 0.0f));
  orbitCamera->setDistance(8.0f);
  orbitCamera->setYAngle(glm::radians(20.0f));
  orbitCamera->setXAngle(glm::radians(20.0f));

  auto perspective = std::make_shared<basicCamera::PerspectiveCamera>();
  perspective->setFovy(glm::radians(60.0f));
  perspective->setNear(0.1f);
  perspective->setFar(1000.0f);
  perspective->setAspect(1024.0f / 768.0f);

  float sensitivity = 0.005f;
  std::map<int, bool> keys;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  SDL_SetWindowRelativeMouseMode(window, false);

  Uint64 lastTime = SDL_GetTicks();
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
      {
        keys[event.key.key] = true;
        std::string targetPart = "";
        if (event.key.key == SDLK_1)
          targetPart = "door_l";
        if (event.key.key == SDLK_2)
          targetPart = "door_r";
        if (event.key.key == SDLK_3)
          targetPart = "hood";
        if (!targetPart.empty())
        {
          for (auto &obj : carObjects)
          {
            std::string objNameLower = toLower(obj.name);
            if (objNameLower.find(targetPart) != std::string::npos)
              obj.isOpen = !obj.isOpen;
          }
        }
      }
      if (event.type == SDL_EVENT_MOUSE_MOTION)
      {
        if (event.motion.state & SDL_BUTTON_LMASK)
        {
          orbitCamera->addYAngle(event.motion.xrel * sensitivity);
          orbitCamera->addXAngle(event.motion.yrel * sensitivity);
        }
      }
      if (event.type == SDL_EVENT_MOUSE_WHEEL)
      {
        float currentDist = orbitCamera->getDistance();
        float newDist = currentDist - (event.wheel.y * 2.0f);
        if (newDist < 2.0f)
          newDist = 2.0f;
        if (newDist > 1000.0f)
          newDist = 1000.0f;
        orbitCamera->setDistance(newDist);
      }
    }

    Uint64 now = SDL_GetTicks();
    float deltaTime = (now - lastTime) / 1000.0f;
    lastTime = now;

    updateObjectAnimations(deltaTime);

    glm::vec3 lightPos(20.0f, 35.0f, 20.0f);
    glm::mat4 lightProjection = glm::ortho(-55.0f, 55.0f, -55.0f, 55.0f, 1.0f, 100.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(depthProg);
    glUniformMatrix4fv(d_lightSpaceMatrixL, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    glDisable(GL_CULL_FACE);
    for (auto &obj : carObjects)
    {
      glUniformMatrix4fv(d_modelMatrixL, 1, GL_FALSE, glm::value_ptr(obj.transform));
      for (auto &part : obj.parts)
      {
        glBindVertexArray(part.vao);
        glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
      }
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 viewMatrix = orbitCamera->getView();
    glm::mat4 projMatrix = perspective->getProjection();
    glm::vec3 camPos = glm::vec3(glm::inverse(viewMatrix)[3]);

    //  SKYBOX
    glDepthFunc(GL_LEQUAL);
    glUseProgram(skyboxProgram);
    glm::mat4 viewNoTrans = glm::mat4(glm::mat3(viewMatrix));
    glUniformMatrix4fv(skyViewL, 1, GL_FALSE, glm::value_ptr(viewNoTrans));
    glUniformMatrix4fv(skyProjL, 1, GL_FALSE, glm::value_ptr(projMatrix));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glUniform1i(skyTexL, 0);
    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);

    // SETUP Car
    glUseProgram(prg);
    glUniformMatrix4fv(viewMatrixL, 1, GL_FALSE, glm::value_ptr(viewMatrix));
    glUniformMatrix4fv(projMatrixL, 1, GL_FALSE, glm::value_ptr(projMatrix));
    glUniformMatrix4fv(lightSpaceMatrixL, 1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
    glUniform3fv(viewPosL, 1, glm::value_ptr(camPos));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glUniform1i(skyboxL, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glUniform1i(shadowMapL, 2);

    glDisable(GL_CULL_FACE);

    glUniform1i(isShadowCatcherL, 0);
    glDisable(GL_BLEND);

    for (auto &obj : carObjects)
    {
      glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, glm::value_ptr(obj.transform));
      for (auto &part : obj.parts)
      {
        if (part.diffuseColor[3] > 0.95f)
        {
          glUniform4fv(materialColorL, 1, part.diffuseColor);
          glUniform1f(specularStrengthL, part.specularStrength);
          if (part.textureID != 0)
          {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, part.textureID);
            glUniform1i(texSamplerL, 0);
            glUniform1i(hasTextureL, 1);
          }
          else
          {
            glUniform1i(hasTextureL, 0);
          }
          glBindVertexArray(part.vao);
          glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1i(isShadowCatcherL, 1);

    glm::mat4 groundModel = glm::mat4(1.0f);
    groundModel = glm::scale(groundModel, glm::vec3(5.0f, 1.0f, 5.0f));
    glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, glm::value_ptr(groundModel));
    glBindVertexArray(catcherVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(isShadowCatcherL, 0);

    for (auto &obj : carObjects)
    {
      glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, glm::value_ptr(obj.transform));
      for (auto &part : obj.parts)
      {
        if (part.diffuseColor[3] <= 0.95f)
        {
          glUniform4fv(materialColorL, 1, part.diffuseColor);
          glUniform1f(specularStrengthL, part.specularStrength);
          if (part.textureID != 0)
          {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, part.textureID);
            glUniform1i(texSamplerL, 0);
            glUniform1i(hasTextureL, 1);
          }
          else
          {
            glUniform1i(hasTextureL, 0);
          }
          glBindVertexArray(part.vao);
          glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
        }
      }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  return 0;
}