#pragma once

#include <TomCat.h>

class Z_Examples2D: public TomCat::Layer
{
public:
	Z_Examples2D();
	virtual~Z_Examples2D() = default;

	virtual void OnAttach()override;
	virtual void OnDetach()override;

	void OnUpdate(TomCat::Timestep ts) override;
	virtual void OnImGuiRender()override;
	void OnEvent(TomCat::Event& e) override;

private:

	TomCat::OrthographicCameraController m_CameraController;

	TomCat::Ref<TomCat::VertexArray> m_SquareVA;

	TomCat::Ref<TomCat::Shader> m_FlatColorShader;

	TomCat::Ref<TomCat::Texture2D> m_CheckerboardTexture;

	glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f,1.0f };
};
