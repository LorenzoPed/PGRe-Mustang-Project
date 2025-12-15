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
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

using namespace ge::gl;

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
  glm::mat4 transform; // Sostituito float[16] con glm::mat4
};

std::vector<LogicalObject> carObjects;
Assimp::Importer importer;
const aiScene *g_scene = nullptr;

MeshPart loadMesh(aiMesh *mesh)
{
  std::vector<float> vertices;
  std::vector<unsigned int> indices;

  for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
  {
    // 1. Posizioni
    vertices.push_back(mesh->mVertices[v].x);
    vertices.push_back(mesh->mVertices[v].y);
    vertices.push_back(mesh->mVertices[v].z);

    // 2. Normali
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

    // 3. Texture Coordinates (UV)
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
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                   0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    }
    else
    {
      std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
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
    {
      return 0;
    }
  }

  const aiTexture *embeddedTex = scene->GetEmbeddedTexture(path.C_Str());
  unsigned char *data = nullptr;
  int width, height, channels;

  if (embeddedTex)
  {
    if (embeddedTex->mHeight == 0)
    {
      data = stbi_load_from_memory(
          reinterpret_cast<unsigned char *>(embeddedTex->pcData),
          embeddedTex->mWidth,
          &width, &height, &channels, 4);
    }
    else
    {
      data = stbi_load_from_memory(
          reinterpret_cast<unsigned char *>(embeddedTex->pcData),
          embeddedTex->mWidth * embeddedTex->mHeight * 4,
          &width, &height, &channels, 4);
    }
  }
  else
  {
    data = stbi_load(path.C_Str(), &width, &height, &channels, 4);
  }

  if (!data)
  {
    std::cerr << "Fallito caricamento texture: " << path.C_Str() << std::endl;
    return 0;
  }

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

  // Conversione da Assimp (Row-Major) a GLM (Column-Major)
  // Costruiamo la matrice colonna per colonna usando i membri di Assimp
  obj.transform = glm::mat4(
      m.a1, m.b1, m.c1, m.d1, // Colonna 1
      m.a2, m.b2, m.c2, m.d2, // Colonna 2
      m.a3, m.b3, m.c3, m.d3, // Colonna 3
      m.a4, m.b4, m.c4, m.d4  // Colonna 4
  );

  for (unsigned int i = 0; i < node->mNumMeshes; ++i)
  {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    MeshPart part = loadMesh(mesh);

    aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
    part.textureID = loadTexture(material, scene);

    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (material->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
    {
      material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
    }

    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
    {
      if (opacity < 1.0f && color.a == 1.0f)
      {
        color.a = opacity;
      }
    }

    part.diffuseColor[0] = color.r;
    part.diffuseColor[1] = color.g;
    part.diffuseColor[2] = color.b;
    part.diffuseColor[3] = color.a;

    float roughness = 0.5f;
    float metallic = 0.0f;
    material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);

    part.specularStrength = (1.0f - roughness) + metallic;
    if (part.specularStrength > 1.0f)
      part.specularStrength = 1.0f;

    obj.parts.push_back(part);
  }

  if (!obj.parts.empty())
  {
    carObjects.push_back(obj);
    std::cout << "Object loaded: " << obj.name << " with " << obj.parts.size() << " meshes\n";
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i)
  {
    processNode(node->mChildren[i], scene);
  }
}

// --- INIZIO BLOCCO ANIMAZIONI ---

// Trova l'indice del keyframe per la Posizione
unsigned int findPosition(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumPositionKeys - 1; i++)
  {
    if (animationTime < (float)pNodeAnim->mPositionKeys[i + 1].mTime)
      return i;
  }
  return 0;
}

// Trova l'indice del keyframe per la Rotazione
unsigned int findRotation(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumRotationKeys - 1; i++)
  {
    if (animationTime < (float)pNodeAnim->mRotationKeys[i + 1].mTime)
      return i;
  }
  return 0;
}

// Trova l'indice del keyframe per la Scala
unsigned int findScaling(float animationTime, const aiNodeAnim *pNodeAnim)
{
  for (unsigned int i = 0; i < pNodeAnim->mNumScalingKeys - 1; i++)
  {
    if (animationTime < (float)pNodeAnim->mScalingKeys[i + 1].mTime)
      return i;
  }
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
  float deltaTime = (float)(pNodeAnim->mPositionKeys[nextIdx].mTime - pNodeAnim->mPositionKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mPositionKeys[idx].mTime) / deltaTime;
  factor = glm::clamp(factor, 0.0f, 1.0f);

  const auto &start = pNodeAnim->mPositionKeys[idx].mValue;
  const auto &end = pNodeAnim->mPositionKeys[nextIdx].mValue;
  auto delta = end - start;
  auto res = start + factor * delta;
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
  float deltaTime = (float)(pNodeAnim->mRotationKeys[nextIdx].mTime - pNodeAnim->mRotationKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mRotationKeys[idx].mTime) / deltaTime;
  factor = glm::clamp(factor, 0.0f, 1.0f);

  const auto &startQ = pNodeAnim->mRotationKeys[idx].mValue;
  const auto &endQ = pNodeAnim->mRotationKeys[nextIdx].mValue;
  glm::quat startGLM(startQ.w, startQ.x, startQ.y, startQ.z);
  glm::quat endGLM(endQ.w, endQ.x, endQ.y, endQ.z);

  return glm::slerp(startGLM, endGLM, factor);
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
  float deltaTime = (float)(pNodeAnim->mScalingKeys[nextIdx].mTime - pNodeAnim->mScalingKeys[idx].mTime);
  float factor = (animationTime - (float)pNodeAnim->mScalingKeys[idx].mTime) / deltaTime;
  factor = glm::clamp(factor, 0.0f, 1.0f);

  const auto &start = pNodeAnim->mScalingKeys[idx].mValue;
  const auto &end = pNodeAnim->mScalingKeys[nextIdx].mValue;
  auto delta = end - start;
  auto res = start + factor * delta;
  return glm::vec3(res.x, res.y, res.z);
}

