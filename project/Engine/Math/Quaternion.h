#pragma once
#include "Vector.h"
#include "Matrix.h"

namespace Fujin {

	struct Quaternion {
		float x, y, z, w;

		Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
		Quaternion(float inX, float inY, float inZ, float inW) : x(inX), y(inY), z(inZ), w(inW) {}

		static const Quaternion Identity;

		static Quaternion FromEuler(float pitch, float yaw, float roll) {
			float cp = std::cos(pitch * 0.5f);
			float sp = std::sin(pitch * 0.5f);
			float cy = std::cos(yaw * 0.5f);
			float sy = std::sin(yaw * 0.5f);
			float cr = std::cos(roll * 0.5f);
			float sr = std::sin(roll * 0.5f);

			Quaternion q;
			q.w = cr * cp * cy + sr * sp * sy;
			q.x = sr * cp * cy - cr * sp * sy;
			q.y = cr * sp * cy + sr * cp * sy;
			q.z = cr * cp * sy - sr * sp * cy;
			return q;
		}

		static Quaternion FromAxisAngle(const Vector3& axis, float angle) {
			float halfAngle = angle * 0.5f;
			float s = std::sin(halfAngle);
			return Quaternion(axis.x * s, axis.y * s, axis.z * s, std::cos(halfAngle));
		}

		Quaternion operator*(const Quaternion& q) const {
			return Quaternion(
				w * q.x + x * q.w + y * q.z - z * q.y,
				w * q.y - x * q.z + y * q.w + z * q.x,
				w * q.z + x * q.y - y * q.x + z * q.w,
				w * q.w - x * q.x - y * q.y - z * q.z
			);
		}

		Vector3 operator*(const Vector3& v) const {
			Vector3 qv(x, y, z);
			Vector3 t = 2.0f * Vector3::Cross(qv, v);
			return v + w * t + Vector3::Cross(qv, t);
		}

		void Normalize() {
			float lenSq = x * x + y * y + z * z + w * w;
			if (lenSq > Math::SMALL_NUMBER) {
				float invLen = 1.0f / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
				z *= invLen;
				w *= invLen;
			}
		}

		static Quaternion Slerp(const Quaternion& a, Quaternion b, float t) {
			float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
			if (dot < 0.0f) { b = Quaternion(-b.x, -b.y, -b.z, -b.w); dot = -dot; }
			if (dot > 0.9995f) {
				Quaternion r(a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), a.z + t*(b.z-a.z), a.w + t*(b.w-a.w));
				r.Normalize(); return r;
			}
			float theta0 = std::acos(dot);
			float theta   = theta0 * t;
			float s0 = std::sin(theta0 - theta) / std::sin(theta0);
			float s1 = std::sin(theta)           / std::sin(theta0);
			return Quaternion(s0*a.x + s1*b.x, s0*a.y + s1*b.y, s0*a.z + s1*b.z, s0*a.w + s1*b.w);
		}

		Matrix4x4 ToMatrix() const {
			Matrix4x4 m;
			float xx = x * x; float yy = y * y; float zz = z * z;
			float xy = x * y; float xz = x * z; float yz = y * z;
			float wx = w * x; float wy = w * y; float wz = w * z;

			m.m[0][0] = 1.0f - 2.0f * (yy + zz);
			m.m[0][1] = 2.0f * (xy - wz);
			m.m[0][2] = 2.0f * (xz + wy);

			m.m[1][0] = 2.0f * (xy + wz);
			m.m[1][1] = 1.0f - 2.0f * (xx + zz);
			m.m[1][2] = 2.0f * (yz - wx);

			m.m[2][0] = 2.0f * (xz - wy);
			m.m[2][1] = 2.0f * (yz + wx);
			m.m[2][2] = 1.0f - 2.0f * (xx + yy);

			return m;
		}
	};

} // namespace Fujin
