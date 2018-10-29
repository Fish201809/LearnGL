﻿#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh.h"
#include "shaderprogram.h"

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include "error.h"
using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

class Model
{
public:
	// 已经载入的纹理
	vector<Texture> textures_loaded;
	//网格
	vector<Mesh> meshes;
	//模型根目录
	string directory;
	//gama矫正
	bool gammaCorrection;

	glm::mat4 model_matrix_;

	Model(const string &path, bool gamma = false) : gammaCorrection(gamma) {
		loadmesh(path);
	}

	void render(ShaderProgram &shader) {

		glm::mat4 vp_matrix = camera_->get_matrix();
		shader.use();
		shader.set_uniform_mat4("vp_matrix", vp_matrix);
		shader.set_uniform_mat4("model_matrix", model_matrix_);

		for (unsigned int i = 0; i < meshes.size(); i++)
			meshes[i].draw(shader);
	}
	void setMatrix(glm::mat4 model_matrix) {
		model_matrix_ = model_matrix;
	}
private:

	void loadmesh(const string &path) {
		/**
		 * aiProcess_Triangulate - 网格三角化
		 * aiProcess_Triangulate - 计算切线空间
		 * aiProcess_FlipUVs - 
		 */
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
		{
			cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
			return;
		}
		// 加载纹理目录的路径
		directory = path.substr(0, path.find_last_of('/'));

		// 递归处理模型节点
		process_node(scene->mRootNode, scene);
	}

	// 递归处理所有模型节点
	void process_node(aiNode *node, const aiScene *scene) {
		for (unsigned int i = 0; i < node->mNumMeshes; i++) {
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			meshes.push_back(process_mesh(mesh, scene));
		}
		// 处理子节点
		for (unsigned int i = 0; i < node->mNumChildren; i++) {
			process_node(node->mChildren[i], scene);
		}
	}
	// 处理网格
	Mesh process_mesh(aiMesh *mesh, const aiScene *scene) {
		vector<Vertex> vertices;
		vector<unsigned int> indices;
		vector<Texture> textures;

		for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
			Vertex vertex;
			glm::vec3 vector;
			// 顶点坐标
			vector.x = mesh->mVertices[i].x;
			vector.y = mesh->mVertices[i].y;
			vector.z = mesh->mVertices[i].z;
			vertex.position = vector;
			// 法线坐标
			vector.x = mesh->mNormals[i].x;
			vector.y = mesh->mNormals[i].y;
			vector.z = mesh->mNormals[i].z;
			vertex.normal = vector;
			// 纹理坐标
			if (mesh->mTextureCoords[0]) 
			{
				glm::vec2 vec;
				// 一个顶点可以含有 AI_MAX_NUMBER_OF_TEXTURECOORDS 个纹理坐标, 该函数只尝试载入模型的其中一个纹理
				vec.x = mesh->mTextureCoords[0][i].x;
				vec.y = mesh->mTextureCoords[0][i].y;
				vertex.cord = vec;
			}
			else
				vertex.cord = glm::vec2(0.0f, 0.0f);
			// 切线
			vector.x = mesh->mTangents[i].x;
			vector.y = mesh->mTangents[i].y;
			vector.z = mesh->mTangents[i].z;
			vertex.tangent = vector;
			// 双切线
			vector.x = mesh->mBitangents[i].x;
			vector.y = mesh->mBitangents[i].y;
			vector.z = mesh->mBitangents[i].z;
			vertex.bitangent = vector;
			vertices.push_back(vertex);
		}
		// 获得网格全部面片绘制的顶点索引
		for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
			aiFace face = mesh->mFaces[i];
			for (unsigned int j = 0; j < face.mNumIndices; j++)
				indices.push_back(face.mIndices[j]);
		}
		/**
		 * 材质 - 一系列的纹理贴图 用于着色器的计算
		 */
		aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

		// 1. diffuse maps
		vector<Texture> diffuseMaps = LoadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
		textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
		// 2. specular maps
		vector<Texture> specularMaps = LoadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
		textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
		// 3. normal maps
		std::vector<Texture> normalMaps = LoadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
		textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
		// 4. height maps
		std::vector<Texture> heightMaps = LoadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
		textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

		return Mesh(vertices, indices, textures);
	}

	/**
	 * 载入 所有type类型的纹理 且避免纹理重复载入
	 */
	vector<Texture> LoadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName) {
		vector<Texture> textures;
		for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
			aiString str;
			mat->GetTexture(type, i, &str);
			bool skip = false;
			for (unsigned int j = 0; j < textures_loaded.size(); j++) {
				if (std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0) {
					textures.push_back(textures_loaded[j]);
					skip = true;
					break;
				}
			}
			if (!skip) {  
				Texture texture;
				texture.id = TextureFromFile(str.C_Str(), this->directory);
				texture.type = typeName;
				texture.path = str.C_Str();
				textures.push_back(texture);
				textures_loaded.push_back(texture); 
			}
		}
		return textures;
	}
};

/**
 * 生成 OpenGL 纹理对象
 */
unsigned int TextureFromFile(const char *path, const string &directory, bool gamma) {
	string filename = string(path);
	filename = directory + '/' + filename;

	unsigned int textureID;
	glGenTextures(1, &textureID);

	int width, height, nrComponents;
	unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
	if (data) {
		GLenum format;
		if (nrComponents == 1)
			format = GL_RED;
		else if (nrComponents == 3)
			format = GL_RGB;
		else if (nrComponents == 4)
			format = GL_RGBA;

		glBindTexture(GL_TEXTURE_2D, textureID);
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		stbi_image_free(data);
	}
	else {
		std::cout << "Texture failed to load at path: " << path << std::endl;
		stbi_image_free(data);
	}

	return textureID;
}