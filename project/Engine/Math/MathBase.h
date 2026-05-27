#pragma once
#include <cmath>
#include <algorithm>
#include <limits>

namespace Fujin {

	namespace Math {
		static constexpr float PI = 3.14159265358979323846f;
		static constexpr float TWO_PI = 6.28318530717958647692f;
		static constexpr float HALF_PI = 1.57079632679489661923f;
		static constexpr float INV_PI = 0.31830988618379067154f;
		static constexpr float SMALL_NUMBER = 1.e-8f;
		static constexpr float KINDA_SMALL_NUMBER = 1.e-4f;

		template<typename T>
		inline T Max(T a, T b) { return (a > b) ? a : b; }

		template<typename T>
		inline T Min(T a, T b) { return (a < b) ? a : b; }

		template<typename T>
		inline T Clamp(T value, T min, T max) {
			return (value < min) ? min : (value > max) ? max : value;
		}

		inline float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
		inline float ToDegrees(float radians) { return radians * (180.0f / PI); }

		inline bool IsNearlyZero(float value, float tolerance = SMALL_NUMBER) {
			return std::abs(value) <= tolerance;
		}

		inline bool IsNearlyEqual(float a, float b, float tolerance = SMALL_NUMBER) {
			return std::abs(a - b) <= tolerance;
		}

		inline float Lerp(float a, float b, float t) {
			return a + t * (b - a);
		}
	}

} // namespace Fujin
