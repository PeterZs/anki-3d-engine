// Copyright (C) 2009-2018, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#include <anki/renderer/LightShading.h>
#include <anki/renderer/Renderer.h>
#include <anki/renderer/ShadowMapping.h>
#include <anki/renderer/Indirect.h>
#include <anki/renderer/GBuffer.h>
#include <anki/renderer/RenderQueue.h>
#include <anki/renderer/ForwardShading.h>
#include <anki/renderer/DepthDownscale.h>
#include <anki/renderer/Ssao.h>
#include <anki/renderer/Ssr.h>
#include <anki/misc/ConfigSet.h>
#include <anki/util/HighRezTimer.h>

namespace anki
{

LightShading::LightShading(Renderer* r)
	: RendererObject(r)
{
}

LightShading::~LightShading()
{
}

Error LightShading::init(const ConfigSet& config)
{
	ANKI_R_LOGI("Initializing light stage");
	Error err = initInternal(config);

	if(err)
	{
		ANKI_R_LOGE("Failed to init light stage");
	}

	return err;
}

Error LightShading::initInternal(const ConfigSet& config)
{
	// Load shaders and programs
	ANKI_CHECK(getResourceManager().loadResource("shaders/LightShading.glslp", m_prog));

	ShaderProgramResourceConstantValueInitList<5> consts(m_prog);
	consts.add("CLUSTER_COUNT_X", U32(m_r->getClusterCount()[0]))
		.add("CLUSTER_COUNT_Y", U32(m_r->getClusterCount()[1]))
		.add("CLUSTER_COUNT_Z", U32(m_r->getClusterCount()[2]))
		.add("CLUSTER_COUNT", U32(m_r->getClusterCount()[3]))
		.add("IR_MIPMAP_COUNT", U32(m_r->getIndirect().getReflectionTextureMipmapCount()));

	m_prog->getOrCreateVariant(consts.get(), m_progVariant);

	// Create RT descr
	m_rtDescr = m_r->create2DRenderTargetDescription(
		m_r->getWidth(), m_r->getHeight(), LIGHT_SHADING_COLOR_ATTACHMENT_PIXEL_FORMAT, "Light Shading");
	m_rtDescr.bake();

	// Create FB descr
	m_fbDescr.m_colorAttachmentCount = 1;
	m_fbDescr.m_colorAttachments[0].m_loadOperation = AttachmentLoadOperation::DONT_CARE;
	m_fbDescr.bake();

	// FS upscale
	{
		ANKI_CHECK(getResourceManager().loadResource("engine_data/BlueNoiseLdrRgb64x64.ankitex", m_fs.m_noiseTex));

		// Shader
		ANKI_CHECK(getResourceManager().loadResource("shaders/ForwardShadingUpscale.glslp", m_fs.m_prog));

		ShaderProgramResourceConstantValueInitList<3> consts(m_fs.m_prog);
		consts.add("NOISE_TEX_SIZE", U32(m_fs.m_noiseTex->getWidth()))
			.add("SRC_SIZE", Vec2(m_r->getWidth() / FS_FRACTION, m_r->getHeight() / FS_FRACTION))
			.add("FB_SIZE", Vec2(m_r->getWidth(), m_r->getWidth()));

		const ShaderProgramResourceVariant* variant;
		m_fs.m_prog->getOrCreateVariant(consts.get(), variant);
		m_fs.m_grProg = variant->getProgram();
	}

	return Error::NONE;
}

void LightShading::run(const RenderingContext& ctx, RenderPassWorkContext& rgraphCtx)
{
	CommandBufferPtr& cmdb = rgraphCtx.m_commandBuffer;
	const ClusterBinOut& rsrc = ctx.m_clusterBinOut;

	cmdb->setViewport(0, 0, m_r->getWidth(), m_r->getHeight());

	// Do light shading
	{
		cmdb->bindShaderProgram(m_progVariant->getProgram());

		// Bind textures
		rgraphCtx.bindColorTextureAndSampler(0, 0, m_r->getGBuffer().getColorRt(0), m_r->getNearestSampler());
		rgraphCtx.bindColorTextureAndSampler(0, 1, m_r->getGBuffer().getColorRt(1), m_r->getNearestSampler());
		rgraphCtx.bindColorTextureAndSampler(0, 2, m_r->getGBuffer().getColorRt(2), m_r->getNearestSampler());
		rgraphCtx.bindTextureAndSampler(0,
			3,
			m_r->getGBuffer().getDepthRt(),
			TextureSubresourceInfo(DepthStencilAspectBit::DEPTH),
			m_r->getNearestSampler());
		rgraphCtx.bindColorTextureAndSampler(0, 4, m_r->getSsr().getRt(), m_r->getLinearSampler());
		rgraphCtx.bindColorTextureAndSampler(0, 5, m_r->getSsao().getRt(), m_r->getLinearSampler());

		rgraphCtx.bindColorTextureAndSampler(0, 6, m_r->getShadowMapping().getShadowmapRt(), m_r->getLinearSampler());
		rgraphCtx.bindColorTextureAndSampler(
			0, 7, m_r->getIndirect().getReflectionRt(), m_r->getTrilinearRepeatSampler());
		rgraphCtx.bindColorTextureAndSampler(
			0, 8, m_r->getIndirect().getIrradianceRt(), m_r->getTrilinearRepeatSampler());
		cmdb->bindTextureAndSampler(0,
			9,
			m_r->getIndirect().getIntegrationLut(),
			m_r->getIndirect().getIntegrationLutSampler(),
			TextureUsageBit::SAMPLED_FRAGMENT);

		// Bind uniforms
		bindUniforms(cmdb, 0, 0, ctx.m_lightShadingUniformsToken);
		bindUniforms(cmdb, 0, 1, rsrc.m_pointLightsToken);
		bindUniforms(cmdb, 0, 2, rsrc.m_spotLightsToken);
		bindUniforms(cmdb, 0, 3, rsrc.m_probesToken);

		// Bind storage
		bindStorage(cmdb, 0, 0, rsrc.m_clustersToken);
		bindStorage(cmdb, 0, 1, rsrc.m_indicesToken);

		drawQuad(cmdb);
	}

	// Forward shading
	{
		// Bind textures
		rgraphCtx.bindTextureAndSampler(0,
			0,
			m_r->getGBuffer().getDepthRt(),
			TextureSubresourceInfo(DepthStencilAspectBit::DEPTH),
			m_r->getNearestSampler());
		rgraphCtx.bindTextureAndSampler(
			0, 1, m_r->getDepthDownscale().getHiZRt(), HIZ_HALF_DEPTH, m_r->getNearestSampler());
		rgraphCtx.bindColorTextureAndSampler(0, 2, m_r->getForwardShading().getRt(), m_r->getLinearSampler());
		cmdb->bindTextureAndSampler(0,
			3,
			m_fs.m_noiseTex->getGrTextureView(),
			m_r->getTrilinearRepeatSampler(),
			TextureUsageBit::SAMPLED_FRAGMENT);

		// Bind uniforms
		Vec4* linearDepth = allocateAndBindUniforms<Vec4*>(sizeof(Vec4), cmdb, 0, 0);
		computeLinearizeDepthOptimal(
			ctx.m_renderQueue->m_cameraNear, ctx.m_renderQueue->m_cameraFar, linearDepth->x(), linearDepth->y());
		linearDepth->z() = ctx.m_renderQueue->m_cameraFar;

		// Other state & draw
		cmdb->setBlendFactors(0, BlendFactor::ONE, BlendFactor::SRC_ALPHA);
		cmdb->bindShaderProgram(m_fs.m_grProg);
		drawQuad(cmdb);
	}

	// Restore state
	cmdb->setBlendFactors(0, BlendFactor::ONE, BlendFactor::ZERO);
}

void LightShading::populateRenderGraph(RenderingContext& ctx)
{
	m_runCtx.m_ctx = &ctx;
	RenderGraphDescription& rgraph = ctx.m_renderGraphDescr;

	// Create RT
	m_runCtx.m_rt = rgraph.newRenderTarget(m_rtDescr);

	// Create pass
	GraphicsRenderPassDescription& pass = rgraph.newGraphicsRenderPass("Light Shading");

	pass.setWork(runCallback, this, 0);
	pass.setFramebufferInfo(m_fbDescr, {{m_runCtx.m_rt}}, {});

	// Light shading
	pass.newDependency({m_runCtx.m_rt, TextureUsageBit::FRAMEBUFFER_ATTACHMENT_WRITE});
	pass.newDependency({m_r->getGBuffer().getColorRt(0), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getGBuffer().getColorRt(1), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getGBuffer().getColorRt(2), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getGBuffer().getDepthRt(),
		TextureUsageBit::SAMPLED_FRAGMENT,
		TextureSubresourceInfo(DepthStencilAspectBit::DEPTH)});
	pass.newDependency({m_r->getShadowMapping().getShadowmapRt(), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getSsao().getRt(), TextureUsageBit::SAMPLED_FRAGMENT});

	// Refl & indirect
	pass.newDependency({m_r->getSsr().getRt(), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getIndirect().getReflectionRt(), TextureUsageBit::SAMPLED_FRAGMENT});
	pass.newDependency({m_r->getIndirect().getIrradianceRt(), TextureUsageBit::SAMPLED_FRAGMENT});

	// For forward shading
	pass.newDependency({m_r->getDepthDownscale().getHiZRt(), TextureUsageBit::SAMPLED_FRAGMENT, HIZ_HALF_DEPTH});
	pass.newDependency({m_r->getForwardShading().getRt(), TextureUsageBit::SAMPLED_FRAGMENT});
}

} // end namespace anki