void updateAnimations(float currentTimeInSeconds)
{
  if (!g_scene || !g_scene->HasAnimations())
    return;

  aiAnimation *animation = g_scene->mAnimations[0];
  float ticksPerSecond = (animation->mTicksPerSecond != 0) ? (float)animation->mTicksPerSecond : 25.0f;
  float timeInTicks = currentTimeInSeconds * ticksPerSecond;
  float animationTime = fmod(timeInTicks, (float)animation->mDuration);

  for (unsigned int i = 0; i < animation->mNumChannels; i++)
  {
    aiNodeAnim *channel = animation->mChannels[i];
    std::string nodeName = channel->mNodeName.C_Str();

    for (auto &obj : carObjects)
    {
      if (obj.name == nodeName)
      {
        glm::vec3 pos = calcInterpolatedPosition(animationTime, channel);
        glm::quat rot = calcInterpolatedRotation(animationTime, channel);
        glm::vec3 scale = calcInterpolatedScaling(animationTime, channel);

        glm::mat4 matPos = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 matRot = glm::toMat4(rot);
        glm::mat4 matScale = glm::scale(glm::mat4(1.0f), scale);

        obj.transform = matPos * matRot * matScale;
        break;
      }
    }
  }
}
// --- FINE BLOCCO ANIMAZIONI ---

