#ifndef PARTICLE_EMITTER_RSRC_H
#define PARTICLE_EMITTER_RSRC_H

#include "Math/Math.h"


/// This is the properties of the particle emitter resource
class ParticleEmitterRsrc
{
	public:
		ParticleEmitterRsrc();
		ParticleEmitterRsrc(const ParticleEmitterRsrc& a);
		virtual ~ParticleEmitterRsrc() {}

		ParticleEmitterRsrc& operator=(const ParticleEmitterRsrc& a);

		bool hasForce() const; ///< Don't call it often. Its slow
		bool usingWorldGrav() const; ///< @return True if gravity is derived

		/// Load it
		void load(const char* filename);

	protected:
		/// @name Particle specific properties
		/// @{
		float particleLife; ///< Required and > 0.0. In seconds
		float particleLifeDeviation;

		Vec3 forceDirection; ///< Not-required, any value, Default 0.0, If not set only the gravity applies
		Vec3 forceDirectionDeviation;
		float forceMagnitude; ///< Default 0.0
		float forceMagnitudeDeviation;

		float particleMass; ///< Required and > 0.0
		float particleMassDeviation;

		Vec3 gravity; ///< Not-required, any value. If not set then it uses the world's default
		Vec3 gravityDeviation;

		Vec3 startingPos; ///< If not set the default is zero
		Vec3 startingPosDeviation;

		float size; ///< The size of the collision shape. Required and > 0.0
		/// @}

		/// @name Emitter specific properties
		/// @{
		uint maxNumOfParticles; ///< The size of the particles vector. Required
		float emissionPeriod; ///< How often the emitter emits new particles. In secs. Required
		uint particlesPerEmittion; ///< How many particles are emitted every emission. Required
		std::string modelName;
		/// @}
};


#endif