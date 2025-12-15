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
  float transform[16];
};

std::vector<LogicalObject> carObjects;

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

    // 3. Texture Coordinates (UV) - NUOVO!
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

  // Positions (Location 0)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);

  // Normal (Location 1)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));

  // UV (Location 2)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));

  glBindVertexArray(0);

  part.indexCount = indices.size();

  return part;
}

GLuint loadTexture(const aiMaterial *material, const aiScene *scene)
{
  aiString path;
  // Cerca la texture "Base Color" o "Diffuse"
  if (material->GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS)
  {
    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS)
    {
      return 0; // Nessuna texture trovata
    }
  }

  // Controlla se è una texture embedded (inizia con *)
  const aiTexture *embeddedTex = scene->GetEmbeddedTexture(path.C_Str());
  unsigned char *data = nullptr;
  int width, height, channels;
  int len = 0;

  if (embeddedTex)
  {
    // Texture compressa dentro il GLB (es. PNG/JPG)
    if (embeddedTex->mHeight == 0)
    {
      data = stbi_load_from_memory(
          reinterpret_cast<unsigned char *>(embeddedTex->pcData),
          embeddedTex->mWidth,
          &width, &height, &channels, 4);
    }
    else
    {
      // Texture raw (raro nei GLB compressi)
      data = stbi_load_from_memory(
          reinterpret_cast<unsigned char *>(embeddedTex->pcData),
          embeddedTex->mWidth * embeddedTex->mHeight * 4,
          &width, &height, &channels, 4);
    }
  }
  else
  {
    // Se non è embedded, prova a caricarla da file esterno (fallback)
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

  // Trasponi la matrice (da Row-Major di Assimp a Column-Major di OpenGL)
  obj.transform[0] = m.a1;
  obj.transform[1] = m.b1;
  obj.transform[2] = m.c1;
  obj.transform[3] = m.d1;
  obj.transform[4] = m.a2;
  obj.transform[5] = m.b2;
  obj.transform[6] = m.c2;
  obj.transform[7] = m.d2;
  obj.transform[8] = m.a3;
  obj.transform[9] = m.b3;
  obj.transform[10] = m.c3;
  obj.transform[11] = m.d3;
  obj.transform[12] = m.a4;
  obj.transform[13] = m.b4;
  obj.transform[14] = m.c4;
  obj.transform[15] = m.d4;

  for (unsigned int i = 0; i < node->mNumMeshes; ++i)
  {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    MeshPart part = loadMesh(mesh);

    aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
    part.textureID = loadTexture(material, scene);

    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);

    // Tenta di leggere il colore base (PBR glTF)
    if (material->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
    {
      // Fallback al vecchio diffuse color
      material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
    }

    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
    {
      // Se c'è un valore di opacità esplicito, usalo
      if (opacity < 1.0f && color.a == 1.0f)
      {
        color.a = opacity;
      }
    }

    part.diffuseColor[0] = color.r;
    part.diffuseColor[1] = color.g;
    part.diffuseColor[2] = color.b;
    part.diffuseColor[3] = color.a;

    // Tenta di leggere la rugosità/lucentezza
    // In glTF PBR: Roughness (0 = liscio, 1 = ruvido).
    // In Phong: Shininess (più alto = più lucido).
    float roughness = 0.5f;
    float metallic = 0.0f;

    // Leggiamo i fattori PBR se esistono
    material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);

    // Convertiamo approssimativamente in "forza speculare" per shader semplice
    // Se è metallico o molto liscio (roughness bassa), brilla di più.
    part.specularStrength = (1.0f - roughness) + metallic;
    if (part.specularStrength > 1.0f)
      part.specularStrength = 1.0f;

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

  if (!loadCarModel("../model/mustang.glb"))
  {
    std::cerr << "Error to load car model" << std::endl;
    return 1;
  }

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
    vPos = worldPos.xyz; // Passiamo la posizione nel mondo
    gl_Position = projMatrix * viewMatrix * worldPos;
    
    // Normal matrix corretta per evitare distorsioni se scali
    vNormal = mat3(transpose(inverse(modelMatrix))) * normal; 
    vTexCoord = texCoord;
  }
  ).";

  auto fsSrc = R".(
  #version 410
  in vec3 vNormal;
  in vec3 vPos;      // Posizione nel mondo (serve per la luce speculare)
  in vec2 vTexCoord;
  
  out vec4 fColor;
  
  uniform sampler2D texSampler;
  uniform int hasTexture;
  uniform vec4 materialColor;    // Colore letto da Assimp
  uniform float specularStrength;// Quanto brilla
  uniform vec3 viewPos;          // Posizione della telecamera

  void main(){
    // 1. Colore Base (Texture o Tinta Unita)
    vec4 baseColor = materialColor;
    if (hasTexture == 1) {
       // Se c'è texture, moltiplichiamo il colore (spesso bianco) per la texture
       baseColor *= texture(texSampler, vTexCoord);
    }

    // 2. Luce Ambientale (Base minima)
    vec3 ambient = 0.3 * baseColor.rgb;

    // 3. Luce Diffusa (Direzionale)
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(vec3(5.0, 10.0, 5.0)); // Luce fissa in alto a destra
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * baseColor.rgb;

    // 4. Luce Speculare (Riflessi luccicanti - Blinn-Phong)
    vec3 viewDir = normalize(viewPos - vPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0); // 32 = shininess fissa
    vec3 specular = vec3(0.5) * spec * specularStrength; // Luce bianca * intensità

    // Somma tutto
    vec3 result = ambient + diffuse + specular;
    fColor = vec4(result, baseColor.a);
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

  float cameraPosition[3] = {0, 1.5, 8.0};
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
  float cameraSpeed = 0.1;

  std::map<int, bool> keys;

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
    glUniform3fv(viewPosL, 1, cameraPosition);

    for (auto &obj : carObjects)
    {
      // float modelMatrix[16];
      // matrixIdentity(modelMatrix);

      // Applica offset basato sul nome dell'oggetto
      // float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;

      /*   if (obj.name.find("door_R") != std::string::npos)
         {
           offsetZ = 20.0f;
         }
         else if (obj.name.find("door_L") != std::string::npos)
         {
           offsetZ = -20.0f;
         }
         else if (obj.name.find("hood") != std::string::npos)
         {
           offsetY = 20.0f;
         }
   */
      glUniformMatrix4fv(modelMatrixL, 1, GL_FALSE, obj.transform);

      for (auto &part : obj.parts)
      {
        // Passa il colore del materiale
        glUniform4fv(materialColorL, 1, part.diffuseColor);
        // Passa la lucentezza
        glUniform1f(specularStrengthL, part.specularStrength);

        if (part.textureID != 0)
        {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, part.textureID);
          glUniform1i(texSamplerL, 0); // Usa texture unit 0
          glUniform1i(hasTextureL, 1); // Diciamo allo shader "abbiamo una texture"
        }
        else
        {
          glUniform1i(hasTextureL, 0); // Niente texture
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
