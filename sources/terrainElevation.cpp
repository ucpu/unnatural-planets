#include <cage-core/noiseFunction.h>
#include <cage-core/config.h>

#include "terrain.h"
#include "sdf.h"
#include "math.h"

namespace
{
	ConfigString configShapeMode("unnatural-planets/shape/mode");
	ConfigString configElevationMode("unnatural-planets/elevation/mode");

	typedef real (*TerrainFunctor)(const vec3 &);
	TerrainFunctor terrainElevationFnc = 0;
	TerrainFunctor terrainShapeFnc = 0;

	real elevationNone(const vec3 &)
	{
		return 100;
	}

	real elevationSimple(const vec3 &pos)
	{
		static const Holder<NoiseFunction> elevNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Simplex;
			cfg.fractalType = NoiseFractalTypeEnum::Ridged;
			cfg.octaves = 6;
			cfg.gain = 0.4;
			cfg.frequency = 0.0005;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();

		real a = elevNoise->evaluate(pos); // min: -0.8, mean: 0.28, max: 1
		a = -a + 0.3; // min: -0.7, mean: 0.02, max: 1.1
		a = pow(a * 1.3 - 0.35, 3) + 0.1;
		return 100 - a * 1000;
	}

	real elevationLegacy(const vec3 &pos)
	{
		static const Holder<NoiseFunction> scaleNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Value;
			cfg.fractalType = NoiseFractalTypeEnum::Fbm;
			cfg.octaves = 4;
			cfg.frequency = 0.0005;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();
		static const Holder<NoiseFunction> elevNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Value;
			cfg.fractalType = NoiseFractalTypeEnum::Fbm;
			cfg.octaves = 4;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();

		real scale = scaleNoise->evaluate(pos) * 0.0005 + 0.0015;
		real a = elevNoise->evaluate(pos * scale);
		a += 0.11; // slightly prefer terrain over ocean
		if (a < 0)
			a = -pow(-a, 0.85);
		else
			a = pow(a, 1.7);
		return a * 2500;
	}

	real commonElevationMountains(const vec3 &pos, real land)
	{
		static const Holder<NoiseFunction> maskNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Perlin;
			cfg.frequency = 0.0015;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();
		static const Holder<NoiseFunction> ridgeNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Simplex;
			cfg.fractalType = NoiseFractalTypeEnum::Ridged;
			cfg.octaves = 4;
			cfg.lacunarity = 1.5;
			cfg.gain = -0.4;
			cfg.frequency = 0.001;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();
		static const Holder<NoiseFunction> terraceNoise = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Perlin;
			cfg.fractalType = NoiseFractalTypeEnum::Fbm;
			cfg.octaves = 3;
			cfg.gain = 0.3;
			cfg.frequency = 0.002;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();

		real cover = 1 - saturate(land * -0.1); // no mountains in the water
		if (cover < 1e-7)
			return land;

		real mask = maskNoise->evaluate(pos);
		real rm = smoothstep(saturate(mask * +7 - 0.3));
		real tm = smoothstep(saturate(mask * -7 - 1.5));

		real ridge = ridgeNoise->evaluate(pos);
		ridge = max(ridge - 0.1, 0);
		ridge = pow(ridge, 1.6);
		ridge *= rm * cover;
		ridge *= 1000;

		real terraces = terraceNoise->evaluate(pos);
		terraces = max(terraces + 0.1, 0) * 2.5;
		terraces = terrace(terraces, 4);
		terraces *= tm * cover;
		terraces *= 250;

		return land + smoothMax(0, max(ridge, terraces), 50);
	}

	// lakes & islands
	// https://www.wolframalpha.com/input/?i=plot+%28%28%281+-+x%5E0.85%29+*+2+-+1%29+%2F+%28abs%28%28%281+-+x%5E0.85%29+*+2+-+1%29%29+%2B+0.17%29+%2B+0.15%29+*+150+%2C+%28%28%281+-+x%5E1.24%29+*+2+-+1%29+%2F+%28abs%28%28%281+-+x%5E1.24%29+*+2+-+1%29%29+%2B+0.17%29+%2B+0.15%29+*+150+%2C+x+%3D+0+..+1

	real elevationLakes(const vec3 &pos)
	{
		static const Holder<NoiseFunction> elevLand = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Value;
			cfg.fractalType = NoiseFractalTypeEnum::Fbm;
			cfg.octaves = 4;
			cfg.frequency = 0.0013;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();

		real land = elevLand->evaluate(pos) * 0.5 + 0.5;
		land = saturate(land);
		land = 1 - pow(land, 1.24);
		land = land * 2 - 1;
		land = land / (abs(land) + 0.17) + 0.15;
		land *= 150;
		return commonElevationMountains(pos, land);
	}

	real elevationIslands(const vec3& pos)
	{
		static const Holder<NoiseFunction> elevLand = []() {
			NoiseFunctionCreateConfig cfg;
			cfg.type = NoiseTypeEnum::Value;
			cfg.fractalType = NoiseFractalTypeEnum::Fbm;
			cfg.octaves = 4;
			cfg.frequency = 0.0013;
			cfg.seed = noiseSeed();
			return newNoiseFunction(cfg);
		}();

		real land = elevLand->evaluate(pos) * 0.5 + 0.5;
		land = saturate(land);
		land = 1 - pow(land, 0.83);
		land = land * 2 - 1;
		land = land / (abs(land) + 0.17) + 0.15;
		land *= 150;
		return commonElevationMountains(pos, land);
	}

