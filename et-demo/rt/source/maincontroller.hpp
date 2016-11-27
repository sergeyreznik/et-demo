#pragma once

#include <et/app/application.h>
#include <et/rendering/interface/renderer.h>

#include <et/camera/camera.h>
#include <et/input/gestures.h>
#include <et-ext/rt/raytrace.h>

namespace demo
{
class MainController : public et::IApplicationDelegate
{
	et::ApplicationIdentifier applicationIdentifier() const override;
	void setApplicationParameters(et::ApplicationParameters&) override;
	void setRenderContextParameters(et::RenderContextParameters&) override;
	void applicationDidLoad(et::RenderContext*) override;
	void render(et::RenderContext*) override;
	void applicationWillTerminate() override;

	void start();

private:
	et::Dictionary _options;
	et::RenderContext* _rc = nullptr;
	et::RenderPass::Pointer _mainPass;
	et::RenderBatch::Pointer _fullscreenQuad;
	et::Raytrace _rt;
	et::Texture::Pointer _texture;
	et::DataStorage<et::vec4> _textureData;
	et::GesturesRecognizer _gestures;
	et::Camera _camera;
	et::s3d::Scene::Pointer _scene;
};
}