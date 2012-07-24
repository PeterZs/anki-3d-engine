#ifndef ANKI_RENDERER_IS_H
#define ANKI_RENDERER_IS_H

#include "anki/renderer/RenderingPass.h"
#include "anki/resource/TextureResource.h"
#include "anki/resource/Resource.h"
#include "anki/resource/ShaderProgramResource.h"
#include "anki/gl/Gl.h"
#include "anki/math/Math.h"
#include "anki/renderer/Sm.h"
#include "anki/renderer/Smo.h"

namespace anki {

class PointLight;
class SpotLight;

/// Illumination stage
class Is: private RenderingPass
{
public:
	Is(Renderer* r);

	~Is();

	void init(const RendererInitializer& initializer);
	void run();

	/// @name Accessors
	/// @{
	const Texture& getFai() const
	{
		return fai;
	}
	/// @}

private:
	Texture fai;
	Fbo fbo;
	BufferObject ubo;

	/// Illumination stage ambient pass shader program
	ShaderProgramResourcePointer ambientPassSProg;
	/// Illumination stage point light shader program
	ShaderProgramResourcePointer pointLightSProg;
	/// Illumination stage spot light w/o shadow shader program
	ShaderProgramResourcePointer spotLightNoShadowSProg;
	/// Illumination stage spot light w/ shadow shader program
	ShaderProgramResourcePointer spotLightShadowSProg;

	/// The ambient pass
	void ambientPass(const Vec3& color);

	/// The point light pass
	void pointLightPass(PointLight& plight);

	/// The spot light pass
	void spotLightPass(SpotLight& slight);
};

} // end namespace anki

#endif