	void chooseElevationFunction()
	{
		constexpr TerrainFunctor elevationModeFunctions[] = {
			&elevationNone,
			&elevationSimple,
			&elevationLegacy,
			&elevationLakes,
			&elevationIslands,
		};

		constexpr uint32 elevationModesCount = sizeof(elevationModeFunctions) / sizeof(elevationModeFunctions[0]);

		constexpr const char *const elevationModeNames[] = {
			"none",
			"simple",
			"legacy",
			"lakes",
			"islands",
		};

		static_assert(elevationModesCount == sizeof(elevationModeNames) / sizeof(elevationModeNames[0]), "number of functions and names must match");

		for (uint32 i = 0; i < elevationModesCount; i++)
			if ((string)configElevationMode == elevationModeNames[i])
				terrainElevationFnc = elevationModeFunctions[i];
		if (!terrainElevationFnc)
		{
			CAGE_LOG_THROW(stringizer() + "elevation mode: '" + (string)configElevationMode + "'");
			CAGE_THROW_ERROR(Exception, "unknown elevation mode configuration");
		}
		CAGE_LOG(SeverityEnum::Info, "configuration", stringizer() + "using elevation mode: '" + (string)configElevationMode + "'");
	}

	void chooseShapeFunction()
	{
		constexpr TerrainFunctor shapeModeFunctions[] = {
			&sdfHexagon,
			&sdfSquare,
			&sdfSphere,
			&sdfTorus,
			&sdfTube,
			&sdfDisk,
			&sdfCapsule,
			&sdfBox,
			&sdfCube,
			&sdfTetrahedron,
			&sdfOctahedron,
			&sdfKnot,
			&sdfMobiusStrip,
			&sdfFibers,
			&sdfH2O,
			&sdfH3O,
			&sdfH4O,
			&sdfTriangularPrism,
			&sdfHexagonalPrism,
		};

		constexpr uint32 shapeModesCount = sizeof(shapeModeFunctions) / sizeof(shapeModeFunctions[0]);

		constexpr const char *const shapeModeNames[] = {
			"hexagon",
			"square",
			"sphere",
			"torus",
			"tube",
			"disk",
			"capsule",
			"box",
			"cube",
			"tetrahedron",
			"octahedron",
			"knot",
			"mobiusstrip",
			"fibers",
			"h2o",
			"h3o",
			"h4o",
			"triangularprism",
			"hexagonalprism",
		};

		static_assert(shapeModesCount == sizeof(shapeModeNames) / sizeof(shapeModeNames[0]), "number of functions and names must match");

		string name = configShapeMode;
		if (name == "random")
		{
			const uint32 i = randomRange(0u, shapeModesCount);
			terrainShapeFnc = shapeModeFunctions[i];
			configShapeMode = name = shapeModeNames[i];
			CAGE_LOG(SeverityEnum::Info, "configuration", stringizer() + "randomly chosen shape mode: '" + name + "'");
		}
		else
		{
			for (uint32 i = 0; i < shapeModesCount; i++)
				if (name == shapeModeNames[i])
					terrainShapeFnc = shapeModeFunctions[i];
			if (!terrainShapeFnc)
			{
				CAGE_LOG_THROW(stringizer() + "shape mode: '" + name + "'");
				CAGE_THROW_ERROR(Exception, "unknown shape mode configuration");
			}
			CAGE_LOG(SeverityEnum::Info, "configuration", stringizer() + "using shape mode: '" + name + "'");
		}
	}

	constexpr real meshElevationRatio = 10;
}

real terrainSdfElevation(const vec3 &pos)
{
	CAGE_ASSERT(terrainShapeFnc != nullptr);
	const real result = terrainShapeFnc(pos) * meshElevationRatio;
	if (!valid(result))
		CAGE_THROW_ERROR(Exception, "invalid elevation sdf value");
	return result;
}

real terrainSdfElevationRaw(const vec3 &pos)
{
	CAGE_ASSERT(terrainElevationFnc != nullptr);
	const real result = terrainElevationFnc(pos);
	if (!valid(result))
		CAGE_THROW_ERROR(Exception, "invalid elevation raw sdf value");
	return result;
}

real terrainSdfLand(const vec3 &pos)
{
	CAGE_ASSERT(terrainShapeFnc != nullptr);
	CAGE_ASSERT(terrainElevationFnc != nullptr);
	const real base = terrainShapeFnc(pos);
	const real elev = terrainElevationFnc(pos) / meshElevationRatio;
	const real result = base - elev;
	if (!valid(result))
		CAGE_THROW_ERROR(Exception, "invalid land sdf value");
	return result;
}

real terrainSdfWater(const vec3 &pos)
{
	CAGE_ASSERT(terrainShapeFnc != nullptr);
	const real result = terrainShapeFnc(pos);
	if (!valid(result))
		CAGE_THROW_ERROR(Exception, "invalid water sdf value");
	return result;
}

real terrainSdfNavigation(const vec3 &pos)
{
	CAGE_ASSERT(terrainShapeFnc != nullptr);
	CAGE_ASSERT(terrainElevationFnc != nullptr);
	const real base = terrainShapeFnc(pos);
	const real elev = terrainElevationFnc(pos) / meshElevationRatio;
	const real result = base - max(elev, 0);
	if (!valid(result))
		CAGE_THROW_ERROR(Exception, "invalid navigation sdf value");
	return result;
}

void terrainApplyConfig()
{
	chooseShapeFunction();
	chooseElevationFunction();
}
