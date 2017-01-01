#include <et/core/json.h>
#include <et/core/conversion.h>
#include <et/core/hardware.h>
#include <et/camera/camera.h>
#include <et/scene3d/objloader.h>
#include <et/rendering/base/primitives.h>
#include <et/rendering/base/helpers.h>
#include <et/rendering/rendercontext.h>
#include <et/imaging/texturedescription.h>
#include "maincontroller.hpp"

namespace et
{

void MainController::setApplicationParameters(ApplicationParameters& p)
{
#if (ET_PLATFORM_WIN)
	p.renderingAPI = RenderingAPI::Vulkan;
#elif (ET_PLATFORM_MAC)
	p.renderingAPI = RenderingAPI::Metal;
#endif

	p.context.style |= ContextOptions::Style::Sizable;
	p.context.supportsHighResolution = true;
	p.context.size = 4 * currentScreen().frame.size() / 5;
}

void MainController::setRenderContextParameters(RenderContextParameters& p)
{
	p.multisamplingQuality = MultisamplingQuality::None;
}

void MainController::applicationDidLoad(RenderContext* rc)
{
	_rc = rc;
	srand(static_cast<unsigned int>(time(nullptr)));

	ObjectsCache localCache;

	auto configName = application().resolveFileName("media/config/config.json");
	VariantClass vc = VariantClass::Invalid;
	_options = json::deserialize(loadTextFile(configName), vc);
	ET_ASSERT(vc == VariantClass::Dictionary);

	if (_options.hasKey("reference"))
	{
		vc = VariantClass::Invalid;
		configName = application().resolveFileName("media/config/" + _options.stringForKey("reference")->content);
		Dictionary reference = json::deserialize(loadTextFile(configName), vc);
		ET_ASSERT(vc == VariantClass::Dictionary);

		for (auto& kv : reference->content)
		{
			_options.setObjectForKey(kv.first, kv.second);
		}
	}

	auto modelName = application().resolveFileName(_options.stringForKey("model-name")->content);

	VertexDeclaration decl(true, VertexAttributeUsage::Position, DataType::Vec3);
	decl.push_back(VertexAttributeUsage::Normal, DataType::Vec3);
	decl.push_back(VertexAttributeUsage::TexCoord0, DataType::Vec2);

	if (!fileExists(modelName))
	{
		ET_FAIL("Unable to load specified model");
	}

	_scene = s3d::Scene::Pointer::create();
	_scene->setMainCamera(Camera::Pointer::create());

	OBJLoader loader(modelName, OBJLoader::Option_CalculateTangents);
	auto model = loader.load(rc->renderer(), _scene->storage(), localCache);
	model->setParent(_scene.pointer());

	rt::Options rtOptions;
	rtOptions.raysPerPixel = static_cast<uint32_t>(_options.integerForKey("rays-per-pixel", 32)->content);
	rtOptions.maxKDTreeDepth = static_cast<uint32_t>(_options.integerForKey("kd-tree-max-depth", 4)->content);
	rtOptions.maxPathLength = static_cast<uint32_t>(_options.integerForKey("max-path-length", 0ll)->content);
	rtOptions.renderRegionSize = static_cast<uint32_t>(_options.integerForKey("render-region-size", 32)->content);
	rtOptions.threads = static_cast<uint32_t>(_options.integerForKey("threads", 0ll)->content);
	rtOptions.lightSamples = static_cast<uint32_t>(_options.integerForKey("light-samples", 1)->content);
	rtOptions.bsdfSamples = static_cast<uint32_t>(_options.integerForKey("bsdf-samples", 1)->content);
	rtOptions.renderKDTree = _options.integerForKey("render-kd-tree", 0ll)->content != 0;
	rtOptions.apertureSize = _options.floatForKey("aperture-size", 0.0f)->content;
	rtOptions.focalDistanceCorrection = _options.floatForKey("focal-distance-correction", 0.0f)->content;

	if (_options.stringForKey("method")->content == "forward")
		rtOptions.method = rt::RaytraceMethod::ForwardLightTracing;
	else
		rtOptions.method = rt::RaytraceMethod::BackwardPathTracing;
	_rt.setOptions(rtOptions);

	_rt.setOutputMethod([this](const vec2i& pixel, const vec4& color)
	{
		if ((pixel.x >= 0) && (pixel.y >= 0) && (pixel.x < _texture->size().x) && (pixel.y < _texture->size().y))
		{
			int pos = pixel.x + pixel.y * _texture->size().x;
			_textureData[pos] = mix(_textureData[pos], color, color.w);
		}
	});

	auto integrator = _options.stringForKey("integrator", "path-trace")->content;
	if (integrator == "ao")
	{
		_rt.setIntegrator(rt::AmbientOcclusionIntegrator::Pointer::create());
	}
	else if (integrator == "hack-ao")
	{
		_rt.setIntegrator(rt::AmbientOcclusionHackIntegrator::Pointer::create());
	}
	else if (integrator == "normals")
	{
		_rt.setIntegrator(rt::NormalsIntegrator::Pointer::create());
	}
	else if (integrator == "fresnel")
	{
		_rt.setIntegrator(rt::FresnelIntegrator::Pointer::create());
	}
	else
	{
		_rt.setIntegrator(rt::BackwardPathTracingIntegrator::Pointer::create());
	}

	rt::float4 envColor(arrayToVec4(_options.arrayForKey("env-color")));

	std::string envMap = _options.stringForKey("env-map")->content;
	if (!envMap.empty())
	{
		envMap = application().resolveFileName(envMap);
	}

	/* TODO : load environemnt map
	if (fileExists(envMap))
	{
		TextureDescription::Pointer texture = TextureDescription::Pointer::create(envMap);
		_rt.setEnvironmentSampler(rt::EnvironmentEquirectangularMapSampler::Pointer::create(texture, envColor));
	}
	else
	{
		auto envType = _options.stringForKey("env-type", "uniform")->content;
		if (envType == "directional")
		{
			rt::float4 light(arrayToVec4(_options.arrayForKey("light-direction")));
			_rt.setEnvironmentSampler(rt::DirectionalLightSampler::Pointer::create(light, envColor));
		}
		else
		{
			_rt.setEnvironmentSampler(rt::EnvironmentColorSampler::Pointer::create(envColor));
		}
	}
	*/;

	Input::instance().keyPressed.connect([this](size_t key)
	{
		if (key == ET_KEY_SPACE)
			start();
	});

	_gestures.click.connect([this](const PointerInputInfo& p)
	{
		vec2i pixel(int(p.pos.x), int(p.pos.y));
		vec4 color = _rt.performAtPoint(_scene, _texture->size(), pixel);
		_rt.output(vec2i(pixel.x, pixel.y), color);
	});

	Input::instance().keyPressed.invokeInMainRunLoop(ET_KEY_SPACE);
}

void MainController::start()
{
	_rt.stop();

	vec2i textureSize = _rc->size();

	_textureData.resize(textureSize.square());
	_textureData.fill(0);

	TextureDescription::Pointer desc = TextureDescription::Pointer::create();
	desc->target = TextureTarget::Texture_2D;
	desc->format = TextureFormat::RGBA32F;
	desc->size = textureSize;
	desc->data = BinaryDataStorage(reinterpret_cast<unsigned char*>(_textureData.data()), _textureData.dataSize());
	_texture = _rc->renderer()->createTexture(desc);

	RenderPass::ConstructionInfo passInfo;
	passInfo.color[0].enabled = true;
	_mainPass = _rc->renderer()->allocateRenderPass(passInfo);
	_fullscreenQuad = renderhelper::createFullscreenRenderBatch(_texture);

	const vec3 lookPoint = arrayToVec3(_options.arrayForKey("camera-view-point"));
	const vec3 offset = arrayToVec3(_options.arrayForKey("camera-offset"));
	float cameraFOV = _options.floatForKey("camera-fov", 60.0f)->content * TO_RADIANS;
	float cameraPhi = _options.floatForKey("camera-phi", 0.0f)->content * TO_RADIANS;
	float cameraTheta = _options.floatForKey("camera-theta", 0.0f)->content * TO_RADIANS;
	float cameraDistance = _options.floatForKey("camera-distance", 3.0f)->content;
	_scene->mainCamera()->perspectiveProjection(cameraFOV, vector2ToFloat(textureSize).aspect(), 0.1f, 2048.0f);
	_scene->mainCamera()->lookAt(cameraDistance * fromSpherical(cameraTheta, cameraPhi) + offset, lookPoint);
	log::info("Camera position: %f, %f, %f", 
		_scene->mainCamera()->position().x, _scene->mainCamera()->position().y, _scene->mainCamera()->position().z);

	_rt.perform(_scene, _texture->size());
}

void MainController::applicationWillTerminate()
{
	_rt.stop();
}

void MainController::render(RenderContext* rc)
{
	if (_texture.valid())
	{
		_texture->setImageData(BinaryDataStorage(reinterpret_cast<uint8_t*>(_textureData.data()), _textureData.dataSize()));
	}

	_mainPass->begin();
	_mainPass->pushRenderBatch(_fullscreenQuad);
	_mainPass->end();

	rc->renderer()->submitRenderPass(_mainPass);
}

ApplicationIdentifier MainController::applicationIdentifier() const
{
	return ApplicationIdentifier(applicationIdentifierForCurrentProject(), "Cheetek", "RT demo");
}


IApplicationDelegate* Application::initApplicationDelegate()
{
	return sharedObjectFactory().createObject<et::MainController>();
}

}
