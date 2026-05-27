#pragma once
#include "MathBase.h"

namespace Fujin {

	struct Vector2 {
		float x, y;

		Vector2() : x(0.0f), y(0.0f) {}
		Vector2(float inX, float inY) : x(inX), y(inY) {}
		explicit Vector2(float value) : x(value), y(value) {}

		static const Vector2 Zero;
		static const Vector2 One;
		static const Vector2 UnitX;
		static const Vector2 UnitY;

		Vector2 operator+(const Vector2& v) const { return Vector2(x + v.x, y + v.y); }
		Vector2 operator-(const Vector2& v) const { return Vector2(x - v.x, y - v.y); }
		Vector2 operator*(const Vector2& v) const { return Vector2(x * v.x, y * v.y); }
		Vector2 operator/(const Vector2& v) const { return Vector2(x / v.x, y / v.y); }

		Vector2 operator*(float s) const { return Vector2(x * s, y * s); }
		Vector2 operator/(float s) const { float inv = 1.0f / s; return Vector2(x * inv, y * inv); }

		Vector2& operator+=(const Vector2& v) { x += v.x; y += v.y; return *this; }
		Vector2& operator-=(const Vector2& v) { x -= v.x; y -= v.y; return *this; }
		Vector2& operator*=(float s) { x *= s; y *= s; return *this; }
		Vector2& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; return *this; }

		float LengthSquared() const { return x * x + y * y; }
		float Length() const { return std::sqrt(LengthSquared()); }

		void Normalize() {
			float lenSq = LengthSquared();
			if (lenSq > Math::SMALL_NUMBER) {
				float invLen = 1.0f / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
			}
		}

		Vector2 GetSafeNormal() const {
			Vector2 v = *this;
			v.Normalize();
			return v;
		}

		static float Dot(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
	};

	struct Vector3 {
		float x, y, z;

		Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
		Vector3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}
		explicit Vector3(float value) : x(value), y(value), z(value) {}
		Vector3(const Vector2& v, float inZ) : x(v.x), y(v.y), z(inZ) {}

		static const Vector3 Zero;
		static const Vector3 One;
		static const Vector3 UnitX;
		static const Vector3 UnitY;
		static const Vector3 UnitZ;
		static const Vector3 Up;
		static const Vector3 Forward;
		static const Vector3 Right;

		Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
		Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
		Vector3 operator*(const Vector3& v) const { return Vector3(x * v.x, y * v.y, z * v.z); }
		Vector3 operator/(const Vector3& v) const { return Vector3(x / v.x, y / v.y, z / v.z); }

		Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
		Vector3 operator/(float s) const { float inv = 1.0f / s; return Vector3(x * inv, y * inv, z * inv); }
		Vector3 operator-() const { return Vector3(-x, -y, -z); }

		Vector3& operator+=(const Vector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
		Vector3& operator-=(const Vector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
		Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
		Vector3& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; return *this; }

		float LengthSquared() const { return x * x + y * y + z * z; }
		float Length() const { return std::sqrt(LengthSquared()); }

		void Normalize() {
			float lenSq = LengthSquared();
			if (lenSq > Math::SMALL_NUMBER) {
				float invLen = 1.0f / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
				z *= invLen;
			}
		}

		Vector3 GetSafeNormal() const {
			Vector3 v = *this;
			v.Normalize();
			return v;
		}

		static float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		static Vector3 Cross(const Vector3& a, const Vector3& b) {
			return Vector3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			);
		}
	};

	struct Vector4 {
		float x, y, z, w;

		Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
		Vector4(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}
		explicit Vector4(float value) : x(value), y(value), z(value), w(value) {}
		Vector4(const Vector3& v, float inW) : x(v.x), y(v.y), z(v.z), w(inW) {}

		static const Vector4 Zero;
		static const Vector4 One;

		Vector4 operator+(const Vector4& v) const { return Vector4(x + v.x, y + v.y, z + v.z, w + v.w); }
		Vector4 operator-(const Vector4& v) const { return Vector4(x - v.x, y - v.y, z - v.z, w - v.w); }
		Vector4 operator*(const Vector4& v) const { return Vector4(x * v.x, y * v.y, z * v.z, w * v.w); }
		Vector4 operator/(const Vector4& v) const { return Vector4(x / v.x, y / v.y, z / v.z, w / v.w); }

		Vector4 operator*(float s) const { return Vector4(x * s, y * s, z * s, w * s); }
		Vector4 operator/(float s) const { float inv = 1.0f / s; return Vector4(x * inv, y * inv, z * inv, w * inv); }

		Vector4& operator+=(const Vector4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
		Vector4& operator-=(const Vector4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
		Vector4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
		Vector4& operator/=(float s) { float inv = 1.0f / s; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }

		float LengthSquared() const { return x * x + y * y + z * z + w * w; }
		float Length() const { return std::sqrt(LengthSquared()); }

		static float Dot(const Vector4& a, const Vector4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
	};

	inline Vector2 operator*(float s, const Vector2& v) { return v * s; }
	inline Vector3 operator*(float s, const Vector3& v) { return v * s; }
	inline Vector4 operator*(float s, const Vector4& v) { return v * s; }

} // namespace Fujin
