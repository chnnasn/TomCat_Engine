#pragma once

#include "TomCat.h"

class ExampleLayer : public TomCat::Layer
{
public:
	ExampleLayer();
	virtual ~ExampleLayer() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	void OnUpdate(TomCat::Timestep ts) override;
	virtual void OnImGuiRender() override;
	void OnEvent(TomCat::Event& e) override;
private:
	TomCat::ShaderLibrary m_ShaderLibrary;
	TomCat::Ref<TomCat::Shader> m_Shader;
	TomCat::Ref<TomCat::VertexArray> m_VertexArray;

	TomCat::Ref<TomCat::Shader> m_FlatColorShader;
	TomCat::Ref<TomCat::VertexArray> m_SquareVA;

	TomCat::Ref<TomCat::Texture2D> m_Texture, m_ChernoLogoTexture;

	TomCat::OrthographicCameraController m_CameraController;
	glm::vec3 m_SquareColor = { 0.2f, 0.3f, 0.8f };
};