bool loadCarModel(const std::string &path)
{
  g_scene = importer.ReadFile(path,
                              aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);

  if (!g_scene || !g_scene->mRootNode)
  {
    std::cerr << "Error to load model: " << importer.GetErrorString() << std::endl;
    return false;
  }

  carObjects.clear();
  processNode(g_scene->mRootNode, g_scene);

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

// --- Funzioni matematiche manuali RIMOSSE (matrixMultiplication, rotateX, frustum, ecc.) ---

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
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

  auto window = SDL_CreateWindow("PGR2025 - GLM Version", 1024, 768, SDL_WINDOW_OPENGL);
  auto context = SDL_GL_CreateContext(window);

  ge::gl::init();

  glEnable(GL_MULTISAMPLE);

  if (!loadCarModel("../model/mustang.glb"))
  {
    std::cerr << "Error to load car model" << std::endl;
    return 1;
  }

  // --- Shader immutato ---
  auto vsSrc = R".(
  #version 410
  layout(location=0) in vec3 position;
  layout(location=1) in vec3 normal;
  layout(location=2) in vec2 texCoord;

  out vec3 vNormal;
  out vec3 vPos; 
  out vec2 vTexCoord;

  uniform mat4 viewMatrix = mat4(1);
  uniform mat4 projMatrix = mat4(1);
  uniform mat4 modelMatrix = mat4(1);

  void main(){
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vPos = worldPos.xyz; 
    gl_Position = projMatrix * viewMatrix * worldPos;
    
    // Normal matrix
    vNormal = mat3(transpose(inverse(modelMatrix))) * normal; 
    vTexCoord = texCoord;
  }
  ).";

  auto fsSrc = R".(
  #version 410
  in vec3 vNormal;
  in vec3 vPos;
  in vec2 vTexCoord;
  
  out vec4 fColor;
  
  uniform sampler2D texSampler;
  uniform samplerCube skybox;
  
  uniform int hasTexture;
  uniform vec4 materialColor;
  uniform float specularStrength;
  uniform vec3 viewPos;

  void main(){
    vec4 baseColor = materialColor;
    if (hasTexture == 1) {
       baseColor *= texture(texSampler, vTexCoord);
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(viewPos - vPos);
    vec3 R = reflect(-V, N);           

    vec3 envColor = texture(skybox, R).rgb;
    vec3 finalColor = baseColor.rgb;

    if (specularStrength > 0.95) {
        finalColor = mix(baseColor.rgb, envColor, 0.65); 
    }
    else {
        float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
        float reflectionAmount = 0.05 + (fresnel * 0.5); 
        reflectionAmount *= specularStrength; 

        finalColor = mix(baseColor.rgb, envColor, reflectionAmount);

        vec3 lightDir = normalize(vec3(5.0, 10.0, 5.0));
        float diff = max(dot(N, lightDir), 0.0);
        finalColor += (baseColor.rgb * diff * 0.6);
    }

    fColor = vec4(finalColor, baseColor.a);
  }
  ).";

  auto vs = createShader(GL_VERTEX_SHADER, vsSrc);
  auto fs = createShader(GL_FRAGMENT_SHADER, fsSrc);
  auto prg = createProgram({vs, fs});

  auto viewMatrixL = glGetUniformLocation(prg, "viewMatrix");
  auto projMatrixL = glGetUniformLocation(prg, "projMatrix");
  auto modelMatrixL = glGetUniformLocation(prg, "modelMatrix");
  auto texSamplerL = glGetUniformLocation(prg, "texSampler");
  auto hasTextureL = glGetUniformLocation(prg, "hasTexture");
  auto materialColorL = glGetUniformLocation(prg, "materialColor");
  auto specularStrengthL = glGetUniformLocation(prg, "specularStrength");
  auto viewPosL = glGetUniformLocation(prg, "viewPos");

  std::vector<std::string> faces = {
      "../model/skybox/right.jpg",
      "../model/skybox/left.jpg",
      "../model/skybox/top.jpg",
      "../model/skybox/bottom.jpg",
      "../model/skybox/front.jpg",
      "../model/skybox/back.jpg"};
  GLuint cubemapTexture = loadCubemap(faces);

  auto skyboxL = glGetUniformLocation(prg, "skybox");

  // --- Variabili GLM ---
  glm::vec3 cameraPosition(0.0f, 1.5f, 8.0f);
  float angleX = 0.3f;
  float angleY = 0.0f;

  float sensitivity = 0.01;
  float cameraSpeed = 0.1;

  std::map<int, bool> keys;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  float currentTime = 0.0f;
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

    Uint64 now = SDL_GetTicks();
    float deltaTime = (now - lastTime) / 1000.0f;
    lastTime = now;

    // Aggiorna animazioni
    currentTime += deltaTime;
    updateAnimations(currentTime);

    // --- Calcolo View Matrix con GLM ---
    // 1. Reset (Identità)
    glm::mat4 viewMatrix = glm::mat4(1.0f);

    // 2. Rotazione (Equivalente al tuo matrixMultiplication(VR, VRX, VRY))
    // Nota: L'ordine è importante. Qui ruotiamo il "Mondo" attorno alla camera.
    // Applicare X poi Y alla matrice View è equivalente a ruotare la camera.

    viewMatrix = glm::rotate(viewMatrix, angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    viewMatrix = glm::rotate(viewMatrix, angleY, glm::vec3(0.0f, 1.0f, 0.0f));

    // --- Movimento Camera (Free Look) ---
    // Estraiamo i vettori "locali" della camera dalla matrice di vista.
    // In una View Matrix ortogonale (solo rotazione), la trasposta è l'inversa.
    // Le colonne della trasposta (ovvero le righe della View Matrix originale)
    // rappresentano gli assi Right, Up, Forward della camera in coordinate World.

    glm::vec3 right = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    glm::vec3 up = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    glm::vec3 forward = glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);

    float leftRight = ((int)(keys[SDLK_D]) - (int)keys[SDLK_A]) * cameraSpeed;
    float forwardBackward = ((int)(keys[SDLK_S]) - (int)keys[SDLK_W]) * cameraSpeed;
    float upDown = ((int)(keys[SDLK_SPACE]) - (int)keys[SDLK_LSHIFT]) * cameraSpeed;

    // Aggiorniamo la posizione
    cameraPosition += right * leftRight;
    cameraPosition += forward * forwardBackward; // Forward è positivo verso "dietro" in OpenGL view space standard
    cameraPosition += up * upDown;

    // 3. Traslazione finale (spostiamo il mondo in direzione opposta alla camera)
    viewMatrix = glm::translate(viewMatrix, -cameraPosition);

    // --- Projezione con GLM ---
    glm::mat4 projMatrix = glm::perspective(glm::radians(90.0f), 1024.0f / 768.0f, 0.1f, 1000.0f);

    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glPointSize(10);
    glUseProgram(prg);

    // Invio Uniforms usando glm::value_ptr
    glUniformMatrix4fv(viewMatrixL, 1, GL_FALSE, glm::value_ptr(viewMatrix));
    glUniformMatrix4fv(projMatrixL, 1, GL_FALSE, glm::value_ptr(projMatrix));
    glUniform3fv(viewPosL, 1, glm::value_ptr(cameraPosition));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glUniform1i(skyboxL, 1);

    for (auto &obj : carObjects)
    {
      // Passiamo la trasformazione dell'oggetto (ora glm::mat4)
      glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, glm::value_ptr(obj.transform));

      for (auto &part : obj.parts)
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

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  return 0;
}
